/*
 * Controlled handle registry - implementation. See the header for the token
 * layout and the ownership rules; this file only enforces them.
 *
 * Locking discipline:
 *   - g_lock guards every slot field.
 *   - Finalisers run with g_lock released. They close FFmpeg contexts, release
 *     platform windows and destroy mutexes: none of that may happen while
 *     another thread waits to acquire or release a handle.
 */

#include "ffmpeg_jni_handle_registry.h"

#include <pthread.h>
#include <string.h>
#include <time.h>

enum {
    FF_SLOT_FREE = 0,     /* reusable; gen stays as the last issued one */
    FF_SLOT_LIVE,         /* issued, object reachable */
    FF_SLOT_DESTROYING,   /* last reference dropped, finaliser running */
    FF_SLOT_QUARANTINED,  /* retained forever: teardown was not provable */
    FF_SLOT_DEAD          /* generation ceiling reached: never reused */
};

typedef struct FFHandleSlot {
    void                 *obj;
    ff_handle_finalize_fn finalize;
    uint32_t              gen;
    int                   refs;
    uint16_t              type;
    uint8_t               state;
    uint8_t               closing;
    uint8_t               creator_dropped;
} FFHandleSlot;

/* 31 bits, so (gen << 32) never sets the sign bit of the int64 token.
 * Defined in the header as FF_HANDLE_GEN_MAX. */

static FFHandleSlot    g_slots[FF_HANDLE_MAX_SLOTS];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_cond = PTHREAD_COND_INITIALIZER;

/* ------------------------------------------------------------------ */
/* Token codec                                                         */
/* ------------------------------------------------------------------ */

static int64_t ff_handle_encode(unsigned slot, FFHandleType type, uint32_t gen)
{
    uint64_t t = ((uint64_t)gen << 32) |
                 ((uint64_t)(uint16_t)type << 16) |
                 ((uint64_t)(slot + 1u));
    return (int64_t)t;
}

static int ff_handle_decode(int64_t token, unsigned *slot_out,
                            FFHandleType *type_out, uint32_t *gen_out)
{
    uint64_t t;
    uint32_t slot, gen;
    uint16_t type;

    if (token <= 0)
        return FF_HANDLE_ERR_TOKEN;
    t = (uint64_t)token;
    if ((t >> 63) != 0)
        return FF_HANDLE_ERR_TOKEN;

    gen  = (uint32_t)((t >> 32) & (uint64_t)FF_HANDLE_GEN_MAX);
    type = (uint16_t)((t >> 16) & 0xFFFFu);
    slot = (uint32_t)(t & 0xFFFFu);

    if (gen == 0 || slot == 0 || type == 0 || type >= FF_HANDLE_TYPE_MAX)
        return FF_HANDLE_ERR_TOKEN;
    if (slot - 1u >= FF_HANDLE_MAX_SLOTS)
        return FF_HANDLE_ERR_TOKEN;

    *slot_out = slot - 1u;
    *type_out = (FFHandleType)type;
    *gen_out  = gen;
    return FF_HANDLE_OK;
}

/* ------------------------------------------------------------------ */
/* Slot helpers (g_lock held)                                          */
/* ------------------------------------------------------------------ */

/* Drops one reference. Returns 1 when the caller must run the finaliser
 * outside the lock, having received the object and the function. */
static int ff_slot_drop_ref_locked(FFHandleSlot *s, void **obj_out,
                                   ff_handle_finalize_fn *fn_out)
{
    if (s->refs > 0)
        s->refs--;
    if (s->refs > 0)
        return 0;
    if (s->state == FF_SLOT_QUARANTINED)
        return 0;               /* retained on purpose; nothing to finalise */

    *obj_out    = s->obj;
    *fn_out     = s->finalize;
    s->obj      = NULL;
    s->finalize = NULL;
    s->state    = FF_SLOT_DESTROYING;
    pthread_cond_broadcast(&g_cond);
    return 1;
}

/* Runs the finaliser unlocked, then returns the slot to the free list. The
 * slot cannot be reused in between: only FF_SLOT_FREE slots are handed out. */
static void ff_slot_finish(FFHandleSlot *s, void *obj, ff_handle_finalize_fn fn)
{
    if (fn)
        fn(obj);

    pthread_mutex_lock(&g_lock);
    s->state           = (s->gen >= FF_HANDLE_GEN_MAX) ? FF_SLOT_DEAD : FF_SLOT_FREE;
    s->obj             = NULL;
    s->finalize        = NULL;
    s->refs            = 0;
    s->type            = 0;
    s->closing         = 0;
    s->creator_dropped = 0;
    pthread_cond_broadcast(&g_cond);
    pthread_mutex_unlock(&g_lock);
}

/* Looks a token up for an operation. Closing tokens are rejected; release and
 * wait paths use ff_slot_lookup_locked() directly instead. */
static void ff_slot_lookup_locked(int64_t token, FFHandleType type,
                                  FFHandleSlot **slot_out, unsigned *idx_out,
                                  uint32_t *gen_out, int *status_out)
{
    unsigned slot;
    FFHandleType t;
    uint32_t gen;
    FFHandleSlot *s;
    int rc = ff_handle_decode(token, &slot, &t, &gen);

    *slot_out = NULL;
    if (rc != FF_HANDLE_OK) {
        *status_out = rc;
        return;
    }
    if (t != type) {
        /* A well formed token of another kind: never a fallback to a pointer. */
        *status_out = FF_HANDLE_ERR_TYPE;
        return;
    }
    s = &g_slots[slot];
    if (s->gen != gen || s->type != (uint16_t)type) {
        *status_out = FF_HANDLE_ERR_STALE;
        return;
    }
    if (s->state != FF_SLOT_LIVE && s->state != FF_SLOT_DESTROYING &&
        s->state != FF_SLOT_QUARANTINED) {
        *status_out = FF_HANDLE_ERR_STALE;
        return;
    }
    *slot_out = s;
    if (idx_out)
        *idx_out = slot;
    if (gen_out)
        *gen_out = gen;
    *status_out = FF_HANDLE_OK;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int64_t ff_handle_create(FFHandleType type, void *obj,
                         ff_handle_finalize_fn finalize)
{
    unsigned i;
    int64_t token = 0;
    int exhausted = 0;

    if (!obj || type <= FF_HANDLE_TYPE_NONE || type >= FF_HANDLE_TYPE_MAX)
        return 0;

    pthread_mutex_lock(&g_lock);
    for (i = 0; i < FF_HANDLE_MAX_SLOTS; i++) {
        FFHandleSlot *s = &g_slots[i];

        if (s->state == FF_SLOT_DEAD)
            continue;
        if (s->state != FF_SLOT_FREE)
            continue;
        if (s->gen >= FF_HANDLE_GEN_MAX) {
            /* Never wrap: an old token must not become valid again. */
            s->state = FF_SLOT_DEAD;
            exhausted++;
            continue;
        }

        s->gen++;
        s->obj             = obj;
        s->finalize        = finalize;
        s->type            = (uint16_t)type;
        s->refs            = 1;         /* the creator reference */
        s->closing         = 0;
        s->creator_dropped = 0;
        s->state           = FF_SLOT_LIVE;
        token = ff_handle_encode(i, type, s->gen);
        break;
    }
    pthread_mutex_unlock(&g_lock);

    (void)exhausted;
    return token;
}

void *ff_handle_acquire(int64_t token, FFHandleType type, int *status)
{
    FFHandleSlot *s = NULL;
    unsigned idx;
    uint32_t gen;
    int rc;
    void *obj = NULL;

    if (status)
        *status = FF_HANDLE_OK;

    pthread_mutex_lock(&g_lock);
    ff_slot_lookup_locked(token, type, &s, &idx, &gen, &rc);
    if (rc != FF_HANDLE_OK) {
        pthread_mutex_unlock(&g_lock);
        if (status)
            *status = rc;
        return NULL;
    }
    if (s->closing || s->state != FF_SLOT_LIVE) {
        pthread_mutex_unlock(&g_lock);
        if (status)
            *status = FF_HANDLE_ERR_CLOSING;
        return NULL;
    }
    if (s->refs <= 0) {
        pthread_mutex_unlock(&g_lock);
        if (status)
            *status = FF_HANDLE_ERR_STATE;
        return NULL;
    }
    s->refs++;
    obj = s->obj;
    pthread_mutex_unlock(&g_lock);
    return obj;
}

int ff_handle_release(int64_t token, FFHandleType type)
{
    FFHandleSlot *s = NULL;
    unsigned idx;
    uint32_t gen;
    int rc, destroy;
    void *obj = NULL;
    ff_handle_finalize_fn fn = NULL;

    pthread_mutex_lock(&g_lock);
    ff_slot_lookup_locked(token, type, &s, &idx, &gen, &rc);
    if (rc != FF_HANDLE_OK) {
        pthread_mutex_unlock(&g_lock);
        return rc;
    }
    destroy = ff_slot_drop_ref_locked(s, &obj, &fn);
    pthread_mutex_unlock(&g_lock);

    if (destroy)
        ff_slot_finish(s, obj, fn);
    return FF_HANDLE_OK;
}

int ff_handle_revoke(int64_t token, FFHandleType type)
{
    unsigned slot;
    FFHandleType t;
    uint32_t gen;
    FFHandleSlot *s;
    int rc, destroy;
    void *obj = NULL;
    ff_handle_finalize_fn fn = NULL;

    /* Close is idempotent: closing an already closed handle - or the reserved
     * 0 - is a no-op, not an error, and can never reach a newer object
     * because a reused slot carries a different generation. */
    rc = ff_handle_decode(token, &slot, &t, &gen);
    if (rc != FF_HANDLE_OK)
        return FF_HANDLE_OK;
    if (t != type) {
        /* A well formed token of another kind is a caller bug, not a double
         * close: say so instead of pretending the close succeeded. */
        return FF_HANDLE_ERR_TYPE;
    }

    pthread_mutex_lock(&g_lock);
    s = &g_slots[slot];
    if (s->gen != gen || s->type != (uint16_t)type ||
        (s->state != FF_SLOT_LIVE && s->state != FF_SLOT_QUARANTINED)) {
        pthread_mutex_unlock(&g_lock);
        return FF_HANDLE_OK;
    }

    s->closing = 1;
    destroy = 0;
    if (!s->creator_dropped) {
        s->creator_dropped = 1;
        destroy = ff_slot_drop_ref_locked(s, &obj, &fn);
    }
    pthread_mutex_unlock(&g_lock);

    if (destroy)
        ff_slot_finish(s, obj, fn);
    return FF_HANDLE_OK;
}

int ff_handle_closing(int64_t token, FFHandleType type)
{
    FFHandleSlot *s = NULL;
    unsigned idx;
    uint32_t gen;
    int rc, closing;

    pthread_mutex_lock(&g_lock);
    ff_slot_lookup_locked(token, type, &s, &idx, &gen, &rc);
    if (rc != FF_HANDLE_OK) {
        pthread_mutex_unlock(&g_lock);
        return rc;
    }
    closing = s->closing ? 1 : 0;
    pthread_mutex_unlock(&g_lock);
    return closing;
}

int ff_handle_wait_done(int64_t token, FFHandleType type, unsigned timeout_ms)
{
    unsigned slot;
    FFHandleType t;
    uint32_t gen;
    FFHandleSlot *s;
    int rc;
    struct timespec deadline;

    rc = ff_handle_decode(token, &slot, &t, &gen);
    if (rc != FF_HANDLE_OK)
        return rc;
    if (t != type)
        return FF_HANDLE_ERR_TYPE;

    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
        return FF_HANDLE_ERR_STATE;
    deadline.tv_sec  += (time_t)(timeout_ms / 1000u);
    deadline.tv_nsec += (long)(timeout_ms % 1000u) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec  += 1;
        deadline.tv_nsec -= 1000000000L;
    }

    pthread_mutex_lock(&g_lock);
    s = &g_slots[slot];
    for (;;) {
        if (s->gen != gen || s->type != (uint16_t)type) {
            /* Slot was recycled: this generation is over. */
            rc = FF_HANDLE_OK;
            break;
        }
        if (s->state == FF_SLOT_FREE || s->state == FF_SLOT_DEAD) {
            rc = FF_HANDLE_OK;
            break;
        }
        if (s->refs <= 0 && s->state != FF_SLOT_DESTROYING) {
            rc = FF_HANDLE_OK;
            break;
        }
        {
            int w = pthread_cond_timedwait(&g_cond, &g_lock, &deadline);
            if (w != 0) {
                rc = FF_HANDLE_ERR_TIMEOUT;
                break;
            }
        }
    }
    pthread_mutex_unlock(&g_lock);
    return rc;
}

int ff_handle_refcount(int64_t token, FFHandleType type)
{
    FFHandleSlot *s = NULL;
    unsigned idx;
    uint32_t gen;
    int rc, refs;

    pthread_mutex_lock(&g_lock);
    ff_slot_lookup_locked(token, type, &s, &idx, &gen, &rc);
    if (rc != FF_HANDLE_OK) {
        pthread_mutex_unlock(&g_lock);
        return rc;
    }
    refs = s->refs;
    pthread_mutex_unlock(&g_lock);
    return refs;
}

int ff_handle_is_live(int64_t token)
{
    unsigned slot;
    FFHandleType t;
    uint32_t gen;
    FFHandleSlot *s;
    int rc, live = 0;

    rc = ff_handle_decode(token, &slot, &t, &gen);
    if (rc != FF_HANDLE_OK)
        return 0;

    pthread_mutex_lock(&g_lock);
    s = &g_slots[slot];
    live = (s->gen == gen && s->type == (uint16_t)t &&
            (s->state == FF_SLOT_LIVE || s->state == FF_SLOT_DESTROYING ||
             s->state == FF_SLOT_QUARANTINED));
    pthread_mutex_unlock(&g_lock);
    return live;
}

int ff_handle_quarantine(int64_t token, FFHandleType type)
{
    FFHandleSlot *s = NULL;
    unsigned idx;
    uint32_t gen;
    int rc;

    pthread_mutex_lock(&g_lock);
    ff_slot_lookup_locked(token, type, &s, &idx, &gen, &rc);
    if (rc != FF_HANDLE_OK) {
        pthread_mutex_unlock(&g_lock);
        return rc;
    }
    s->closing = 1;
    s->state   = FF_SLOT_QUARANTINED;
    pthread_mutex_unlock(&g_lock);
    return FF_HANDLE_OK;
}

void ff_handle_registry_reset_for_tests(void)
{
    pthread_mutex_lock(&g_lock);
    memset(g_slots, 0, sizeof(g_slots));
    pthread_mutex_unlock(&g_lock);
}

int ff_handle_test_set_gen(int64_t token, FFHandleType type, uint32_t gen,
                           int64_t *new_token_out)
{
    FFHandleSlot *s = NULL;
    unsigned idx;
    uint32_t cur;
    int rc;

    if (new_token_out)
        *new_token_out = 0;

    pthread_mutex_lock(&g_lock);
    ff_slot_lookup_locked(token, type, &s, &idx, &cur, &rc);
    if (rc == FF_HANDLE_OK) {
        /* Re-issue the *same* live slot at the requested generation so the
         * caller keeps a token that still round-trips. */
        s->gen = gen;
        if (new_token_out)
            *new_token_out = ff_handle_encode(idx, type, gen);
    }
    pthread_mutex_unlock(&g_lock);
    return rc;
}

int ff_handle_inspect(int64_t token, FFHandleType *type_out, int *slot_out,
                      uint32_t *gen_out)
{
    unsigned slot;
    FFHandleType type;
    uint32_t gen;
    int rc = ff_handle_decode(token, &slot, &type, &gen);

    if (rc != FF_HANDLE_OK)
        return rc;
    if (type_out)
        *type_out = type;
    if (slot_out)
        *slot_out = (int)slot;
    if (gen_out)
        *gen_out = gen;
    return FF_HANDLE_OK;
}
