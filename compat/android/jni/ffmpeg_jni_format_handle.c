/*
 * Managed AVFormatContext - implementation.
 *
 * Two things are deliberately kept apart here:
 *
 *   the registry reference   guards the object's lifetime
 *   the exec mutex           serialises operations on one context
 *
 * Closing takes the first and never the second, so an abort can be raised while
 * the reading thread is parked in a socket read holding the exec lock.
 */

#include "ffmpeg_jni_format_handle.h"

#include <pthread.h>
#include <string.h>

#include "libavformat/avio.h"
#include "libavutil/error.h"
#include "libavutil/mem.h"

struct FFFormatHandle {
    AVFormatContext *ctx;
    FFFormatSource   source;
    /* Serialises av_read_frame / seek / flush / probe / metadata reads on this
     * one context. Never taken by the close path. */
    pthread_mutex_t  exec;
    int              exec_ready;
    /* Set once the bridge opened ctx->pb, which then has to be closed before
     * the context itself is freed. */
    int              owns_pb;
    /* Borrowed pointer, kept valid by the registry reference we hold on
     * flag_token. libavformat polls this after Java's own handle is gone. */
    FFInterruptFlag *flag;
    int64_t          flag_token;
};

/* ------------------------------------------------------------------ */
/* Interrupt flag                                                      */
/* ------------------------------------------------------------------ */

/* Called from inside libavformat's I/O loops. No locks, no JVM entry: it is
 * polled in tight loops, and an AttachCurrentThread round trip per poll would
 * be both slow and deadlock-prone during teardown. */
static int ff_format_interrupt_cb(void *opaque)
{
    const FFInterruptFlag *f = opaque;

    return f && atomic_load_explicit(&f->aborted, memory_order_acquire);
}

int ff_interrupt_raised(const FFInterruptFlag *flag)
{
    return flag && atomic_load_explicit(&flag->aborted, memory_order_acquire);
}

void ff_format_install_interrupt(AVFormatContext *ctx, FFInterruptFlag *flag)
{
    if (!ctx || !flag)
        return;
    ctx->interrupt_callback.callback = ff_format_interrupt_cb;
    ctx->interrupt_callback.opaque   = flag;
}

static void ff_interrupt_finalize(void *p)
{
    av_free(p);
}

int64_t ff_interrupt_create(void)
{
    FFInterruptFlag *f;
    int64_t token;

    f = av_mallocz(sizeof(*f));
    if (!f)
        return 0;
    atomic_init(&f->aborted, 0);

    token = ff_handle_create(FF_HANDLE_TYPE_INTERRUPT, f, ff_interrupt_finalize);
    if (!token) {
        av_free(f);
        return 0;
    }
    return token;
}

int ff_interrupt_abort(int64_t token)
{
    FFInterruptFlag *f;
    int st = 0;

    f = ff_handle_acquire(token, FF_HANDLE_TYPE_INTERRUPT, &st);
    if (!f) {
        /* Already closed, or never ours: the caller's intent is satisfied
         * either way, because nothing can be reading through it any more. */
        return st == FF_HANDLE_ERR_TYPE ? FF_HANDLE_ERR_TYPE : 0;
    }
    /* Note the ordering rule this breaks on purpose: no exec lock is taken,
     * and no wait is performed. A close that waited here would deadlock with
     * the thread it is trying to unblock. */
    atomic_store_explicit(&f->aborted, 1, memory_order_release);
    ff_handle_release(token, FF_HANDLE_TYPE_INTERRUPT);
    return 0;
}

FFInterruptFlag *ff_interrupt_acquire(int64_t token, int *status)
{
    return ff_handle_acquire(token, FF_HANDLE_TYPE_INTERRUPT, status);
}

void ff_interrupt_release(int64_t token)
{
    ff_handle_release(token, FF_HANDLE_TYPE_INTERRUPT);
}

int ff_interrupt_revoke(int64_t token)
{
    return ff_handle_revoke(token, FF_HANDLE_TYPE_INTERRUPT);
}

/* ------------------------------------------------------------------ */
/* Context destruction                                                 */
/* ------------------------------------------------------------------ */

/* Releases a context exactly once, along the path its origin requires. */
static void ff_format_destroy_ctx(AVFormatContext *ctx, FFFormatSource src,
                                  int owns_pb)
{
    if (!ctx)
        return;

    switch (src) {
    case FF_FORMAT_SOURCE_INPUT:
        /* Closes ctx->pb when libavformat opened it, and frees the context. */
        avformat_close_input(&ctx);
        break;

    case FF_FORMAT_SOURCE_OUTPUT:
    case FF_FORMAT_SOURCE_ALLOCATED:
        /* avformat_free_context() does not close the I/O it was given, so a
         * pb this bridge opened is closed here first. */
        if (owns_pb && ctx->pb)
            avio_closep(&ctx->pb);
        avformat_free_context(ctx);
        break;
    }
}

static void ff_format_finalize(void *p)
{
    FFFormatHandle *h = p;
    AVFormatContext *ctx = h->ctx;

    h->ctx = NULL;

    /* Order matters: the callback data must outlive every poll of it, so the
     * flag reference is dropped only after the context is gone. */
    ff_format_destroy_ctx(ctx, h->source, h->owns_pb);

    if (h->flag_token) {
        ff_handle_release(h->flag_token, FF_HANDLE_TYPE_INTERRUPT);
        h->flag_token = 0;
        h->flag = NULL;
    }
    if (h->exec_ready) {
        pthread_mutex_destroy(&h->exec);
        h->exec_ready = 0;
    }
    av_free(h);
}

/* ------------------------------------------------------------------ */
/* Publication                                                         */
/* ------------------------------------------------------------------ */

int64_t ff_format_publish(AVFormatContext *ctx, FFFormatSource src,
                          int64_t flag_token, FFInterruptFlag *flag)
{
    FFFormatHandle *h;
    int64_t token;

    if (!ctx) {
        if (flag_token)
            ff_handle_release(flag_token, FF_HANDLE_TYPE_INTERRUPT);
        return 0;
    }
    if (flag_token && !flag) {
        ff_format_destroy_ctx(ctx, src, 0);
        return 0;
    }

    h = av_mallocz(sizeof(*h));
    if (!h) {
        ff_format_destroy_ctx(ctx, src, 0);
        if (flag_token)
            ff_handle_release(flag_token, FF_HANDLE_TYPE_INTERRUPT);
        return 0;
    }
    if (pthread_mutex_init(&h->exec, NULL) != 0) {
        av_free(h);
        ff_format_destroy_ctx(ctx, src, 0);
        if (flag_token)
            ff_handle_release(flag_token, FF_HANDLE_TYPE_INTERRUPT);
        return 0;
    }

    h->exec_ready = 1;
    h->ctx        = ctx;
    h->source     = src;
    h->flag       = flag;
    h->flag_token = flag_token;

    token = ff_handle_create(FF_HANDLE_TYPE_FORMAT_CONTEXT, h,
                             ff_format_finalize);
    if (!token) {
        /* No slot: the context must not be left half-published. */
        h->ctx = NULL;
        ff_format_destroy_ctx(ctx, src, 0);
        if (flag_token)
            ff_handle_release(flag_token, FF_HANDLE_TYPE_INTERRUPT);
        pthread_mutex_destroy(&h->exec);
        av_free(h);
        return 0;
    }
    return token;
}

int ff_format_mark_pb_owned(int64_t token)
{
    FFFormatOp op;
    AVFormatContext *ctx = ff_format_begin(token, &op);

    if (!ctx)
        return AVERROR(EINVAL);
    op.handle->owns_pb = 1;
    ff_format_end(&op);
    return 0;
}

void ff_format_op_mark_pb_owned(FFFormatOp *op)
{
    if (op && op->handle)
        op->handle->owns_pb = 1;
}

void ff_format_op_clear_pb_owned(FFFormatOp *op)
{
    if (op && op->handle)
        op->handle->owns_pb = 0;
}

int ff_format_attach_interrupt(int64_t token, int64_t flag_token)
{
    FFFormatOp op;
    AVFormatContext *ctx = ff_format_begin(token, &op);
    FFInterruptFlag *f;
    int rc = 0;

    if (!ctx)
        return AVERROR(EINVAL);

    if (op.handle->flag_token == flag_token) {
        rc = 0;                          /* binding the same flag is a no-op */
        goto done;
    }
    if (op.handle->flag_token) {
        /* Swapping would leave libavformat polling a released flag, and the
         * old one may already be inside a protocol's callback data. */
        rc = AVERROR(EPERM);
        goto done;
    }

    f = ff_interrupt_acquire(flag_token, NULL);
    if (!f) {
        rc = AVERROR(EINVAL);
        goto done;
    }
    ff_format_install_interrupt(ctx, f);
    op.handle->flag       = f;
    op.handle->flag_token = flag_token;

done:
    ff_format_end(&op);
    return rc;
}

/* ------------------------------------------------------------------ */
/* Operation guard                                                     */
/* ------------------------------------------------------------------ */

AVFormatContext *ff_format_begin(int64_t token, FFFormatOp *op)
{
    FFFormatHandle *h;
    int st = 0;

    if (!op)
        return NULL;
    op->handle = NULL;
    op->ctx    = NULL;
    op->token  = token;

    h = ff_handle_acquire(token, FF_HANDLE_TYPE_FORMAT_CONTEXT, &st);
    if (!h)
        return NULL;

    pthread_mutex_lock(&h->exec);

    /* Re-check after the lock: a close may have started while this operation
     * was waiting for a context that was still busy. Starting native work now
     * would run against a context whose teardown is already under way. */
    if (!h->ctx || ff_handle_closing(token, FF_HANDLE_TYPE_FORMAT_CONTEXT) > 0) {
        pthread_mutex_unlock(&h->exec);
        ff_handle_release(token, FF_HANDLE_TYPE_FORMAT_CONTEXT);
        return NULL;
    }

    op->handle = h;
    op->ctx    = h->ctx;
    return h->ctx;
}

void ff_format_end(FFFormatOp *op)
{
    if (!op || !op->handle)
        return;
    pthread_mutex_unlock(&op->handle->exec);
    ff_handle_release(op->token, FF_HANDLE_TYPE_FORMAT_CONTEXT);
    op->handle = NULL;
    op->ctx    = NULL;
}

void ff_format_op_cleanup(FFFormatOp *op)
{
    ff_format_end(op);
}

/* ------------------------------------------------------------------ */
/* Close protocol                                                      */
/* ------------------------------------------------------------------ */

int ff_format_request_close(int64_t token, int *in_flight_out)
{
    FFFormatHandle *h;
    int st = 0;

    if (in_flight_out)
        *in_flight_out = 0;

    h = ff_handle_acquire(token, FF_HANDLE_TYPE_FORMAT_CONTEXT, &st);
    if (!h) {
        /* A token of another kind is a caller bug; a stale one means the
         * caller is closing something already closed, which is idempotent. */
        return st == FF_HANDLE_ERR_TYPE ? FF_FORMAT_CLOSE_INVALID
                                        : FF_FORMAT_CLOSE_OK;
    }

    /* 1. No new operation may start. This does not free anything: existing
     *    references keep working, which is what lets an in-flight read finish
     *    instead of touching freed memory. */
    ff_handle_revoke(token, FF_HANDLE_TYPE_FORMAT_CONTEXT);

    /* 2. Wake anything parked in a blocking read. Deliberately without the
     *    exec lock, and without waiting for it. */
    if (h->flag)
        atomic_store_explicit(&h->flag->aborted, 1, memory_order_release);

    if (in_flight_out)
        *in_flight_out = ff_handle_refcount(token, FF_HANDLE_TYPE_FORMAT_CONTEXT);

    /* 3. Drop our own reference. If it was the last one, the context is
     *    destroyed inside this call; otherwise the last operation does it. */
    ff_handle_release(token, FF_HANDLE_TYPE_FORMAT_CONTEXT);
    return FF_FORMAT_CLOSE_OK;
}

int ff_format_await_close(int64_t token, unsigned timeout_ms)
{
    int rc = ff_handle_wait_done(token, FF_HANDLE_TYPE_FORMAT_CONTEXT,
                                 timeout_ms);
    if (rc == FF_HANDLE_ERR_TIMEOUT)
        return FF_FORMAT_CLOSE_UNCONFIRMED;
    if (rc == FF_HANDLE_ERR_TYPE)
        return FF_FORMAT_CLOSE_INVALID;
    return FF_FORMAT_CLOSE_OK;
}

int ff_format_close(int64_t token, unsigned timeout_ms)
{
    int rc = ff_format_request_close(token, NULL);

    if (rc != FF_FORMAT_CLOSE_OK)
        return rc;
    return ff_format_await_close(token, timeout_ms);
}
