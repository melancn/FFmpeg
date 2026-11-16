/*
 * Managed wrapper around AVFormatContext for the Android JNI bridge.
 *
 * A format context is the most widely shared object the bridge hands to Java:
 * demuxing reads from it on the playback thread while a teardown thread may
 * close it, a stats overlay queries streams from it, and probes, seeks and
 * flushes come from yet other threads. Handing Java its address meant every
 * one of those entry points dereferenced whatever the caller stored, even
 * after the context had been closed.
 *
 * This unit gives the context an identity (a controlled token from
 * ffmpeg_jni_handle_registry) and two separate protections, because they
 * solve different problems:
 *
 *   - the registry reference keeps the context from being destroyed while an
 *     operation is using it;
 *   - the per-context exec mutex keeps two operations on the same context from
 *     interleaving (av_read_frame, av_seek_frame, avformat_find_stream_info
 *     and metadata reads are not re-entrant on one context).
 *
 * Closing is therefore a protocol, not a free():
 *
 *   revoke the token        no new operation may start
 *   raise the interrupt     a thread blocked in a socket read returns
 *   wait for the last user  every admitted operation finishes
 *   destroy                 avformat_close_input or the output-context path
 *   release the flag        only now, so the callback cannot outlive its data
 *
 * The interrupt flag is a registry object of its own, and a format context
 * holds a reference to it. That reference is what makes it safe for libavformat
 * to keep calling back into the flag after Java has dropped its own handle.
 */

#ifndef FFMPEG_JNI_FORMAT_HANDLE_H
#define FFMPEG_JNI_FORMAT_HANDLE_H

#include <stdatomic.h>
#include <stdint.h>

#include "libavformat/avformat.h"
#include "ffmpeg_jni_handle_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Result of a close request. Anything other than FF_FORMAT_CLOSE_OK means the
 * native resources may still exist: the caller must not report them gone. */
#define FF_FORMAT_CLOSE_OK          0
#define FF_FORMAT_CLOSE_UNCONFIRMED (-1)  /* waited, still in use            */
#define FF_FORMAT_CLOSE_INVALID     (-2)  /* token is not a format context   */

/* How the context was created decides how it must be destroyed. Using the
 * wrong entry point here is a double free or a leak, so the origin is recorded
 * at publication time instead of being guessed at close time. */
typedef enum FFFormatSource {
    FF_FORMAT_SOURCE_INPUT = 0,  /* avformat_open_input   -> close_input      */
    FF_FORMAT_SOURCE_OUTPUT,     /* output context, may own ctx->pb            */
    FF_FORMAT_SOURCE_ALLOCATED   /* avformat_alloc_context, no I/O yet         */
} FFFormatSource;

/* The flag a format context polls. Written by the controller, read inside
 * libavformat's I/O loops, so it is a real atomic, not a volatile int. */
typedef struct FFInterruptFlag {
    atomic_int aborted;
} FFInterruptFlag;

/* ------------------------------------------------------------------ */
/* Interrupt flag                                                      */
/* ------------------------------------------------------------------ */

/* Allocates a cleared flag. @return token owning the creator reference, 0 on
 * allocation failure. */
int64_t ff_interrupt_create(void);

/* Raises the flag. Takes no exec lock and never waits: it must be callable
 * while the reading thread is parked in a network operation.
 * @return 0, or a negative status for a dead token (a no-op by design). */
int ff_interrupt_abort(int64_t token);

/* Takes one operation reference on the flag. @return the flag or NULL. */
FFInterruptFlag *ff_interrupt_acquire(int64_t token, int *status);

/* Drops a reference taken by ff_interrupt_acquire(). */
void ff_interrupt_release(int64_t token);

/* Drops the creator reference. The object stays alive while a format context
 * still holds it. */
int ff_interrupt_revoke(int64_t token);

/* Whether the flag has been raised. Safe for the callback's use. */
int ff_interrupt_raised(const FFInterruptFlag *flag);

/* Installs the callback on a context that has not started I/O yet. */
void ff_format_install_interrupt(AVFormatContext *ctx, FFInterruptFlag *flag);

/* ------------------------------------------------------------------ */
/* Format context                                                      */
/* ------------------------------------------------------------------ */

typedef struct FFFormatHandle FFFormatHandle;

/* One admitted operation. Holds both a registry reference and the exec lock
 * for as long as it is open. */
typedef struct FFFormatOp {
    FFFormatHandle  *handle;
    AVFormatContext *ctx;
    int64_t          token;
} FFFormatOp;

/* Publishes an opened or allocated context.
 *
 * Takes ownership: on success the context is destroyed by the handle, on
 * failure it is destroyed here before returning 0, so the caller never has to
 * guess who owns a half-built context.
 *
 * @param ctx        context to adopt, may be NULL (returns 0)
 * @param src        how it was created, which selects the destructor
 * @param flag_token interrupt token this context polls, 0 for none; the
 *                   reference is adopted by the handle
 * @param flag       the same flag's object, non-NULL when flag_token is set
 * @return the token, or 0.
 */
int64_t ff_format_publish(AVFormatContext *ctx, FFFormatSource src,
                          int64_t flag_token, FFInterruptFlag *flag);

/* Records that this context owns ctx->pb (set after a successful ioOpen), so
 * the output destructor closes the I/O before freeing the context. */
int ff_format_mark_pb_owned(int64_t token);

/* The same, from inside an already admitted operation. No lock is taken: the
 * caller holds it. */
void ff_format_op_mark_pb_owned(FFFormatOp *op);

/* Forgets pb ownership after ioClose() closed it, so the destructor does not
 * close it a second time. */
void ff_format_op_clear_pb_owned(FFFormatOp *op);

/* Attaches an interrupt flag to an already published context, for the paths
 * that open first and bind afterwards. Idempotent for the same flag; refuses
 * to swap in a different one once one is bound, because libavformat may
 * already be holding the old callback data. */
int ff_format_attach_interrupt(int64_t token, int64_t flag_token);

/* Begins an operation: take a reference, take the exec lock, then re-check
 * that the context has not started closing while the lock was contended.
 * @return the context, or NULL (with `op` left empty, safe to end). */
AVFormatContext *ff_format_begin(int64_t token, FFFormatOp *op);

/* Ends an operation: drop the exec lock and the reference. Idempotent. */
void ff_format_end(FFFormatOp *op);

/* Cleanup attribute target, so every early return in an entry point releases
 * the operation automatically. */
void ff_format_op_cleanup(FFFormatOp *op);

#if defined(__clang__) || defined(__GNUC__)
/* Declares `op` and `ctxvar`, and binds the context for the enclosing block.
 * Any return path - including the error ones - ends the operation. */
#define FF_FORMAT_OP_BEGIN(token, op, ctxvar)                                \
    FFFormatOp op __attribute__((cleanup(ff_format_op_cleanup)));            \
    AVFormatContext *ctxvar = ff_format_begin((token), &op)
#else
#error "the bridge requires __attribute__((cleanup)) for FF_FORMAT_OP_BEGIN"
#endif

/* Step 1 of the close protocol: refuse new operations and wake blocked I/O.
 * Does not wait and does not free. *in_flight_out, when given, receives the
 * number of references still outstanding (including the caller's own). */
int ff_format_request_close(int64_t token, int *in_flight_out);

/* Step 2: wait until the object has actually been destroyed and its slot
 * released. @return FF_FORMAT_CLOSE_OK or FF_FORMAT_CLOSE_UNCONFIRMED. */
int ff_format_await_close(int64_t token, unsigned timeout_ms);

/* The whole protocol. Returns FF_FORMAT_CLOSE_OK only when the native context
 * is gone; FF_FORMAT_CLOSE_UNCONFIRMED leaves the object intact for the last
 * operation to release, and never reuses the slot. */
int ff_format_close(int64_t token, unsigned timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* FFMPEG_JNI_FORMAT_HANDLE_H */
