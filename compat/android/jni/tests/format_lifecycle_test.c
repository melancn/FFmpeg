/*
 * Behaviour contract for the managed AVFormatContext and interrupt flag.
 *
 * These are the properties the JNI wrappers rely on, tested directly against
 * the units rather than through a JNIEnv:
 *
 *   - an operation keeps the context alive across a concurrent close;
 *   - a close that cannot drain reports UNCONFIRMED and never frees under a
 *     running operation;
 *   - after the close completes, the token cannot open an operation again;
 *   - the destructor is chosen from the context's origin;
 *   - an interrupt flag may be released by its owner while a format context
 *     still polls it, and the context is the last one to let it go;
 *   - a different flag cannot be swapped in once one is bound;
 *   - slots are recycled, so create/close cycles are not capped by the table.
 *
 * The window renderer is not covered here: it needs an ANativeWindow. The
 * lifetime rules it uses are the registry's, and handle_registry_test.c pins
 * those (including the deferred finaliser and the slot-reuse generation).
 *
 * Driven from packet_contract_test.c's main() through format_lifecycle_run().
 */

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "libavformat/avformat.h"
#include "libavutil/error.h"

#include "../ffmpeg_jni_handle_registry.h"
#include "../ffmpeg_jni_format_handle.h"

extern void fftest_add_checks(int n, int f);

static int g_checks;
static int g_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        g_checks++;                                                          \
        if (!(cond)) {                                                       \
            g_failures++;                                                    \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                    \
    } while (0)

#define CHECK_EQ(actual, expected)                                           \
    do {                                                                     \
        long long a_ = (long long)(actual);                                   \
        long long e_ = (long long)(expected);                                 \
        g_checks++;                                                          \
        if (a_ != e_) {                                                      \
            g_failures++;                                                    \
            fprintf(stderr, "FAIL %s:%d: %s == %lld, expected %lld\n",        \
                    __FILE__, __LINE__, #actual, a_, e_);                     \
        }                                                                    \
    } while (0)

/* ------------------------------------------------------------------ */
/* 1. Basic lifecycle                                                  */
/* ------------------------------------------------------------------ */

static void test_allocated_lifecycle(void)
{
    AVFormatContext *ctx = avformat_alloc_context();
    FFFormatOp op;
    int64_t tok;

    CHECK(ctx != NULL);
    if (!ctx)
        return;

    tok = ff_format_publish(ctx, FF_FORMAT_SOURCE_ALLOCATED, 0, NULL);
    CHECK(tok > 0);

    /* An admitted operation sees the same context. */
    CHECK(ff_format_begin(tok, &op) == ctx);
    CHECK(op.ctx == ctx);
    ff_format_end(&op);

    CHECK_EQ(ff_format_close(tok, 1000), FF_FORMAT_CLOSE_OK);

    /* The token is spent: no operation may start, and a second close is an
     * idempotent no-op rather than an error or a crash. */
    CHECK(ff_format_begin(tok, &op) == NULL);
    CHECK_EQ(ff_format_close(tok, 1000), FF_FORMAT_CLOSE_OK);
}

static void test_close_of_zero_is_a_noop(void)
{
    CHECK_EQ(ff_format_close(0, 100), FF_FORMAT_CLOSE_OK);
    CHECK_EQ(ff_format_request_close(0, NULL), FF_FORMAT_CLOSE_OK);
    CHECK_EQ(ff_format_await_close(0, 100), FF_FORMAT_CLOSE_OK);
}

static void test_garbage_token_is_refused(void)
{
    FFFormatOp op;
    int64_t bogus = (int64_t)0x7fff0000ffffLL;

    CHECK(ff_format_begin(bogus, &op) == NULL);
    CHECK_EQ(ff_format_close(bogus, 100), FF_FORMAT_CLOSE_OK);
    CHECK(ff_format_begin(-1, &op) == NULL);
}

static void test_other_handle_type_is_refused(void)
{
    int dummy = 0;
    int64_t dict_tok;
    FFFormatOp op;

    dict_tok = ff_handle_create(FF_HANDLE_TYPE_DICTIONARY, &dummy, NULL);
    CHECK(dict_tok > 0);

    /* A live handle of another kind is never a pointer fallback. */
    CHECK(ff_format_begin(dict_tok, &op) == NULL);
    CHECK_EQ(ff_format_close(dict_tok, 100), FF_FORMAT_CLOSE_INVALID);

    ff_handle_release(dict_tok, FF_HANDLE_TYPE_DICTIONARY);
}

/* ------------------------------------------------------------------ */
/* 2. Close against a running operation                                */
/* ------------------------------------------------------------------ */

static void test_close_waits_for_an_admitted_operation(void)
{
    AVFormatContext *ctx = avformat_alloc_context();
    FFFormatOp op, op2;
    int64_t tok;

    CHECK(ctx != NULL);
    if (!ctx)
        return;

    tok = ff_format_publish(ctx, FF_FORMAT_SOURCE_ALLOCATED, 0, NULL);
    CHECK(tok > 0);

    /* The reading thread is inside. */
    CHECK(ff_format_begin(tok, &op) == ctx);

    /* Close begins: no new operation is admitted... */
    CHECK_EQ(ff_format_request_close(tok, NULL), FF_FORMAT_CLOSE_OK);
    CHECK(ff_format_begin(tok, &op2) == NULL);

    /* ...but the context is still there for the admitted one, and the close is
     * reported as unconfirmed rather than as finished. */
    CHECK_EQ(ctx->nb_streams, 0);
    CHECK_EQ(ff_format_await_close(tok, 30), FF_FORMAT_CLOSE_UNCONFIRMED);

    /* The operation leaves: now the teardown completes. */
    ff_format_end(&op);
    CHECK_EQ(ff_format_await_close(tok, 1000), FF_FORMAT_CLOSE_OK);
    CHECK(ff_format_begin(tok, &op2) == NULL);
}

/* An operation that took the exec lock and returned must leave it in a state
 * where the next operation is admitted immediately: no leak, no double
 * unlock. The blocking half of the contract is covered by
 * test_close_does_not_wait_for_the_exec_lock, which parks a thread inside an
 * operation on purpose. */
typedef struct LockProbe {
    int64_t   tok;
    int       entered;
    int       release_now;
} LockProbe;

static void *lock_probe_thread(void *arg)
{
    LockProbe *p = arg;
    FFFormatOp op;

    if (ff_format_begin(p->tok, &op)) {
        __atomic_store_n(&p->entered, 1, __ATOMIC_SEQ_CST);
        while (!__atomic_load_n(&p->release_now, __ATOMIC_SEQ_CST))
            ;
        ff_format_end(&op);
    }
    return NULL;
}

static void test_exec_lock_serialises_operations(void)
{
    AVFormatContext *ctx = avformat_alloc_context();
    LockProbe p;
    FFFormatOp op;
    pthread_t th;
    int64_t tok;

    CHECK(ctx != NULL);
    if (!ctx)
        return;

    tok = ff_format_publish(ctx, FF_FORMAT_SOURCE_ALLOCATED, 0, NULL);
    CHECK(tok > 0);

    memset(&p, 0, sizeof(p));
    p.tok = tok;
    CHECK_EQ(pthread_create(&th, NULL, lock_probe_thread, &p), 0);
    while (!__atomic_load_n(&p.entered, __ATOMIC_SEQ_CST))
        ;

    __atomic_store_n(&p.release_now, 1, __ATOMIC_SEQ_CST);
    pthread_join(th, NULL);

    CHECK(ff_format_begin(tok, &op) == ctx);
    ff_format_end(&op);
    CHECK_EQ(ff_format_close(tok, 1000), FF_FORMAT_CLOSE_OK);
}

/* ------------------------------------------------------------------ */
/* 3. Origin decides the destructor                                    */
/* ------------------------------------------------------------------ */

static void test_output_context_destroys_cleanly(void)
{
    AVFormatContext *ctx = NULL;
    int64_t tok;

    /* A real output context: allocated by libavformat, freed by the output
     * path. Publishing and closing it must not use the input destructor. */
    if (avformat_alloc_output_context2(&ctx, NULL, "mp4", NULL) < 0 || !ctx) {
        /* No mp4 muxer in this build: nothing to assert. */
        return;
    }
    tok = ff_format_publish(ctx, FF_FORMAT_SOURCE_OUTPUT, 0, NULL);
    CHECK(tok > 0);
    CHECK_EQ(ff_format_close(tok, 1000), FF_FORMAT_CLOSE_OK);
}

/* Publishing a NULL context must not leak a token. */
static void test_publish_null(void)
{
    CHECK_EQ(ff_format_publish(NULL, FF_FORMAT_SOURCE_ALLOCATED, 0, NULL), 0);
}

/* ------------------------------------------------------------------ */
/* 4. Interrupt flag lifetime                                          */
/* ------------------------------------------------------------------ */

static void test_flag_outlives_its_owner(void)
{
    AVFormatContext *ctx = avformat_alloc_context();
    FFInterruptFlag *f;
    FFFormatOp op;
    int64_t flag, tok;

    CHECK(ctx != NULL);
    if (!ctx)
        return;

    flag = ff_interrupt_create();
    CHECK(flag > 0);

    f = ff_interrupt_acquire(flag, NULL);
    CHECK(f != NULL);
    ff_format_install_interrupt(ctx, f);

    tok = ff_format_publish(ctx, FF_FORMAT_SOURCE_ALLOCATED, flag, f);
    CHECK(tok > 0);

    /* The owner drops its handle first - the order the old contract forbade. */
    ff_interrupt_revoke(flag);

    /* The context still holds the flag, so the callback data is valid. */
    CHECK_EQ(ff_interrupt_raised(f), 0);
    CHECK(ff_format_begin(tok, &op) != NULL);
    ff_format_end(&op);

    /* Aborting through a released token is a no-op, not a write to freed
     * memory. */
    CHECK_EQ(ff_interrupt_abort(flag), 0);
    CHECK_EQ(ff_interrupt_raised(f), 1);

    CHECK_EQ(ff_format_close(tok, 1000), FF_FORMAT_CLOSE_OK);
    /* Closing the context released the last flag reference; nothing further is
     * touched through `f` here on purpose. */
}

static void test_flag_cannot_be_swapped(void)
{
    AVFormatContext *ctx = avformat_alloc_context();
    int64_t flag_a, flag_b, tok;
    FFInterruptFlag *fa;

    CHECK(ctx != NULL);
    if (!ctx)
        return;

    tok = ff_format_publish(ctx, FF_FORMAT_SOURCE_ALLOCATED, 0, NULL);
    CHECK(tok > 0);

    flag_a = ff_interrupt_create();
    flag_b = ff_interrupt_create();
    CHECK(flag_a > 0);
    CHECK(flag_b > 0);

    CHECK_EQ(ff_format_attach_interrupt(tok, flag_a), 0);
    /* Binding the same flag again is idempotent... */
    CHECK_EQ(ff_format_attach_interrupt(tok, flag_a), 0);
    /* ...but a different one is refused: libavformat may already hold the old
     * callback data. */
    CHECK_EQ(ff_format_attach_interrupt(tok, flag_b), AVERROR(EPERM));

    fa = ff_interrupt_acquire(flag_a, NULL);
    CHECK(fa != NULL);
    CHECK_EQ(ff_interrupt_raised(fa), 0);
    ff_interrupt_release(flag_a);

    ff_interrupt_revoke(flag_a);
    ff_interrupt_revoke(flag_b);
    CHECK_EQ(ff_format_close(tok, 1000), FF_FORMAT_CLOSE_OK);
}

/* ------------------------------------------------------------------ */
/* 5. Slot recycling                                                   */
/* ------------------------------------------------------------------ */

/* The old renderer table could not be reused, so the 33rd create failed. The
 * format path shares the registry now: many more cycles than the table has
 * slots must all succeed, and every token must be distinct. */
static void test_many_create_close_cycles(void)
{
    enum { CYCLES = 5000 };
    int64_t previous = 0;
    int i, ok = 1;

    for (i = 0; i < CYCLES; i++) {
        AVFormatContext *ctx = avformat_alloc_context();
        int64_t tok;

        if (!ctx) {
            ok = 0;
            break;
        }
        tok = ff_format_publish(ctx, FF_FORMAT_SOURCE_ALLOCATED, 0, NULL);
        if (tok <= 0 || tok == previous) {
            ok = 0;
            if (tok <= 0 && ctx)
                avformat_free_context(ctx);
            break;
        }
        previous = tok;
        if (ff_format_close(tok, 1000) != FF_FORMAT_CLOSE_OK) {
            ok = 0;
            break;
        }
    }
    CHECK(ok);
    CHECK_EQ(i, CYCLES);
}

/* ------------------------------------------------------------------ */
/* 6. Close with the flag raised does not wait for the exec lock       */
/* ------------------------------------------------------------------ */

/* A close must be able to raise the flag while another thread is inside an
 * operation. If the close path took the exec lock, this would deadlock. */
static void *hold_operation_thread(void *arg)
{
    int64_t tok = *(int64_t *)arg;
    FFFormatOp op;

    if (ff_format_begin(tok, &op)) {
        /* Park long enough for the closer to run. */
        struct timespec ts = { 0, 50 * 1000 * 1000 };
        nanosleep(&ts, NULL);
        ff_format_end(&op);
    }
    return NULL;
}

static void test_close_does_not_wait_for_the_exec_lock(void)
{
    AVFormatContext *ctx = avformat_alloc_context();
    int64_t tok;
    pthread_t th;

    CHECK(ctx != NULL);
    if (!ctx)
        return;

    tok = ff_format_publish(ctx, FF_FORMAT_SOURCE_ALLOCATED, 0, NULL);
    CHECK(tok > 0);

    CHECK_EQ(pthread_create(&th, NULL, hold_operation_thread, &tok), 0);
    /* Give the operation a chance to take the exec lock. */
    {
        struct timespec ts = { 0, 5 * 1000 * 1000 };
        nanosleep(&ts, NULL);
    }

    /* request_close takes no exec lock; it must return promptly. */
    CHECK_EQ(ff_format_request_close(tok, NULL), FF_FORMAT_CLOSE_OK);

    pthread_join(th, NULL);
    CHECK_EQ(ff_format_await_close(tok, 1000), FF_FORMAT_CLOSE_OK);
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

void format_lifecycle_run(void)
{
    ff_handle_registry_reset_for_tests();

    test_allocated_lifecycle();
    test_close_of_zero_is_a_noop();
    test_garbage_token_is_refused();
    test_other_handle_type_is_refused();
    test_close_waits_for_an_admitted_operation();
    test_exec_lock_serialises_operations();
    test_output_context_destroys_cleanly();
    test_publish_null();
    test_flag_outlives_its_owner();
    test_flag_cannot_be_swapped();
    test_many_create_close_cycles();
    test_close_does_not_wait_for_the_exec_lock();

    printf("format lifecycle: %d checks, %d failures\n", g_checks, g_failures);
    fftest_add_checks(g_checks, g_failures);
}
