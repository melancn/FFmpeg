/*
 * Behaviour contract for ffmpeg_jni_handle_registry.
 *
 * This suite links only the registry, so it runs on the host without FFmpeg,
 * JNI or Android headers. It pins the properties the rest of the bridge relies
 * on:
 *
 *   - a token is never a pointer, and is validated on type, slot and
 *     generation before anything is dereferenced;
 *   - a stale token can never name a newer object that reuses its slot;
 *   - revoking stops new operations but never frees an object that an
 *     operation still holds;
 *   - the finaliser runs exactly once, and only after the last reference;
 *   - a slot is reusable only after the finaliser returned;
 *   - the generation ceiling retires a slot instead of wrapping.
 *
 * Build (host, standalone):
 *   cc -std=c11 -pthread tests/handle_registry_test.c \
 *      ffmpeg_jni_handle_registry.c -o handle_registry_test
 *
 * In the Android suite it is linked into packet_contract_test and driven from
 * its main() through handle_registry_run(), so both ABIs execute the real
 * threaded behaviour on the device.
 */

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../ffmpeg_jni_handle_registry.h"

/* Provided by packet_contract_test.c: the shared pass/fail counters. */
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
/* A stand-in object that counts its own finalisation.                 */
/* ------------------------------------------------------------------ */

typedef struct TestObj {
    int      id;
    int      finalized;
    int      live;
} TestObj;

static int g_finalize_calls;

static void test_finalize(void *p)
{
    TestObj *o = p;

    __atomic_fetch_add(&g_finalize_calls, 1, __ATOMIC_SEQ_CST);
    if (o)
        o->finalized = 1;
}

static void reset(void)
{
    ff_handle_registry_reset_for_tests();
    g_finalize_calls = 0;
}

/* ------------------------------------------------------------------ */
/* 1. Token validation                                                 */
/* ------------------------------------------------------------------ */

static void test_zero_and_garbage_tokens(void)
{
    TestObj o = { 1, 0, 1 };
    int64_t tok;
    int st = 0;

    reset();
    tok = ff_handle_create(FF_HANDLE_TYPE_DICTIONARY, &o, test_finalize);
    CHECK(tok > 0);

    /* 0 is the reserved "no handle" value. */
    CHECK(ff_handle_acquire(0, FF_HANDLE_TYPE_DICTIONARY, NULL) == NULL);
    CHECK_EQ(ff_handle_release(0, FF_HANDLE_TYPE_DICTIONARY),
             FF_HANDLE_ERR_TOKEN);

    /* Negative values, including one with the top bit set, are not tokens. */
    CHECK(ff_handle_acquire(-1, FF_HANDLE_TYPE_DICTIONARY, NULL) == NULL);
    CHECK(ff_handle_acquire((int64_t)0x8000000000000001LL,
                            FF_HANDLE_TYPE_DICTIONARY, NULL) == NULL);

    /* A token with a valid shape but a slot index beyond the table. */
    CHECK(ff_handle_acquire((int64_t)((1LL << 32) | (2LL << 16) | 5000LL),
                            FF_HANDLE_TYPE_DICTIONARY, NULL) == NULL);

    /* Type field 0 is never issued. */
    CHECK(ff_handle_acquire((int64_t)((1LL << 32) | 1LL),
                            FF_HANDLE_TYPE_DICTIONARY, NULL) == NULL);

    /* Generation field 0 is never issued. */
    CHECK(ff_handle_acquire((int64_t)((2LL << 16) | 1LL),
                            FF_HANDLE_TYPE_DICTIONARY, NULL) == NULL);

    CHECK(ff_handle_release(tok, FF_HANDLE_TYPE_DICTIONARY) == FF_HANDLE_OK);
    CHECK_EQ(g_finalize_calls, 1);
}

static void test_type_mismatch_is_rejected(void)
{
    TestObj o = { 1, 0, 1 };
    int64_t tok;
    int st = 0;

    reset();
    tok = ff_handle_create(FF_HANDLE_TYPE_DICTIONARY, &o, test_finalize);
    CHECK(tok > 0);

    /* Right slot and generation, wrong declared kind: never a pointer
     * fallback, always a rejection. */
    CHECK(ff_handle_acquire(tok, FF_HANDLE_TYPE_FORMAT_CONTEXT, &st) == NULL);
    CHECK_EQ(st, FF_HANDLE_ERR_TYPE);
    CHECK(ff_handle_acquire(tok, FF_HANDLE_TYPE_DICTIONARY, &st) == &o);
    CHECK_EQ(st, FF_HANDLE_OK);
    CHECK(ff_handle_release(tok, FF_HANDLE_TYPE_DICTIONARY) == FF_HANDLE_OK);

    CHECK(ff_handle_release(tok, FF_HANDLE_TYPE_DICTIONARY) == FF_HANDLE_OK);
    CHECK_EQ(g_finalize_calls, 1);
}

/* ------------------------------------------------------------------ */
/* 2. References, revoke and the close protocol                        */
/* ------------------------------------------------------------------ */

static void test_revoke_does_not_free_under_a_running_operation(void)
{
    TestObj o = { 1, 0, 1 };
    TestObj o2 = { 2, 0, 1 };
    int64_t tok;
    int st = 0;
    int64_t second;

    reset();
    tok = ff_handle_create(FF_HANDLE_TYPE_FORMAT_CONTEXT, &o, test_finalize);
    CHECK(tok > 0);

    /* An operation is inside. */
    CHECK(ff_handle_acquire(tok, FF_HANDLE_TYPE_FORMAT_CONTEXT, &st) == &o);
    CHECK_EQ(st, FF_HANDLE_OK);

    /* Close begins: no new operation may start... */
    CHECK(ff_handle_revoke(tok, FF_HANDLE_TYPE_FORMAT_CONTEXT) == FF_HANDLE_OK);
    CHECK(ff_handle_acquire(tok, FF_HANDLE_TYPE_FORMAT_CONTEXT, &st) == NULL);
    CHECK_EQ(st, FF_HANDLE_ERR_CLOSING);
    CHECK_EQ(ff_handle_closing(tok, FF_HANDLE_TYPE_FORMAT_CONTEXT), 1);

    /* ...but the object is still there for the admitted one. */
    CHECK_EQ(g_finalize_calls, 0);
    CHECK_EQ(o.finalized, 0);
    CHECK(o.live == 1);

    /* It is not finalised, so the slot is not reusable yet. */
    second = ff_handle_create(FF_HANDLE_TYPE_FORMAT_CONTEXT, &o2, test_finalize);
    CHECK(second > 0);
    CHECK(second != tok);
    CHECK(ff_handle_release(second, FF_HANDLE_TYPE_FORMAT_CONTEXT) ==
          FF_HANDLE_OK);
    CHECK_EQ(o2.finalized, 1);

    /* The admitted operation leaves: now it is finalised, exactly once. */
    CHECK(ff_handle_release(tok, FF_HANDLE_TYPE_FORMAT_CONTEXT) == FF_HANDLE_OK);
    CHECK_EQ(o.finalized, 1);
    CHECK_EQ(g_finalize_calls, 2);

    /* And the close is complete, not merely requested. */
    CHECK_EQ(ff_handle_wait_done(tok, FF_HANDLE_TYPE_FORMAT_CONTEXT, 100),
             FF_HANDLE_OK);
}

static void test_double_revoke_finalises_once(void)
{
    TestObj o = { 1, 0, 1 };
    int64_t tok;

    reset();
    tok = ff_handle_create(FF_HANDLE_TYPE_INTERRUPT, &o, test_finalize);
    CHECK(tok > 0);
    CHECK(ff_handle_revoke(tok, FF_HANDLE_TYPE_INTERRUPT) == FF_HANDLE_OK);
    CHECK_EQ(g_finalize_calls, 1);
    CHECK(ff_handle_revoke(tok, FF_HANDLE_TYPE_INTERRUPT) == FF_HANDLE_OK);
    CHECK_EQ(g_finalize_calls, 1);
    CHECK(ff_handle_release(tok, FF_HANDLE_TYPE_INTERRUPT) != FF_HANDLE_OK);
    CHECK_EQ(g_finalize_calls, 1);
}

static void test_slot_reuse_never_revives_an_old_token(void)
{
    TestObj a = { 1, 0, 1 };
    TestObj b = { 2, 0, 1 };
    int64_t tok_a, tok_b;
    int slot_a = -1, slot_b = -1;
    uint32_t gen_a = 0, gen_b = 0;

    reset();
    tok_a = ff_handle_create(FF_HANDLE_TYPE_DICTIONARY, &a, test_finalize);
    CHECK_EQ(ff_handle_inspect(tok_a, NULL, &slot_a, &gen_a), FF_HANDLE_OK);
    CHECK(ff_handle_release(tok_a, FF_HANDLE_TYPE_DICTIONARY) == FF_HANDLE_OK);
    CHECK_EQ(g_finalize_calls, 1);

    /* The freed slot is normally the first free one again. */
    tok_b = ff_handle_create(FF_HANDLE_TYPE_DICTIONARY, &b, test_finalize);
    CHECK_EQ(ff_handle_inspect(tok_b, NULL, &slot_b, &gen_b), FF_HANDLE_OK);
    CHECK_EQ(slot_b, slot_a);
    CHECK(gen_b != gen_a);
    CHECK(tok_b != tok_a);

    /* The old token must not reach the new object. */
    CHECK(ff_handle_acquire(tok_a, FF_HANDLE_TYPE_DICTIONARY, NULL) == NULL);
    CHECK(ff_handle_release(tok_a, FF_HANDLE_TYPE_DICTIONARY) != FF_HANDLE_OK);
    CHECK(ff_handle_acquire(tok_b, FF_HANDLE_TYPE_DICTIONARY, NULL) == &b);

    CHECK(ff_handle_release(tok_b, FF_HANDLE_TYPE_DICTIONARY) == FF_HANDLE_OK);
    CHECK(ff_handle_release(tok_b, FF_HANDLE_TYPE_DICTIONARY) == FF_HANDLE_OK);
    CHECK_EQ(g_finalize_calls, 2);
    CHECK_EQ(a.finalized, 1);
    CHECK_EQ(b.finalized, 1);
}

/* ------------------------------------------------------------------ */
/* 3. Exhaustion and the generation ceiling                            */
/* ------------------------------------------------------------------ */

static void test_exhaustion_and_recovery(void)
{
    TestObj objs[FF_HANDLE_MAX_SLOTS];
    int64_t toks[FF_HANDLE_MAX_SLOTS];
    int i, made = 0;

    reset();
    memset(objs, 0, sizeof(objs));
    for (i = 0; i < FF_HANDLE_MAX_SLOTS; i++) {
        objs[i].id = i;
        toks[i] = ff_handle_create(FF_HANDLE_TYPE_DICT_CURSOR, &objs[i],
                                   test_finalize);
        if (toks[i] > 0)
            made++;
    }
    CHECK_EQ(made, FF_HANDLE_MAX_SLOTS);

    /* Full: refused, not handed a bogus slot. */
    CHECK_EQ(ff_handle_create(FF_HANDLE_TYPE_DICT_CURSOR, &objs[0],
                              test_finalize), 0);

    for (i = 0; i < FF_HANDLE_MAX_SLOTS; i++)
        CHECK(ff_handle_release(toks[i], FF_HANDLE_TYPE_DICT_CURSOR) ==
              FF_HANDLE_OK);
    CHECK_EQ(g_finalize_calls, FF_HANDLE_MAX_SLOTS);

    /* Everything is reusable again after the finalisers ran. */
    for (i = 0; i < FF_HANDLE_MAX_SLOTS; i++) {
        toks[i] = ff_handle_create(FF_HANDLE_TYPE_DICT_CURSOR, &objs[i],
                                   test_finalize);
        CHECK(toks[i] > 0);
    }
    for (i = 0; i < FF_HANDLE_MAX_SLOTS; i++)
        ff_handle_release(toks[i], FF_HANDLE_TYPE_DICT_CURSOR);
    CHECK_EQ(g_finalize_calls, FF_HANDLE_MAX_SLOTS * 2);
}

static void test_generation_ceiling_retires_the_slot(void)
{
    TestObj o = { 1, 0, 1 };
    int64_t tok, tok_max, probe;
    int slot_max = -1, slot_probe = -1;
    uint32_t gen = 0;
    int i;

    reset();
    tok = ff_handle_create(FF_HANDLE_TYPE_RENDERER, &o, test_finalize);
    CHECK(tok > 0);

    CHECK_EQ(ff_handle_test_set_gen(tok, FF_HANDLE_TYPE_RENDERER,
                                    FF_HANDLE_GEN_MAX, &tok_max),
             FF_HANDLE_OK);
    CHECK(tok_max > 0);
    CHECK_EQ(ff_handle_inspect(tok_max, NULL, &slot_max, &gen), FF_HANDLE_OK);
    CHECK_EQ(gen, FF_HANDLE_GEN_MAX);

    /* The re-issued token still round-trips, so it can be released... */
    CHECK(ff_handle_acquire(tok_max, FF_HANDLE_TYPE_RENDERER, NULL) == &o);
    CHECK(ff_handle_release(tok_max, FF_HANDLE_TYPE_RENDERER) == FF_HANDLE_OK);
    CHECK(ff_handle_release(tok_max, FF_HANDLE_TYPE_RENDERER) == FF_HANDLE_OK);
    CHECK_EQ(g_finalize_calls, 1);

    /* ...and that drain retires the slot instead of wrapping its generation. */
    CHECK(ff_handle_acquire(tok_max, FF_HANDLE_TYPE_RENDERER, NULL) == NULL);
    for (i = 0; i < 8; i++) {
        probe = ff_handle_create(FF_HANDLE_TYPE_RENDERER, &o, test_finalize);
        CHECK(probe > 0);
        CHECK_EQ(ff_handle_inspect(probe, NULL, &slot_probe, &gen),
                 FF_HANDLE_OK);
        CHECK(slot_probe != slot_max);
        ff_handle_release(probe, FF_HANDLE_TYPE_RENDERER);
    }
}

/* Large generations must survive the round trip intact. With the old
 * (intptr_t)gen << 16 encoding this failed on 32-bit ABIs: the token went
 * negative once the generation reached 0x8000, and every lookup was refused. */
static void test_large_generation_round_trips(void)
{
    TestObj o = { 1, 0, 1 };
    int64_t tok, tok_hi;
    uint32_t gen = 0;

    reset();
    tok = ff_handle_create(FF_HANDLE_TYPE_RENDERER, &o, test_finalize);
    CHECK(tok > 0);

    CHECK_EQ(ff_handle_test_set_gen(tok, FF_HANDLE_TYPE_RENDERER, 0x8000u,
                                    &tok_hi), FF_HANDLE_OK);
    CHECK(tok_hi > 0);
    CHECK_EQ(ff_handle_inspect(tok_hi, NULL, NULL, &gen), FF_HANDLE_OK);
    CHECK_EQ(gen, 0x8000u);
    CHECK(ff_handle_acquire(tok_hi, FF_HANDLE_TYPE_RENDERER, NULL) == &o);
    CHECK(ff_handle_release(tok_hi, FF_HANDLE_TYPE_RENDERER) == FF_HANDLE_OK);
    CHECK(ff_handle_release(tok_hi, FF_HANDLE_TYPE_RENDERER) == FF_HANDLE_OK);
    CHECK_EQ(g_finalize_calls, 1);
}

/* ------------------------------------------------------------------ */
/* 4. Quarantine                                                       */
/* ------------------------------------------------------------------ */

static void test_quarantine_retains_object_and_slot(void)
{
    TestObj o = { 1, 0, 1 };
    int64_t tok, probe;
    int slot_q = -1, slot_probe = -1;
    int i;

    reset();
    tok = ff_handle_create(FF_HANDLE_TYPE_RENDERER, &o, test_finalize);
    CHECK(tok > 0);
    CHECK(ff_handle_acquire(tok, FF_HANDLE_TYPE_RENDERER, NULL) == &o);

    /* Teardown could not be proven: the object is kept, not released. */
    CHECK(ff_handle_quarantine(tok, FF_HANDLE_TYPE_RENDERER) == FF_HANDLE_OK);
    CHECK(ff_handle_revoke(tok, FF_HANDLE_TYPE_RENDERER) == FF_HANDLE_OK);
    CHECK(ff_handle_release(tok, FF_HANDLE_TYPE_RENDERER) == FF_HANDLE_OK);
    CHECK_EQ(g_finalize_calls, 0);
    CHECK_EQ(o.finalized, 0);

    /* Its slot must never be handed out again. */
    CHECK_EQ(ff_handle_inspect(tok, NULL, &slot_q, NULL), FF_HANDLE_OK);
    for (i = 0; i < 8; i++) {
        probe = ff_handle_create(FF_HANDLE_TYPE_RENDERER, &o, test_finalize);
        CHECK(probe > 0);
        CHECK_EQ(ff_handle_inspect(probe, NULL, &slot_probe, NULL),
                 FF_HANDLE_OK);
        CHECK(slot_probe != slot_q);
        ff_handle_release(probe, FF_HANDLE_TYPE_RENDERER);
    }
    CHECK_EQ(g_finalize_calls, 8);
    CHECK_EQ(o.finalized, 0);
}

/* ------------------------------------------------------------------ */
/* 5. Concurrent close                                                 */
/* ------------------------------------------------------------------ */

typedef struct RevokeCtx {
    int64_t tok;
    int     ok;
    int     rc;
} RevokeCtx;

static void *revoke_thread(void *arg)
{
    RevokeCtx *c = arg;

    c->rc = ff_handle_revoke(c->tok, FF_HANDLE_TYPE_FORMAT_CONTEXT);
    c->ok = (c->rc == FF_HANDLE_OK);
    return NULL;
}

static void test_concurrent_revoke_finalises_once(void)
{
    enum { THREADS = 4, ROUNDS = 2000 };
    int round;

    reset();
    for (round = 0; round < ROUNDS; round++) {
        TestObj o;
        int64_t tok;
        pthread_t th[THREADS];
        RevokeCtx ctx[THREADS];
        int i;

        o.id = round;
        o.finalized = 0;
        o.live = 1;

        tok = ff_handle_create(FF_HANDLE_TYPE_FORMAT_CONTEXT, &o, test_finalize);
        CHECK(tok > 0);
        /* Hold one operation reference across the race, like a live read. */
        CHECK(ff_handle_acquire(tok, FF_HANDLE_TYPE_FORMAT_CONTEXT, NULL) == &o);

        for (i = 0; i < THREADS; i++) {
            ctx[i].tok = tok;
            ctx[i].ok = 0;
            ctx[i].rc = 0;
            pthread_create(&th[i], NULL, revoke_thread, &ctx[i]);
        }
        for (i = 0; i < THREADS; i++) {
            pthread_join(th[i], NULL);
            CHECK_EQ(ctx[i].rc, FF_HANDLE_OK);
        }
        /* Still alive: the operation reference is outstanding. */
        CHECK_EQ(o.finalized, 0);

        CHECK(ff_handle_release(tok, FF_HANDLE_TYPE_FORMAT_CONTEXT) ==
              FF_HANDLE_OK);
        CHECK_EQ(o.finalized, 1);
    }
    CHECK_EQ(g_finalize_calls, ROUNDS);
}

/* Two threads dropping the creator reference and the last operation reference
 * at the same time must still finalise exactly once. */
static void *release_thread(void *arg)
{
    RevokeCtx *c = arg;

    c->rc = ff_handle_release(c->tok, FF_HANDLE_TYPE_DICTIONARY);
    c->ok = (c->rc == FF_HANDLE_OK);
    return NULL;
}

static void test_concurrent_release_finalises_once(void)
{
    enum { ROUNDS = 2000 };
    int round;

    reset();
    for (round = 0; round < ROUNDS; round++) {
        TestObj o;
        int64_t tok;
        pthread_t th[2];
        RevokeCtx ctx[2];

        o.id = round;
        o.finalized = 0;
        o.live = 1;

        tok = ff_handle_create(FF_HANDLE_TYPE_DICTIONARY, &o, test_finalize);
        CHECK(tok > 0);
        CHECK(ff_handle_acquire(tok, FF_HANDLE_TYPE_DICTIONARY, NULL) == &o);

        ctx[0].tok = tok;
        ctx[1].tok = tok;
        pthread_create(&th[0], NULL, release_thread, &ctx[0]);
        pthread_create(&th[1], NULL, release_thread, &ctx[1]);
        pthread_join(th[0], NULL);
        pthread_join(th[1], NULL);

        /* Exactly one release takes the count to zero and finalises; the other
         * either preceded it (OK) or found the slot already retired (STALE).
         * Both are correct, and neither may finalise a second time. */
        CHECK(ctx[0].rc == FF_HANDLE_OK || ctx[0].rc == FF_HANDLE_ERR_STALE);
        CHECK(ctx[1].rc == FF_HANDLE_OK || ctx[1].rc == FF_HANDLE_ERR_STALE);
        CHECK(ctx[0].rc == FF_HANDLE_OK || ctx[1].rc == FF_HANDLE_OK);
        CHECK_EQ(o.finalized, 1);
    }
    CHECK_EQ(g_finalize_calls, ROUNDS);
}

/* The slot must not be reusable while a finaliser is still running. */
typedef struct SlowFinalize {
    TestObj  obj;
    volatile int entered;
    volatile int may_finish;
} SlowFinalize;

static void slow_finalize(void *p)
{
    SlowFinalize *s = p;

    __atomic_store_n(&s->entered, 1, __ATOMIC_SEQ_CST);
    while (!__atomic_load_n(&s->may_finish, __ATOMIC_SEQ_CST))
        ;                                   /* spin: test-only */
    s->obj.finalized = 1;
    __atomic_fetch_add(&g_finalize_calls, 1, __ATOMIC_SEQ_CST);
}

typedef struct ReleaseArg {
    int64_t tok;
} ReleaseArg;

static void *slow_release_thread(void *arg)
{
    ReleaseArg *a = arg;

    ff_handle_release(a->tok, FF_HANDLE_TYPE_RENDERER);
    return NULL;
}

static void test_slot_not_reused_during_finalize(void)
{
    static SlowFinalize s;
    int64_t tok, probe;
    int slot_a = -1, slot_b = -1;
    pthread_t th;
    ReleaseArg arg;

    reset();
    s.obj.id = 1;
    s.obj.finalized = 0;
    s.entered = 0;
    s.may_finish = 0;

    tok = ff_handle_create(FF_HANDLE_TYPE_RENDERER, &s, slow_finalize);
    CHECK(tok > 0);
    CHECK_EQ(ff_handle_inspect(tok, NULL, &slot_a, NULL), FF_HANDLE_OK);

    arg.tok = tok;
    pthread_create(&th, NULL, slow_release_thread, &arg);
    while (!__atomic_load_n(&s.entered, __ATOMIC_SEQ_CST))
        ;                                   /* wait for the finaliser */

    /* The slot is mid-destruction: a create must not hand it out. */
    probe = ff_handle_create(FF_HANDLE_TYPE_RENDERER, &s.obj, test_finalize);
    CHECK(probe > 0);
    CHECK_EQ(ff_handle_inspect(probe, NULL, &slot_b, NULL), FF_HANDLE_OK);
    CHECK(slot_b != slot_a);

    __atomic_store_n(&s.may_finish, 1, __ATOMIC_SEQ_CST);
    pthread_join(th, NULL);
    CHECK_EQ(s.obj.finalized, 1);
    CHECK_EQ(g_finalize_calls, 1);

    /* And wait_done reports the close as complete. */
    CHECK_EQ(ff_handle_wait_done(tok, FF_HANDLE_TYPE_RENDERER, 200),
             FF_HANDLE_OK);
}

void handle_registry_run(void)
{
    test_zero_and_garbage_tokens();
    test_type_mismatch_is_rejected();
    test_revoke_does_not_free_under_a_running_operation();
    test_double_revoke_finalises_once();
    test_slot_reuse_never_revives_an_old_token();
    test_exhaustion_and_recovery();
    test_generation_ceiling_retires_the_slot();
    test_large_generation_round_trips();
    test_quarantine_retains_object_and_slot();
    test_concurrent_revoke_finalises_once();
    test_concurrent_release_finalises_once();
    test_slot_not_reused_during_finalize();

    printf("handle registry: %d checks, %d failures\n", g_checks, g_failures);
    fftest_add_checks(g_checks, g_failures);
}
