/*
 * Controlled handle registry for the Android JNI bridge.
 *
 * The bridge used to hand raw object addresses to Java as `long` values. A
 * stale address, an address of a different object kind, or one the allocator
 * happened to hand out again all reached libav* unchecked, which is how a
 * late call could crash the process inside FFmpeg.
 *
 * A handle is now an opaque token, never a pointer:
 *
 *   bit 63        always 0        (so the token is a positive jlong)
 *   bits 62..32   generation      31 bits, never 0, never wrapped
 *   bits 31..16   object type     16 bits, FFHandleType
 *   bits 15..0    slot index + 1  16 bits, 0 means "no handle"
 *
 * That gives three independent checks - type, generation and slot range -
 * before any pointer is dereferenced, and it keeps a handle from one object
 * lifetime from ever naming a newer object that reuses the same slot.
 *
 * Ownership rules the callers must honour:
 *
 *   1. ff_handle_create() returns a token that owns one *creator* reference.
 *   2. ff_handle_acquire() takes one *operation* reference and hands back the
 *      object; the caller must pair it with ff_handle_release().
 *   3. ff_handle_revoke() stops new operations and drops the creator
 *      reference. It does NOT free the object.
 *   4. The object is finalised exactly once, by the release that drops the
 *      last reference. The slot becomes reusable only after the finaliser has
 *      returned.
 *
 * Consequence: revoking a handle only means "no new operation may start". It
 * never pulls memory out from under an operation that is already running.
 *
 * This unit deliberately depends on nothing but pthreads and stdint, so it can
 * be exercised by a host test without FFmpeg, JNI or Android.
 */

#ifndef FFMPEG_JNI_HANDLE_REGISTRY_H
#define FFMPEG_JNI_HANDLE_REGISTRY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum FFHandleType {
    FF_HANDLE_TYPE_NONE           = 0,
    FF_HANDLE_TYPE_FORMAT_CONTEXT = 1,
    FF_HANDLE_TYPE_DICTIONARY     = 2,
    FF_HANDLE_TYPE_DICT_CURSOR    = 3,
    FF_HANDLE_TYPE_INTERRUPT      = 4,
    FF_HANDLE_TYPE_RENDERER       = 5,
    FF_HANDLE_TYPE_ASYNC_DECODER  = 6,
    FF_HANDLE_TYPE_MAX
} FFHandleType;

/* Status codes. Kept small and negative so they can never be confused with an
 * AVERROR value or with 0 == success. */
enum {
    FF_HANDLE_OK            = 0,
    FF_HANDLE_ERR_TOKEN     = -1, /* 0, reserved bit set, bad type/slot field  */
    FF_HANDLE_ERR_TYPE      = -2, /* live slot, but another object kind        */
    FF_HANDLE_ERR_STALE     = -3, /* generation already retired                */
    FF_HANDLE_ERR_CLOSING   = -4, /* revoked: no new operation may start       */
    FF_HANDLE_ERR_NOMEM     = -5, /* no free slot                             */
    FF_HANDLE_ERR_EXHAUSTED = -6, /* slot hit the generation ceiling           */
    FF_HANDLE_ERR_STATE     = -7, /* wrong state for the call                  */
    FF_HANDLE_ERR_TIMEOUT   = -8  /* declared wait expired                     */
};

/* Slots are shared by every managed object kind. 192 covers the bridge's own
 * concurrency (one format context per player, one dictionary per open, one
 * cursor per iteration, one renderer per surface) with a wide margin, and
 * doubles as the cap on abandoned cursors. */
#define FF_HANDLE_MAX_SLOTS 192

/* Highest generation a slot may reach. 31 bits so that (gen << 32) keeps the
 * sign bit of the int64 token clear. A slot that reaches it is retired for
 * good instead of wrapping: a wrapped generation would make an expired token
 * valid again. */
#define FF_HANDLE_GEN_MAX 0x7FFFFFFFu

typedef void (*ff_handle_finalize_fn)(void *obj);

/* Creates a token owning the creator reference. `obj` must be non-NULL and
 * `finalize` may be NULL for an object that needs no teardown.
 * @return the token, or 0 when no slot is available. */
int64_t ff_handle_create(FFHandleType type, void *obj, ff_handle_finalize_fn finalize);

/* Takes one operation reference.
 * @return the object, or NULL with *status set when the token must not be
 *         dereferenced (unknown token, stale generation, other type, closing).
 *         `status` may be NULL. */
void *ff_handle_acquire(int64_t token, FFHandleType type, int *status);

/* Drops one operation reference. When it is the last one the object is
 * finalised before this call returns and the slot becomes reusable. */
int ff_handle_release(int64_t token, FFHandleType type);

/* Stops new acquisitions and drops the creator reference exactly once.
 * Idempotent. The object stays alive while any operation reference remains. */
int ff_handle_revoke(int64_t token, FFHandleType type);

/* 1 while the token is closing, 0 while it accepts operations, negative when
 * the token does not name a live object of this type. */
int ff_handle_closing(int64_t token, FFHandleType type);

/* Waits until the token's object has been finalised and its slot released.
 * @return FF_HANDLE_OK, FF_HANDLE_ERR_TIMEOUT, or a negative token error. */
int ff_handle_wait_done(int64_t token, FFHandleType type, unsigned timeout_ms);

/* Number of outstanding references, or a negative status for a dead token.
 * Diagnostics and tests only; never a basis for a control decision. */
int ff_handle_refcount(int64_t token, FFHandleType type);

/* Whether the value names a live managed object of any type.
 *
 * Used at the boundary of the legacy entry points that take an unmanaged
 * pointer: a managed token must be refused there rather than dereferenced as
 * an address. Exact by construction - a real pointer does not carry the
 * generation of a slot that is currently live. */
int ff_handle_is_live(int64_t token);

/* Keeps the object and retires the slot permanently. Used when teardown cannot
 * be proven: leaking one object and one slot is the honest outcome, reusing
 * either would be a silent claim of safety the bridge cannot back up. */
int ff_handle_quarantine(int64_t token, FFHandleType type);

/* Test support: forget every slot. Only valid when nothing is live. */
void ff_handle_registry_reset_for_tests(void);

/* Test support: re-issue a live slot at `gen` so the ceiling behaviour can be
 * exercised without 2^31 create() calls. The replacement token is written to
 * *new_token_out (the original token stops round-tripping).
 * @return FF_HANDLE_OK, or a negative token status. */
int ff_handle_test_set_gen(int64_t token, FFHandleType type, uint32_t gen,
                           int64_t *new_token_out);

/* Decodes a token without touching any object. Diagnostics and tests use it to
 * report type/slot/generation in a log line.
 * @return FF_HANDLE_OK, or FF_HANDLE_ERR_TOKEN for a malformed value. */
int ff_handle_inspect(int64_t token, FFHandleType *type_out, int *slot_out,
                      uint32_t *gen_out);

#ifdef __cplusplus
}
#endif

#endif /* FFMPEG_JNI_HANDLE_REGISTRY_H */
