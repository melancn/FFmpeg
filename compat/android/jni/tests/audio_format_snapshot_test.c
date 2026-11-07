/*
 * Behaviour contract for the two audio format snapshot entry points:
 *
 *   streamGetAudioFormatInfo(formatCtx, streamIndex, outInfo, outChannelIds)
 *   frameGetAudioFormatInfo(frame, outInfo, outChannelIds)
 *
 * This is the only place that proves what those entry points *do*. The
 * workflow's symbol check only proves they are exported, and a capability bit
 * only proves the library claims them.
 *
 * This suite is a second translation unit, but it deliberately does NOT
 * include ../ffmpeg_jni.c: packet_contract_test.c already does, which gives
 * every Java_* entry point exactly one definition in the binary. Including
 * the bridge here as well is what produced "duplicate symbol
 * Java_org_ffmpeg_FFMpegNative_getVersion" at link time.
 *
 * So this file declares the exported entry points it calls (see the extern
 * block below) and includes only ffmpeg_jni_audio_format.h for the schema.
 * The functions it calls are therefore the real exported ones, still
 * exercised through their real argument checks and publication path.
 *
 * Schema v1 slot layout (see FFMpegNative.java, which repeats every index):
 *   0 schemaVersion        7 codecId
 *   1 knownFields          8 profile
 *   2 sampleFormat         9 bitsPerCodedSample
 *   3 sampleRate          10 bitsPerRawSample
 *   4 channelCount        11 samplesPerChannel
 *   5 channelLayoutOrder  12 channelIdsCount
 *   6 nativeChannelMask
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <jni.h>

#include "libavcodec/avcodec.h"
#include "libavcodec/codec_par.h"
#include "libavformat/avformat.h"
#include "libavutil/channel_layout.h"
#include "libavutil/error.h"
#include "libavutil/frame.h"
#include "libavutil/mem.h"

#include "../ffmpeg_jni_audio_format.h"

/* The exported entry points under test, and the two capability queries.
 *
 * Declared here rather than pulled in via ../ffmpeg_jni.c: the bridge is
 * compiled into packet_contract_test.c's translation unit, and a second
 * definition of these symbols is a link error. The declarations must match
 * the bridge exactly -- JNIEXPORT/JNICALL are empty on the ABIs this test
 * builds for, and each parameter type is spelled out below.
 *
 * Capability constants are repeated because they live in ffmpeg_jni.c (a C
 * translation unit that cannot be included twice). test_capability_bits()
 * cross-checks them against the values the library actually returns, which
 * is what keeps these copies honest. */
#define FFTEST_CAP_CODEC_SCOPED_BUFFER_ACCOUNTING (1LL << 7)
#define FFTEST_CAP_AUDIO_STREAM_FORMAT_INFO       (1LL << 8)
#define FFTEST_CAP_AUDIO_FRAME_FORMAT_INFO        (1LL << 9)
#define FFTEST_CAP_AUDIO_PROBE_POLICY             (1LL << 10)
#define FFTEST_CAP_AUDIO_PROBE_STATS              (1LL << 11)

jint Java_org_ffmpeg_FFMpegNative_streamGetAudioFormatInfo(JNIEnv *env,
                                                           jobject thiz,
                                                           jlong ctx,
                                                           jint index,
                                                           jlongArray outInfo,
                                                           jintArray outIds);
jint Java_org_ffmpeg_FFMpegNative_frameGetAudioFormatInfo(JNIEnv *env,
                                                          jobject thiz,
                                                          jlong frame,
                                                          jlongArray outInfo,
                                                          jintArray outIds);
jint  Java_org_ffmpeg_FFMpegNative_getBridgeApiVersion(JNIEnv *env, jobject thiz);
jlong Java_org_ffmpeg_FFMpegNative_getBridgeCapabilities(JNIEnv *env, jobject thiz);

/* ------------------------------------------------------------------ */
/* Harness                                                             */
/*                                                                     */
/* The entry points touch exactly five JNI slots. GetArrayLength and the */
/* two Get*ArrayRegion calls are used only as capacity validation, so a  */
/* stub with real lengths plus the two Set slots drives the production   */
/* code unchanged.                                                      */
/*                                                                     */
/* Explicitly NOT modelled: a VM that throws mid-publish, direct array   */
/* elements, and any concurrency. Those are device-stage items.         */
/* ------------------------------------------------------------------ */

extern void fftest_add_checks(int n, int f);

static int g_checks;
static int g_failures;

#define CHECK(cond) do {                                                      \
    g_checks++;                                                               \
    if (!(cond)) {                                                            \
        g_failures++;                                                         \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
    }                                                                         \
} while (0)

/* Sentinel pre-filled into every output slot, so "the entry point wrote this"
 * is distinguishable from "the array was never touched". */
#define SENTINEL_INFO 0x5A5A5A5A5A5A5A5ALL
#define SENTINEL_ID   (-12345)

typedef struct SnapshotArray {
    jsize    length;
    jsize    written;      /* elements written by the last Set call     */
    int64_t *longs;
    int32_t *ints;
    int      allow_publish; /* 0 simulates a VM that refuses the write  */
} SnapshotArray;

typedef struct SnapshotEnv {
    int publishes;
    int bad_region;        /* a write outside the array was attempted   */
} SnapshotEnv;

static SnapshotEnv g_env;

static jsize snap_len(JNIEnv *env, jarray a)
{
    const SnapshotArray *arr = (const SnapshotArray *)(const void *)a;
    (void)env;
    return arr ? arr->length : 0;
}

static void snap_set_long(JNIEnv *env, jlongArray a, jsize start, jsize len,
                          const jlong *buf)
{
    SnapshotArray *arr = (SnapshotArray *)(void *)a;
    (void)env;
    if (!arr || start < 0 || len < 0 || start + len > arr->length) {
        g_env.bad_region = 1;
        return;
    }
    if (!arr->allow_publish)
        return;
    if (len > 0 && arr->longs)
        memcpy(arr->longs + start, buf, (size_t)len * sizeof(*buf));
    arr->written = start + len;
    g_env.publishes++;
}

static void snap_set_int(JNIEnv *env, jintArray a, jsize start, jsize len,
                         const jint *buf)
{
    SnapshotArray *arr = (SnapshotArray *)(void *)a;
    (void)env;
    if (!arr || start < 0 || len < 0 || start + len > arr->length) {
        g_env.bad_region = 1;
        return;
    }
    if (!arr->allow_publish)
        return;
    if (len > 0 && arr->ints)
        memcpy(arr->ints + start, buf, (size_t)len * sizeof(*buf));
    arr->written = start + len;
    g_env.publishes++;
}

/* A VM that throws instead of silently dropping the write. The bridge checks
 * ExceptionCheck after each Set call, so this is the path a refused publish
 * really takes on device. */
static jboolean g_exception;
static jboolean snap_exception_check(JNIEnv *env)
{
    (void)env;
    return g_exception ? JNI_TRUE : JNI_FALSE;
}

static struct JNINativeInterface g_snap_functions;

static JNIEnv snap_env(void)
{
    static int initialised;

    if (!initialised) {
        memset(&g_snap_functions, 0, sizeof(g_snap_functions));
        g_snap_functions.GetArrayLength     = snap_len;
        g_snap_functions.SetLongArrayRegion = snap_set_long;
        g_snap_functions.SetIntArrayRegion  = snap_set_int;
        g_snap_functions.ExceptionCheck     = snap_exception_check;
        initialised = 1;
    }
    return (JNIEnv)&g_snap_functions;
}

static SnapshotArray *snap_array_new(jsize len)
{
    SnapshotArray *a = av_mallocz(sizeof(*a));
    jsize i;

    if (!a)
        return NULL;
    a->length = len;
    a->allow_publish = 1;
    if (len > 0) {
        /* av_calloc, not av_mallocz_array: the latter is a lavu-internal
         * name and is not declared in libavutil/mem.h. */
        a->longs = av_calloc((size_t)len, sizeof(*a->longs));
        a->ints  = av_calloc((size_t)len, sizeof(*a->ints));
        if (!a->longs || !a->ints) {
            av_free(a->longs);
            av_free(a->ints);
            av_free(a);
            return NULL;
        }
        for (i = 0; i < len; i++) {
            a->longs[i] = (int64_t)SENTINEL_INFO;
            a->ints[i]  = SENTINEL_ID;
        }
    }
    return a;
}

static void snap_array_free(SnapshotArray *a)
{
    if (!a)
        return;
    av_free(a->longs);
    av_free(a->ints);
    av_free(a);
}

static void snap_reset(void)
{
    memset(&g_env, 0, sizeof(g_env));
    g_exception = JNI_FALSE;
}

/* ------------------------------------------------------------------ */
/* Fixtures                                                            */
/*                                                                     */
/* Built from public structs only: the contract under test is "report    */
/* what the stream/frame says", so every value set here must come back   */
/* unchanged, and the fixture never opens a decoder to produce one.      */
/* ------------------------------------------------------------------ */

/* A format context plus a convenience accessor for its streams.
 *
 * There is deliberately no separate stream array: the bridge reads
 * c->streams[i]->codecpar, so the fixtures do the same through
 * snap_stream(). Keeping a parallel array was how an earlier build ended up
 * with a struct member that did not exist. */
typedef struct SnapContext {
    AVFormatContext *ctx;
} SnapContext;

/* Stream i of the context. Only valid for indices the fixtures created. */
static AVStream *snap_stream(SnapContext *sc, int i)
{
    return sc->ctx->streams[i];
}

/* n streams, all audio, at index 0..n-1. */
static int snap_context_init(SnapContext *sc, int n)
{
    int i;

    memset(sc, 0, sizeof(*sc));
    sc->ctx = avformat_alloc_context();
    if (!sc->ctx)
        return -1;
    for (i = 0; i < n; i++) {
        AVStream *st = avformat_new_stream(sc->ctx, NULL);
        if (!st)
            return -1;
        st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    }
    return 0;
}

static void snap_context_free(SnapContext *sc)
{
    avformat_free_context(sc->ctx);
    memset(sc, 0, sizeof(*sc));
}

/* A CUSTOM layout whose map is in exactly the given order, including ids a
 * mask cannot express. */
static int snap_layout_custom(AVChannelLayout *l, const int *ids, int n)
{
    int i;

    memset(l, 0, sizeof(*l));
    if (av_channel_layout_custom_init(l, n) < 0)
        return -1;
    for (i = 0; i < n; i++)
        l->u.map[i].id = (enum AVChannel)ids[i];
    return 0;
}

/* Result of the last successful call, copied out before the arrays die. */
static int64_t g_info[FF_JNI_AUDIO_FORMAT_INFO_SLOTS];
static int32_t g_ids[FF_JNI_AUDIO_FORMAT_MAX_CHANNELS];
static jsize   g_info_written;
static jsize   g_ids_written;

static int call_stream(AVFormatContext *ctx, int index)
{
    JNIEnv env = snap_env();
    SnapshotArray *info = snap_array_new(FF_JNI_AUDIO_FORMAT_INFO_SLOTS);
    SnapshotArray *ids  = snap_array_new(FF_JNI_AUDIO_FORMAT_MAX_CHANNELS);
    int ret;

    g_info_written = -1;
    g_ids_written  = -1;
    memset(g_info, 0, sizeof(g_info));
    memset(g_ids, 0, sizeof(g_ids));
    if (!info || !ids) {
        snap_array_free(info);
        snap_array_free(ids);
        return AVERROR(ENOMEM);
    }

    ret = Java_org_ffmpeg_FFMpegNative_streamGetAudioFormatInfo(
              &env, NULL, (jlong)(intptr_t)ctx, (jint)index,
              (jlongArray)(void *)info, (jintArray)(void *)ids);

    if (ret >= 0) {
        g_info_written = info->written;
        g_ids_written  = ids->written;
        memcpy(g_info, info->longs, sizeof(g_info));
        memcpy(g_ids, ids->ints, sizeof(g_ids));
    }
    snap_array_free(info);
    snap_array_free(ids);
    return ret;
}

static int call_frame(AVFrame *f)
{
    JNIEnv env = snap_env();
    SnapshotArray *info = snap_array_new(FF_JNI_AUDIO_FORMAT_INFO_SLOTS);
    SnapshotArray *ids  = snap_array_new(FF_JNI_AUDIO_FORMAT_MAX_CHANNELS);
    int ret;

    g_info_written = -1;
    g_ids_written  = -1;
    memset(g_info, 0, sizeof(g_info));
    memset(g_ids, 0, sizeof(g_ids));
    if (!info || !ids) {
        snap_array_free(info);
        snap_array_free(ids);
        return AVERROR(ENOMEM);
    }

    ret = Java_org_ffmpeg_FFMpegNative_frameGetAudioFormatInfo(
              &env, NULL, (jlong)(intptr_t)f,
              (jlongArray)(void *)info, (jintArray)(void *)ids);

    if (ret >= 0) {
        g_info_written = info->written;
        g_ids_written  = ids->written;
        memcpy(g_info, info->longs, sizeof(g_info));
        memcpy(g_ids, ids->ints, sizeof(g_ids));
    }
    snap_array_free(info);
    snap_array_free(ids);
    return ret;
}

/* knownFields is the only authority on whether a slot is defined. */
static int known(int s)
{
    return !!(g_info[FF_JNI_AUDIO_INFO_KNOWN_FIELDS] & (1LL << s));
}

static int64_t slot(int s)
{
    return g_info[s];
}

/* ------------------------------------------------------------------ */
/* Schema constants                                                    */
/* ------------------------------------------------------------------ */

/* The JAR repeats every one of these. If they drift, the JAR reads the
 * wrong slot and sees a plausible number, which is worse than a crash. */
static void test_schema_constants(void)
{
    CHECK(FF_JNI_AUDIO_FORMAT_SCHEMA_VERSION == 1);
    CHECK(FF_JNI_AUDIO_FORMAT_INFO_SLOTS == 13);
    CHECK(FF_JNI_AUDIO_FORMAT_MAX_CHANNELS == 64);

    CHECK(FF_JNI_AUDIO_INFO_SCHEMA_VERSION        == 0);
    CHECK(FF_JNI_AUDIO_INFO_KNOWN_FIELDS          == 1);
    CHECK(FF_JNI_AUDIO_INFO_SAMPLE_FORMAT         == 2);
    CHECK(FF_JNI_AUDIO_INFO_SAMPLE_RATE           == 3);
    CHECK(FF_JNI_AUDIO_INFO_CHANNEL_COUNT         == 4);
    CHECK(FF_JNI_AUDIO_INFO_CHANNEL_LAYOUT_ORDER  == 5);
    CHECK(FF_JNI_AUDIO_INFO_NATIVE_CHANNEL_MASK   == 6);
    CHECK(FF_JNI_AUDIO_INFO_CODEC_ID              == 7);
    CHECK(FF_JNI_AUDIO_INFO_PROFILE               == 8);
    CHECK(FF_JNI_AUDIO_INFO_BITS_PER_CODED_SAMPLE == 9);
    CHECK(FF_JNI_AUDIO_INFO_BITS_PER_RAW_SAMPLE   == 10);
    CHECK(FF_JNI_AUDIO_INFO_SAMPLES_PER_CHANNEL   == 11);
    CHECK(FF_JNI_AUDIO_INFO_CHANNEL_IDS_COUNT     == 12);

    /* Every slot index fits in the 64-bit known mask. */
    CHECK(FF_JNI_AUDIO_FORMAT_INFO_SLOTS <= 64);

    /* The order codes are FFmpeg's, reported unchanged. */
    CHECK(FF_JNI_AUDIO_ORDER_UNSPEC    == AV_CHANNEL_ORDER_UNSPEC);
    CHECK(FF_JNI_AUDIO_ORDER_NATIVE    == AV_CHANNEL_ORDER_NATIVE);
    CHECK(FF_JNI_AUDIO_ORDER_CUSTOM    == AV_CHANNEL_ORDER_CUSTOM);
    CHECK(FF_JNI_AUDIO_ORDER_AMBISONIC == AV_CHANNEL_ORDER_AMBISONIC);

    CHECK(FF_JNI_AUDIO_FORMAT_UNKNOWN_VALUE == 0);
}

/* ------------------------------------------------------------------ */
/* NATIVE layouts                                                      */
/* ------------------------------------------------------------------ */

static void check_native_case(const char *what, uint64_t mask,
                              const int *expected_ids, int n)
{
    SnapContext sc;
    AVCodecParameters *p;
    int i;
    int ok = 1;

    if (snap_context_init(&sc, 1) < 0) {
        CHECK(!"context allocation failed");
        snap_context_free(&sc);
        return;
    }
    p = snap_stream(&sc, 0)->codecpar;
    p->codec_id    = AV_CODEC_ID_AAC;
    p->profile     = AV_PROFILE_AAC_LOW;
    p->format      = AV_SAMPLE_FMT_FLTP;
    p->sample_rate = 48000;
    CHECK(av_channel_layout_from_mask(&p->ch_layout, mask) == 0);

    snap_reset();
    CHECK(call_stream(sc.ctx, 0) >= 0);

    /* Order, count and mask must all agree with the source. */
    CHECK(slot(FF_JNI_AUDIO_INFO_CHANNEL_LAYOUT_ORDER) == AV_CHANNEL_ORDER_NATIVE);
    CHECK(known(FF_JNI_AUDIO_INFO_CHANNEL_LAYOUT_ORDER));
    CHECK(slot(FF_JNI_AUDIO_INFO_CHANNEL_COUNT) == n);
    CHECK(known(FF_JNI_AUDIO_INFO_NATIVE_CHANNEL_MASK));
    /* The mask is FFmpeg's, bit for bit, including bit 63. It is never
     * converted to an Android AudioFormat mask here. */
    CHECK((uint64_t)slot(FF_JNI_AUDIO_INFO_NATIVE_CHANNEL_MASK) == mask);
    CHECK(slot(FF_JNI_AUDIO_INFO_CHANNEL_IDS_COUNT) == n);
    CHECK(g_ids_written == n);
    for (i = 0; i < n; i++)
        if (g_ids[i] != expected_ids[i])
            ok = 0;
    CHECK(ok);
    if (!ok)
        fprintf(stderr, "  %s: id sequence mismatch\n", what);

    snap_context_free(&sc);
}

static void test_native_mono_stereo_5_1_7_1(void)
{
    static const int mono[1]   = { AV_CHAN_FRONT_CENTER };
    static const int stereo[2] = { AV_CHAN_FRONT_LEFT, AV_CHAN_FRONT_RIGHT };
    static const int five1[6]  = { AV_CHAN_FRONT_LEFT, AV_CHAN_FRONT_RIGHT,
                                   AV_CHAN_FRONT_CENTER, AV_CHAN_LOW_FREQUENCY,
                                   AV_CHAN_BACK_LEFT, AV_CHAN_BACK_RIGHT };

    check_native_case("mono",   AV_CH_LAYOUT_MONO,  mono,   1);
    check_native_case("stereo", AV_CH_LAYOUT_STEREO, stereo, 2);
    /* 5.1 here is the side-less variant, so the ids come back in ascending
     * AVChannel order, not in any "speaker order" convention. */
    check_native_case("5.1", AV_CH_LAYOUT_5POINT1, five1, 6);

    /* 7.1 has 8 ids; build the expectation from the same accessor the
     * implementation uses, then assert it is ascending and complete. */
    {
        AVChannelLayout l = AV_CHANNEL_LAYOUT_7POINT1;
        int ids[8];
        int i;

        for (i = 0; i < 8; i++)
            ids[i] = (int)av_channel_layout_channel_from_index(&l, (unsigned)i);
        check_native_case("7.1", AV_CH_LAYOUT_7POINT1, ids, 8);
    }
}

/* A mask bit above 31 must survive the round trip: truncating to int would
 * silently report a stereo-only layout for a 3-channel one. */
static void test_high_mask_bits_survive(void)
{
    SnapContext sc;
    AVCodecParameters *p;

    if (snap_context_init(&sc, 1) < 0) {
        CHECK(!"context allocation failed");
        snap_context_free(&sc);
        return;
    }
    /* The highest channel bit reachable from a legal layout. enum AVChannel
     * stops at AV_CHAN_BINAURAL_RIGHT (62); bit 63 has no channel value, so a
     * layout cannot set it and asserting on it would be asserting on a state
     * that does not exist. Bits above 31 are what an int mask would lose. */
    const uint64_t high = AV_CH_FRONT_LEFT | AV_CH_BINAURAL_RIGHT;
    const uint64_t high32 = AV_CH_TOP_SURROUND_RIGHT;   /* id 38, above bit 31 */

    p = snap_stream(&sc, 0)->codecpar;
    p->codec_id = AV_CODEC_ID_PCM_S16LE;
    p->format   = AV_SAMPLE_FMT_S16;
    CHECK(av_channel_layout_from_mask(&p->ch_layout, high | high32) == 0);

    snap_reset();
    CHECK(call_stream(sc.ctx, 0) >= 0);
    CHECK(known(FF_JNI_AUDIO_INFO_NATIVE_CHANNEL_MASK));
    /* Bit 62 survives intact. Truncating to 32 bits would drop it.
     * The operands are parenthesised because == binds tighter than |: the
     * unparenthesised form compares against `high` and then ORs high32 into
     * a boolean, which compiles to a comparison this test never intended. */
    CHECK((uint64_t)slot(FF_JNI_AUDIO_INFO_NATIVE_CHANNEL_MASK) == (high | high32));
    CHECK(((uint64_t)slot(FF_JNI_AUDIO_INFO_NATIVE_CHANNEL_MASK) &
           (1ULL << AV_CHAN_BINAURAL_RIGHT)) != 0);
    CHECK(slot(FF_JNI_AUDIO_INFO_CHANNEL_COUNT) == 3);

    snap_context_free(&sc);
}

/* ------------------------------------------------------------------ */
/* UNSPEC vs CUSTOM at the same channel count                          */
/* ------------------------------------------------------------------ */

/* Both have 6 channels. streamGetParChannelLayout() answers 0 for both, which
 * is the ambiguity this entry point exists to remove. */
static void test_unspec_vs_custom(void)
{
    static const int custom_ids[6] = { AV_CHAN_LOW_FREQUENCY, AV_CHAN_FRONT_LEFT,
                                       AV_CHAN_FRONT_RIGHT, AV_CHAN_FRONT_CENTER,
                                       AV_CHAN_BACK_LEFT, AV_CHAN_BACK_RIGHT };
    SnapContext sc;
    AVCodecParameters *p;
    int i;

    if (snap_context_init(&sc, 2) < 0) {
        CHECK(!"context allocation failed");
        snap_context_free(&sc);
        return;
    }

    p = snap_stream(&sc, 0)->codecpar;
    p->codec_id = AV_CODEC_ID_AAC;
    memset(&p->ch_layout, 0, sizeof(p->ch_layout));
    p->ch_layout.order       = AV_CHANNEL_ORDER_UNSPEC;
    p->ch_layout.nb_channels = 6;

    p = snap_stream(&sc, 1)->codecpar;
    p->codec_id = AV_CODEC_ID_AAC;
    CHECK(snap_layout_custom(&p->ch_layout, custom_ids, 6) == 0);

    /* UNSPEC: order is a defined answer, positions are not. */
    snap_reset();
    CHECK(call_stream(sc.ctx, 0) >= 0);
    CHECK(slot(FF_JNI_AUDIO_INFO_CHANNEL_LAYOUT_ORDER) == AV_CHANNEL_ORDER_UNSPEC);
    CHECK(known(FF_JNI_AUDIO_INFO_CHANNEL_LAYOUT_ORDER));
    CHECK(slot(FF_JNI_AUDIO_INFO_CHANNEL_COUNT) == 6);
    CHECK(known(FF_JNI_AUDIO_INFO_CHANNEL_COUNT));
    /* No mask, and specifically not a stereo or 5.1 default. */
    CHECK(!known(FF_JNI_AUDIO_INFO_NATIVE_CHANNEL_MASK));
    CHECK(slot(FF_JNI_AUDIO_INFO_CHANNEL_IDS_COUNT) == 0);
    CHECK(g_ids_written == 0);

    /* CUSTOM: same count, different order, ids in the stored order. No
     * mask, because the union holds a map pointer, not a mask. */
    snap_reset();
    CHECK(call_stream(sc.ctx, 1) >= 0);
    CHECK(slot(FF_JNI_AUDIO_INFO_CHANNEL_LAYOUT_ORDER) == AV_CHANNEL_ORDER_CUSTOM);
    CHECK(slot(FF_JNI_AUDIO_INFO_CHANNEL_COUNT) == 6);
    CHECK(!known(FF_JNI_AUDIO_INFO_NATIVE_CHANNEL_MASK));
    CHECK(slot(FF_JNI_AUDIO_INFO_CHANNEL_IDS_COUNT) == 6);
    for (i = 0; i < 6; i++)
        CHECK(g_ids[i] == custom_ids[i]);

    snap_context_free(&sc);
}

/* A CUSTOM layout may contain channels that cannot be located. The raw id is
 * preserved and the snapshot does not claim a position for it. */
static void test_custom_unlocatable_channels(void)
{
    static const int ids[3] = { AV_CHAN_FRONT_LEFT, AV_CHAN_UNKNOWN,
                                AV_CHAN_UNUSED };
    SnapContext sc;
    AVCodecParameters *p;

    if (snap_context_init(&sc, 1) < 0) {
        CHECK(!"context allocation failed");
        snap_context_free(&sc);
        return;
    }
    p = snap_stream(&sc, 0)->codecpar;
    p->codec_id = AV_CODEC_ID_AAC;
    CHECK(snap_layout_custom(&p->ch_layout, ids, 3) == 0);

    snap_reset();
    CHECK(call_stream(sc.ctx, 0) >= 0);
    CHECK(slot(FF_JNI_AUDIO_INFO_CHANNEL_LAYOUT_ORDER) == AV_CHANNEL_ORDER_CUSTOM);
    CHECK(slot(FF_JNI_AUDIO_INFO_CHANNEL_COUNT) == 3);
    /* Carried through verbatim: the caller decides whether it can use them. */
    CHECK(g_ids[0] == AV_CHAN_FRONT_LEFT);
    CHECK(g_ids[1] == AV_CHAN_UNKNOWN);
    CHECK(g_ids[2] == AV_CHAN_UNUSED);
    /* Never turned into a mask. */
    CHECK(!known(FF_JNI_AUDIO_INFO_NATIVE_CHANNEL_MASK));

    snap_context_free(&sc);
}

/* Ambisonic order survives, ids are the ACN components, and the mask is
 * Unknown unless a non-diegetic mask was actually set. */
static void test_ambisonic(void)
{
    SnapContext sc;
    AVCodecParameters *p;
    int i;

    if (snap_context_init(&sc, 1) < 0) {
        CHECK(!"context allocation failed");
        snap_context_free(&sc);
        return;
    }
    p = snap_stream(&sc, 0)->codecpar;
    p->codec_id = AV_CODEC_ID_PCM_F32LE;
    p->format   = AV_SAMPLE_FMT_FLT;
    /* There is no av_channel_layout_ambisonic_init() in this tree; first
     * order ambisonic is a designated initializer (4 ACN channels, no
     * non-diegetic mask). */
    {
        const AVChannelLayout ambi = AV_CHANNEL_LAYOUT_AMBISONIC_FIRST_ORDER;

        CHECK(av_channel_layout_copy(&p->ch_layout, &ambi) == 0);
    }
    CHECK(p->ch_layout.order == AV_CHANNEL_ORDER_AMBISONIC);
    CHECK(p->ch_layout.nb_channels == 4);

    snap_reset();
    CHECK(call_stream(sc.ctx, 0) >= 0);
    CHECK(slot(FF_JNI_AUDIO_INFO_CHANNEL_LAYOUT_ORDER) == AV_CHANNEL_ORDER_AMBISONIC);
    CHECK(slot(FF_JNI_AUDIO_INFO_CHANNEL_COUNT) == 4);
    CHECK(slot(FF_JNI_AUDIO_INFO_CHANNEL_IDS_COUNT) == 4);
    for (i = 0; i < 4; i++)
        CHECK(g_ids[i] == AV_CHAN_AMBISONIC_BASE + i);
    CHECK(!known(FF_JNI_AUDIO_INFO_NATIVE_CHANNEL_MASK));

    snap_context_free(&sc);
}

/* ------------------------------------------------------------------ */
/* Precision: storage format and valid bits are different facts         */
/* ------------------------------------------------------------------ */

static void test_s32_with_24_raw_bits(void)
{
    SnapContext sc;
    AVCodecParameters *p;

    if (snap_context_init(&sc, 1) < 0) {
        CHECK(!"context allocation failed");
        snap_context_free(&sc);
        return;
    }
    p = snap_stream(&sc, 0)->codecpar;
    p->codec_id              = AV_CODEC_ID_TRUEHD;
    p->profile               = AV_PROFILE_UNKNOWN;
    p->format                = AV_SAMPLE_FMT_S32;
    p->sample_rate           = 96000;
    p->bits_per_coded_sample = 24;
    p->bits_per_raw_sample   = 24;
    CHECK(av_channel_layout_from_mask(&p->ch_layout, AV_CH_LAYOUT_7POINT1) == 0);

    snap_reset();
    CHECK(call_stream(sc.ctx, 0) >= 0);

    /* Two separate facts, both reported. */
    CHECK(slot(FF_JNI_AUDIO_INFO_SAMPLE_FORMAT) == AV_SAMPLE_FMT_S32);
    CHECK(known(FF_JNI_AUDIO_INFO_SAMPLE_FORMAT));
    CHECK(slot(FF_JNI_AUDIO_INFO_BITS_PER_RAW_SAMPLE) == 24);
    CHECK(known(FF_JNI_AUDIO_INFO_BITS_PER_RAW_SAMPLE));
    CHECK(slot(FF_JNI_AUDIO_INFO_BITS_PER_CODED_SAMPLE) == 24);
    CHECK(known(FF_JNI_AUDIO_INFO_BITS_PER_CODED_SAMPLE));

    CHECK(slot(FF_JNI_AUDIO_INFO_SAMPLE_RATE) == 96000);
    CHECK(slot(FF_JNI_AUDIO_INFO_CODEC_ID) == AV_CODEC_ID_TRUEHD);
    CHECK(known(FF_JNI_AUDIO_INFO_CODEC_ID));

    /* AV_PROFILE_UNKNOWN means "no profile stated": the slot is Unknown
     * rather than carrying a sentinel the caller might print. */
    CHECK(!known(FF_JNI_AUDIO_INFO_PROFILE));
    CHECK(slot(FF_JNI_AUDIO_INFO_PROFILE) == FF_JNI_AUDIO_FORMAT_UNKNOWN_VALUE);

    snap_context_free(&sc);
}

/* ------------------------------------------------------------------ */
/* Missing fields stay missing                                         */
/* ------------------------------------------------------------------ */

/* A stream that states nothing beyond its codec: no rate, no format, no
 * profile, no bit depths. Nothing may be filled in with 48k / S16 / stereo. */
static void test_missing_fields_stay_unknown(void)
{
    SnapContext sc;
    AVCodecParameters *p;

    if (snap_context_init(&sc, 1) < 0) {
        CHECK(!"context allocation failed");
        snap_context_free(&sc);
        return;
    }
    p = snap_stream(&sc, 0)->codecpar;
    p->codec_id              = AV_CODEC_ID_TRUEHD;
    p->format                = AV_SAMPLE_FMT_NONE;   /* -1 */
    p->sample_rate           = 0;
    p->profile               = AV_PROFILE_UNKNOWN;   /* -99 */
    p->bits_per_coded_sample = 0;
    p->bits_per_raw_sample   = 0;
    memset(&p->ch_layout, 0, sizeof(p->ch_layout));
    p->ch_layout.order       = AV_CHANNEL_ORDER_UNSPEC;
    p->ch_layout.nb_channels = 8;

    snap_reset();
    CHECK(call_stream(sc.ctx, 0) >= 0);

    CHECK(!known(FF_JNI_AUDIO_INFO_SAMPLE_FORMAT));
    CHECK(slot(FF_JNI_AUDIO_INFO_SAMPLE_FORMAT) == FF_JNI_AUDIO_FORMAT_UNKNOWN_VALUE);
    CHECK(!known(FF_JNI_AUDIO_INFO_SAMPLE_RATE));
    CHECK(slot(FF_JNI_AUDIO_INFO_SAMPLE_RATE) == FF_JNI_AUDIO_FORMAT_UNKNOWN_VALUE);
    CHECK(!known(FF_JNI_AUDIO_INFO_PROFILE));
    CHECK(!known(FF_JNI_AUDIO_INFO_BITS_PER_CODED_SAMPLE));
    CHECK(!known(FF_JNI_AUDIO_INFO_BITS_PER_RAW_SAMPLE));
    CHECK(!known(FF_JNI_AUDIO_INFO_NATIVE_CHANNEL_MASK));

    /* What the stream does say is still reported. */
    CHECK(known(FF_JNI_AUDIO_INFO_CODEC_ID));
    CHECK(slot(FF_JNI_AUDIO_INFO_CHANNEL_COUNT) == 8);
    CHECK(slot(FF_JNI_AUDIO_INFO_CHANNEL_LAYOUT_ORDER) == AV_CHANNEL_ORDER_UNSPEC);

    /* Every written slot is either defined or the declared placeholder. */
    {
        int i;
        int consistent = 1;
        for (i = 0; i < FF_JNI_AUDIO_FORMAT_INFO_SLOTS; i++)
            if (!known(i) && g_info[i] != FF_JNI_AUDIO_FORMAT_UNKNOWN_VALUE)
                consistent = 0;
        CHECK(consistent);
    }
    /* Zero is not the same as unknown: 0 channels would be a defined layout. */
    CHECK(slot(FF_JNI_AUDIO_INFO_CHANNEL_COUNT) != 0);

    snap_context_free(&sc);
}

/* ------------------------------------------------------------------ */
/* Frame snapshot                                                      */
/* ------------------------------------------------------------------ */

static void test_frame_snapshot(void)
{
    AVFrame *f = av_frame_alloc();
    uint64_t mask;

    CHECK(f != NULL);
    if (!f)
        return;

    f->format      = AV_SAMPLE_FMT_S32;
    f->sample_rate = 44100;
    f->nb_samples  = 1024;
    CHECK(av_channel_layout_from_mask(&f->ch_layout, AV_CH_LAYOUT_STEREO) == 0);
    mask = f->ch_layout.u.mask;
    CHECK(mask != 0);

    snap_reset();
    CHECK(call_frame(f) >= 0);

    CHECK(slot(FF_JNI_AUDIO_INFO_SAMPLE_FORMAT) == AV_SAMPLE_FMT_S32);
    CHECK(slot(FF_JNI_AUDIO_INFO_SAMPLE_RATE) == 44100);
    CHECK(slot(FF_JNI_AUDIO_INFO_CHANNEL_COUNT) == 2);
    CHECK((uint64_t)slot(FF_JNI_AUDIO_INFO_NATIVE_CHANNEL_MASK) == mask);
    CHECK(slot(FF_JNI_AUDIO_INFO_CHANNEL_IDS_COUNT) == 2);
    CHECK(g_ids[0] == AV_CHAN_FRONT_LEFT);
    CHECK(g_ids[1] == AV_CHAN_FRONT_RIGHT);

    /* samplesPerChannel is frame-only and defined, even though the value
     * could legitimately be 0. */
    CHECK(known(FF_JNI_AUDIO_INFO_SAMPLES_PER_CHANNEL));
    CHECK(slot(FF_JNI_AUDIO_INFO_SAMPLES_PER_CHANNEL) == 1024);

    /* The four slots a frame cannot answer. */
    CHECK(!known(FF_JNI_AUDIO_INFO_CODEC_ID));
    CHECK(!known(FF_JNI_AUDIO_INFO_PROFILE));
    CHECK(!known(FF_JNI_AUDIO_INFO_BITS_PER_RAW_SAMPLE));
    CHECK(!known(FF_JNI_AUDIO_INFO_BITS_PER_CODED_SAMPLE));
    /* S32 must not be read as "32 valid bits" by proxy. */
    CHECK(slot(FF_JNI_AUDIO_INFO_BITS_PER_RAW_SAMPLE) == FF_JNI_AUDIO_FORMAT_UNKNOWN_VALUE);

    av_frame_free(&f);
}

/* An empty frame is a defined answer, not an unknown one. */
static void test_frame_zero_samples_is_defined(void)
{
    AVFrame *f = av_frame_alloc();

    CHECK(f != NULL);
    if (!f)
        return;

    f->format      = AV_SAMPLE_FMT_FLTP;
    f->sample_rate = 48000;
    f->nb_samples  = 0;
    CHECK(av_channel_layout_from_mask(&f->ch_layout, AV_CH_LAYOUT_STEREO) == 0);

    snap_reset();
    CHECK(call_frame(f) >= 0);
    CHECK(known(FF_JNI_AUDIO_INFO_SAMPLES_PER_CHANNEL));
    CHECK(slot(FF_JNI_AUDIO_INFO_SAMPLES_PER_CHANNEL) == 0);

    av_frame_free(&f);
}

/* A frame never inherits a stream's codec/profile/bit depth, even when a
 * stream snapshot was taken immediately before. */
static void test_frame_does_not_inherit_from_previous_stream_query(void)
{
    SnapContext sc;
    AVCodecParameters *p;
    AVFrame *f;

    if (snap_context_init(&sc, 1) < 0) {
        CHECK(!"context allocation failed");
        snap_context_free(&sc);
        return;
    }
    p = snap_stream(&sc, 0)->codecpar;
    p->codec_id            = AV_CODEC_ID_TRUEHD;
    p->profile             = AV_PROFILE_TRUEHD_ATMOS;
    p->format              = AV_SAMPLE_FMT_S32;
    p->sample_rate         = 96000;
    p->bits_per_raw_sample = 24;
    CHECK(av_channel_layout_from_mask(&p->ch_layout, AV_CH_LAYOUT_7POINT1) == 0);

    snap_reset();
    CHECK(call_stream(sc.ctx, 0) >= 0);
    CHECK(known(FF_JNI_AUDIO_INFO_BITS_PER_RAW_SAMPLE));

    f = av_frame_alloc();
    CHECK(f != NULL);
    if (f) {
        f->format      = AV_SAMPLE_FMT_FLTP;
        f->sample_rate = 48000;
        f->nb_samples  = 1536;
        CHECK(av_channel_layout_from_mask(&f->ch_layout, AV_CH_LAYOUT_STEREO) == 0);

        CHECK(call_frame(f) >= 0);
        CHECK(!known(FF_JNI_AUDIO_INFO_CODEC_ID));
        CHECK(!known(FF_JNI_AUDIO_INFO_PROFILE));
        CHECK(!known(FF_JNI_AUDIO_INFO_BITS_PER_RAW_SAMPLE));
        CHECK(slot(FF_JNI_AUDIO_INFO_SAMPLE_RATE) == 48000);
        CHECK(slot(FF_JNI_AUDIO_INFO_CHANNEL_COUNT) == 2);
        av_frame_free(&f);
    }

    snap_context_free(&sc);
}

/* An UNSPEC frame reports no layout and is not normalised. */
static void test_frame_unspec_not_normalised(void)
{
    AVFrame *f = av_frame_alloc();

    CHECK(f != NULL);
    if (!f)
        return;

    f->format      = AV_SAMPLE_FMT_FLTP;
    f->sample_rate = 48000;
    f->nb_samples  = 1536;
    memset(&f->ch_layout, 0, sizeof(f->ch_layout));
    f->ch_layout.order       = AV_CHANNEL_ORDER_UNSPEC;
    f->ch_layout.nb_channels = 2;

    snap_reset();
    CHECK(call_frame(f) >= 0);
    CHECK(slot(FF_JNI_AUDIO_INFO_CHANNEL_LAYOUT_ORDER) == AV_CHANNEL_ORDER_UNSPEC);
    CHECK(slot(FF_JNI_AUDIO_INFO_CHANNEL_COUNT) == 2);
    CHECK(!known(FF_JNI_AUDIO_INFO_NATIVE_CHANNEL_MASK));
    CHECK(slot(FF_JNI_AUDIO_INFO_CHANNEL_IDS_COUNT) == 0);

    /* The frame itself is untouched: no stereo default was installed. */
    CHECK(f->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC);
    CHECK(f->ch_layout.nb_channels == 2);

    av_frame_free(&f);
}

/* ------------------------------------------------------------------ */
/* Read-only contract                                                  */
/* ------------------------------------------------------------------ */

/* A query must not touch the source object. CUSTOM is the sharpest case: its
 * map is heap-owned and shared with the demuxer. */
static void test_query_does_not_modify_source(void)
{
    static const int ids[4] = { AV_CHAN_BACK_LEFT, AV_CHAN_FRONT_LEFT,
                                AV_CHAN_BACK_RIGHT, AV_CHAN_FRONT_RIGHT };
    SnapContext sc;
    AVCodecParameters *p;
    AVChannelCustom *map_before;
    int i;
    int same = 1;

    if (snap_context_init(&sc, 1) < 0) {
        CHECK(!"context allocation failed");
        snap_context_free(&sc);
        return;
    }
    p = snap_stream(&sc, 0)->codecpar;
    p->codec_id = AV_CODEC_ID_AAC;
    CHECK(snap_layout_custom(&p->ch_layout, ids, 4) == 0);
    map_before = p->ch_layout.u.map;

    snap_reset();
    CHECK(call_stream(sc.ctx, 0) >= 0);
    CHECK(call_stream(sc.ctx, 0) >= 0);   /* second query, same object */

    CHECK(p->ch_layout.order == AV_CHANNEL_ORDER_CUSTOM);
    CHECK(p->ch_layout.nb_channels == 4);
    CHECK(p->ch_layout.u.map == map_before);
    for (i = 0; i < 4; i++)
        if (p->ch_layout.u.map[i].id != ids[i])
            same = 0;
    CHECK(same);

    snap_context_free(&sc);
}

/* Two independent sources queried back to back must not share state. A
 * process-global "current stream" would show up here. */
static void test_no_state_between_sources(void)
{
    SnapContext a, b;
    AVCodecParameters *p;
    int32_t first_ids[8];
    int same = 1;
    int i;

    if (snap_context_init(&a, 1) < 0 || snap_context_init(&b, 1) < 0) {
        CHECK(!"context allocation failed");
        snap_context_free(&a);
        snap_context_free(&b);
        return;
    }

    p = snap_stream(&a, 0)->codecpar;
    p->codec_id = AV_CODEC_ID_AAC;
    CHECK(av_channel_layout_from_mask(&p->ch_layout, AV_CH_LAYOUT_7POINT1) == 0);

    p = snap_stream(&b, 0)->codecpar;
    p->codec_id = AV_CODEC_ID_MP3;
    CHECK(av_channel_layout_from_mask(&p->ch_layout, AV_CH_LAYOUT_STEREO) == 0);

    snap_reset();
    CHECK(call_stream(a.ctx, 0) >= 0);
    CHECK(slot(FF_JNI_AUDIO_INFO_CHANNEL_COUNT) == 8);
    CHECK(slot(FF_JNI_AUDIO_INFO_CODEC_ID) == AV_CODEC_ID_AAC);
    memcpy(first_ids, g_ids, sizeof(first_ids));

    CHECK(call_stream(b.ctx, 0) >= 0);
    CHECK(slot(FF_JNI_AUDIO_INFO_CHANNEL_COUNT) == 2);
    CHECK(slot(FF_JNI_AUDIO_INFO_CODEC_ID) == AV_CODEC_ID_MP3);
    CHECK(g_ids_written == 2);

    /* Back to a: the result is reproduced exactly, ids included. */
    CHECK(call_stream(a.ctx, 0) >= 0);
    CHECK(g_ids_written == 8);
    for (i = 0; i < 8; i++)
        if (g_ids[i] != first_ids[i])
            same = 0;
    CHECK(same);

    snap_context_free(&a);
    snap_context_free(&b);
}

/* A reused output array must not show the previous call's values. */
static void test_success_overwrites_every_slot(void)
{
    SnapContext sc;
    AVCodecParameters *p;
    int i;
    int stale = 0;

    if (snap_context_init(&sc, 1) < 0) {
        CHECK(!"context allocation failed");
        snap_context_free(&sc);
        return;
    }
    p = snap_stream(&sc, 0)->codecpar;
    p->codec_id = AV_CODEC_ID_AAC;
    CHECK(av_channel_layout_from_mask(&p->ch_layout, AV_CH_LAYOUT_STEREO) == 0);

    snap_reset();
    CHECK(call_stream(sc.ctx, 0) >= 0);

    /* Now strip the stream down to a layout-only declaration and query
     * again: every slot that was defined before must be overwritten by the
     * placeholder, not left holding the old value. */
    p->format                = AV_SAMPLE_FMT_NONE;
    p->sample_rate           = 0;
    p->profile               = AV_PROFILE_UNKNOWN;
    p->bits_per_coded_sample = 0;
    p->bits_per_raw_sample   = 0;

    CHECK(call_stream(sc.ctx, 0) >= 0);
    for (i = 2; i < FF_JNI_AUDIO_FORMAT_INFO_SLOTS; i++) {
        if (i == FF_JNI_AUDIO_INFO_CHANNEL_COUNT ||
            i == FF_JNI_AUDIO_INFO_CHANNEL_LAYOUT_ORDER ||
            i == FF_JNI_AUDIO_INFO_CHANNEL_IDS_COUNT ||
            i == FF_JNI_AUDIO_INFO_CODEC_ID ||
            i == FF_JNI_AUDIO_INFO_NATIVE_CHANNEL_MASK)
            continue;
        if (known(i) || g_info[i] != FF_JNI_AUDIO_FORMAT_UNKNOWN_VALUE)
            stale = 1;
    }
    CHECK(!stale);

    snap_context_free(&sc);
}

/* ------------------------------------------------------------------ */
/* Errors: capacity, bounds, invalid arguments                         */
/* ------------------------------------------------------------------ */

/* outInfo too short is EINVAL; a snapshot missing slots is not a snapshot. */
static void test_short_info_is_einval(void)
{
    SnapContext sc;
    JNIEnv env;
    SnapshotArray *info;
    SnapshotArray *ids;
    int i;
    int untouched = 1;

    if (snap_context_init(&sc, 1) < 0) {
        CHECK(!"context allocation failed");
        snap_context_free(&sc);
        return;
    }
    snap_stream(&sc, 0)->codecpar->codec_id = AV_CODEC_ID_AAC;
    CHECK(av_channel_layout_from_mask(&snap_stream(&sc, 0)->codecpar->ch_layout,
                                      AV_CH_LAYOUT_STEREO) == 0);

    env  = snap_env();
    info = snap_array_new(FF_JNI_AUDIO_FORMAT_INFO_SLOTS - 1);
    ids  = snap_array_new(FF_JNI_AUDIO_FORMAT_MAX_CHANNELS);
    CHECK(info && ids);
    if (!info || !ids)
        goto done;

    snap_reset();
    CHECK(Java_org_ffmpeg_FFMpegNative_streamGetAudioFormatInfo(
              &env, NULL, (jlong)(intptr_t)sc.ctx, 0,
              (jlongArray)(void *)info, (jintArray)(void *)ids) == AVERROR(EINVAL));
    /* Neither array was touched: sentinels are intact. */
    for (i = 0; i < info->length; i++)
        if (info->longs[i] != (int64_t)SENTINEL_INFO)
            untouched = 0;
    for (i = 0; i < ids->length; i++)
        if (ids->ints[i] != SENTINEL_ID)
            untouched = 0;
    CHECK(untouched);
    CHECK(info->written == 0);
    CHECK(ids->written == 0);
    CHECK(g_env.publishes == 0);
    CHECK(g_env.bad_region == 0);

done:
    snap_array_free(info);
    snap_array_free(ids);
    snap_context_free(&sc);
}

/* outChannelIds too short for this layout is ENOSPC, never a truncated
 * layout. A 5-of-6 array that "succeeds" would silently drop a channel. */
static void test_short_ids_is_enospc(void)
{
    SnapContext sc;
    JNIEnv env;
    SnapshotArray *info;
    SnapshotArray *ids;
    int i;
    int untouched = 1;

    if (snap_context_init(&sc, 1) < 0) {
        CHECK(!"context allocation failed");
        snap_context_free(&sc);
        return;
    }
    snap_stream(&sc, 0)->codecpar->codec_id = AV_CODEC_ID_AAC;
    CHECK(av_channel_layout_from_mask(&snap_stream(&sc, 0)->codecpar->ch_layout,
                                      AV_CH_LAYOUT_5POINT1) == 0);

    env  = snap_env();
    info = snap_array_new(FF_JNI_AUDIO_FORMAT_INFO_SLOTS);
    ids  = snap_array_new(5);            /* one short of 6 */
    CHECK(info && ids);
    if (!info || !ids)
        goto done;

    snap_reset();
    CHECK(Java_org_ffmpeg_FFMpegNative_streamGetAudioFormatInfo(
              &env, NULL, (jlong)(intptr_t)sc.ctx, 0,
              (jlongArray)(void *)info, (jintArray)(void *)ids) == AVERROR(ENOSPC));
    for (i = 0; i < info->length; i++)
        if (info->longs[i] != (int64_t)SENTINEL_INFO)
            untouched = 0;
    for (i = 0; i < ids->length; i++)
        if (ids->ints[i] != SENTINEL_ID)
            untouched = 0;
    CHECK(untouched);
    CHECK(g_env.publishes == 0);
    CHECK(g_env.bad_region == 0);

    /* The same stream with a big enough array succeeds: the refusal is about
     * capacity, not about the stream being unreadable. */
    snap_array_free(ids);
    ids = snap_array_new(6);
    CHECK(ids != NULL);
    if (!ids)
        goto done;
    CHECK(Java_org_ffmpeg_FFMpegNative_streamGetAudioFormatInfo(
              &env, NULL, (jlong)(intptr_t)sc.ctx, 0,
              (jlongArray)(void *)info, (jintArray)(void *)ids) == 0);

done:
    snap_array_free(info);
    snap_array_free(ids);
    snap_context_free(&sc);
}

/* The bridge ceiling: 64 is accepted, 65 is E2BIG, and nothing is clamped. */
static void test_channel_count_ceiling(void)
{
    SnapContext sc;
    AVCodecParameters *p;
    int *ids = NULL;
    int i;

    if (snap_context_init(&sc, 2) < 0) {
        CHECK(!"context allocation failed");
        snap_context_free(&sc);
        return;
    }
    ids = av_calloc(65, sizeof(*ids));
    CHECK(ids != NULL);
    if (!ids) {
        snap_context_free(&sc);
        return;
    }
    for (i = 0; i < 65; i++)
        ids[i] = AV_CHAN_UNKNOWN;

    p = snap_stream(&sc, 0)->codecpar;
    p->codec_id = AV_CODEC_ID_PCM_S16LE;
    p->format   = AV_SAMPLE_FMT_S16;
    CHECK(snap_layout_custom(&p->ch_layout, ids, 64) == 0);
    snap_reset();
    CHECK(call_stream(sc.ctx, 0) >= 0);
    CHECK(slot(FF_JNI_AUDIO_INFO_CHANNEL_COUNT) == 64);
    CHECK(slot(FF_JNI_AUDIO_INFO_CHANNEL_IDS_COUNT) == 64);
    CHECK(g_ids_written == 64);

    /* 65 is refused outright: not clamped to 64, not to stereo. */
    p = snap_stream(&sc, 1)->codecpar;
    p->codec_id = AV_CODEC_ID_PCM_S16LE;
    CHECK(snap_layout_custom(&p->ch_layout, ids, 65) == 0);
    snap_reset();
    CHECK(call_stream(sc.ctx, 1) == AVERROR(E2BIG));
    CHECK(g_info_written == -1);
    CHECK(g_env.publishes == 0);

    av_free(ids);
    snap_context_free(&sc);
}

/* Handles, indices and stream types. */
static void test_invalid_arguments(void)
{
    SnapContext sc;
    JNIEnv env;
    SnapshotArray *info;
    SnapshotArray *ids;
    int i;
    int untouched = 1;

    if (snap_context_init(&sc, 1) < 0) {
        CHECK(!"context allocation failed");
        snap_context_free(&sc);
        return;
    }

    /* A video stream is not an audio stream. */
    {
        AVStream *v = avformat_new_stream(sc.ctx, NULL);
        CHECK(v != NULL);
        if (v)
            v->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    }

    env  = snap_env();
    info = snap_array_new(FF_JNI_AUDIO_FORMAT_INFO_SLOTS);
    ids  = snap_array_new(FF_JNI_AUDIO_FORMAT_MAX_CHANNELS);
    CHECK(info && ids);
    if (!info || !ids)
        goto done;

    snap_reset();
    CHECK(Java_org_ffmpeg_FFMpegNative_streamGetAudioFormatInfo(
              &env, NULL, 0, 0,
              (jlongArray)(void *)info, (jintArray)(void *)ids) == AVERROR(EINVAL));
    CHECK(Java_org_ffmpeg_FFMpegNative_streamGetAudioFormatInfo(
              &env, NULL, (jlong)(intptr_t)sc.ctx, -1,
              (jlongArray)(void *)info, (jintArray)(void *)ids) == AVERROR(EINVAL));
    CHECK(Java_org_ffmpeg_FFMpegNative_streamGetAudioFormatInfo(
              &env, NULL, (jlong)(intptr_t)sc.ctx, (jint)sc.ctx->nb_streams,
              (jlongArray)(void *)info, (jintArray)(void *)ids) == AVERROR(EINVAL));
    /* Index 1 is the video stream added above. */
    CHECK(Java_org_ffmpeg_FFMpegNative_streamGetAudioFormatInfo(
              &env, NULL, (jlong)(intptr_t)sc.ctx, 1,
              (jlongArray)(void *)info, (jintArray)(void *)ids) == AVERROR(EINVAL));

    /* Null frames and null arrays. */
    CHECK(Java_org_ffmpeg_FFMpegNative_frameGetAudioFormatInfo(
              &env, NULL, 0,
              (jlongArray)(void *)info, (jintArray)(void *)ids) == AVERROR(EINVAL));
    CHECK(Java_org_ffmpeg_FFMpegNative_streamGetAudioFormatInfo(
              &env, NULL, (jlong)(intptr_t)sc.ctx, 0, NULL,
              (jintArray)(void *)ids) == AVERROR(EINVAL));
    CHECK(Java_org_ffmpeg_FFMpegNative_streamGetAudioFormatInfo(
              &env, NULL, (jlong)(intptr_t)sc.ctx, 0,
              (jlongArray)(void *)info, NULL) == AVERROR(EINVAL));

    for (i = 0; i < info->length; i++)
        if (info->longs[i] != (int64_t)SENTINEL_INFO)
            untouched = 0;
    for (i = 0; i < ids->length; i++)
        if (ids->ints[i] != SENTINEL_ID)
            untouched = 0;
    CHECK(untouched);
    CHECK(g_env.publishes == 0);
    CHECK(g_env.bad_region == 0);

done:
    snap_array_free(info);
    snap_array_free(ids);
    snap_context_free(&sc);
}

/* A VM that throws during publication must produce an error, not a silent
 * success over an unwritten array. */
static void test_publish_exception_is_reported(void)
{
    SnapContext sc;
    JNIEnv env;
    SnapshotArray *info;
    SnapshotArray *ids;

    if (snap_context_init(&sc, 1) < 0) {
        CHECK(!"context allocation failed");
        snap_context_free(&sc);
        return;
    }
    snap_stream(&sc, 0)->codecpar->codec_id = AV_CODEC_ID_AAC;
    CHECK(av_channel_layout_from_mask(&snap_stream(&sc, 0)->codecpar->ch_layout,
                                      AV_CH_LAYOUT_STEREO) == 0);

    env  = snap_env();
    info = snap_array_new(FF_JNI_AUDIO_FORMAT_INFO_SLOTS);
    ids  = snap_array_new(FF_JNI_AUDIO_FORMAT_MAX_CHANNELS);
    CHECK(info && ids);
    if (!info || !ids)
        goto done;

    snap_reset();
    g_exception = JNI_TRUE;
    CHECK(Java_org_ffmpeg_FFMpegNative_streamGetAudioFormatInfo(
              &env, NULL, (jlong)(intptr_t)sc.ctx, 0,
              (jlongArray)(void *)info, (jintArray)(void *)ids) < 0);

done:
    snap_array_free(info);
    snap_array_free(ids);
    snap_context_free(&sc);
}

/* ------------------------------------------------------------------ */
/* Version and capability                                              */
/* ------------------------------------------------------------------ */

/* Bits 8 and 9 are advertised only because the cases above exist. Bits 7, 10
 * and 11 stay clear until their own implementation and tests do. */
static void test_capability_bits(void)
{
    const jlong caps = Java_org_ffmpeg_FFMpegNative_getBridgeCapabilities(NULL, NULL);

    CHECK(caps & FFTEST_CAP_AUDIO_STREAM_FORMAT_INFO);
    CHECK(caps & FFTEST_CAP_AUDIO_FRAME_FORMAT_INFO);

    /* Allocated but unimplemented: never returned. */
    CHECK((caps & FFTEST_CAP_CODEC_SCOPED_BUFFER_ACCOUNTING) == 0);
    CHECK((caps & FFTEST_CAP_AUDIO_PROBE_POLICY) == 0);
    CHECK((caps & FFTEST_CAP_AUDIO_PROBE_STATS) == 0);

    /* Distinct, in the frozen positions. */
    CHECK(FFTEST_CAP_CODEC_SCOPED_BUFFER_ACCOUNTING == (1LL << 7));
    CHECK(FFTEST_CAP_AUDIO_STREAM_FORMAT_INFO == (1LL << 8));
    CHECK(FFTEST_CAP_AUDIO_FRAME_FORMAT_INFO == (1LL << 9));
    CHECK(FFTEST_CAP_AUDIO_PROBE_POLICY == (1LL << 10));
    CHECK(FFTEST_CAP_AUDIO_PROBE_STATS == (1LL << 11));
    CHECK((FFTEST_CAP_AUDIO_STREAM_FORMAT_INFO &
           FFTEST_CAP_AUDIO_FRAME_FORMAT_INFO) == 0);
    CHECK((FFTEST_CAP_AUDIO_PROBE_POLICY &
           FFTEST_CAP_AUDIO_PROBE_STATS) == 0);

    /* The whole mask for this release, written out once so an unannounced
     * bit is a test failure rather than a silent contract change. The local
     * constants above are copies; this equality is what proves they still
     * describe the library. */
    CHECK(caps == (jlong)0x37f);
    CHECK((caps & (FFTEST_CAP_AUDIO_STREAM_FORMAT_INFO |
                   FFTEST_CAP_AUDIO_FRAME_FORMAT_INFO)) ==
          (jlong)(0x37f & ~(jlong)0x7f));
}

/* The bridge version the capability bits were allocated against. */
static void test_version_matches_schema(void)
{
    const jint api = Java_org_ffmpeg_FFMpegNative_getBridgeApiVersion(NULL, NULL);

    /* 1.3: the release that appended the two snapshot entry points. */
    CHECK(api == 0x00010003);
    CHECK(FF_JNI_AUDIO_FORMAT_SCHEMA_VERSION == 1);
}

void audio_snapshot_run(void)
{
    test_schema_constants();
    test_native_mono_stereo_5_1_7_1();
    test_high_mask_bits_survive();
    test_unspec_vs_custom();
    test_custom_unlocatable_channels();
    test_ambisonic();
    test_s32_with_24_raw_bits();
    test_missing_fields_stay_unknown();
    test_frame_snapshot();
    test_frame_zero_samples_is_defined();
    test_frame_does_not_inherit_from_previous_stream_query();
    test_frame_unspec_not_normalised();
    test_query_does_not_modify_source();
    test_no_state_between_sources();
    test_success_overwrites_every_slot();
    test_short_info_is_einval();
    test_short_ids_is_enospc();
    test_channel_count_ceiling();
    test_invalid_arguments();
    test_publish_exception_is_reported();
    test_capability_bits();
    test_version_matches_schema();

    printf("audio format snapshot: %d checks, %d failures\n",
           g_checks, g_failures);
    fftest_add_checks(g_checks, g_failures);
}
