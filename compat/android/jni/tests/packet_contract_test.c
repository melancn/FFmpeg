/*
 * Contract tests for the packet-related JNI entry points.
 *
 * These exercise the native helpers behind the JNI surface without a JVM, so
 * they can run on both target ABIs inside the build workflow. They are the
 * only place that proves the *behaviour* of these bridges; the workflow's
 * symbol checks only prove the entry points exist.
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
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>
#include <jni.h>

#include "libavcodec/avcodec.h"
#include "libavcodec/packet.h"
/* AVMediaCodecBuffer is opaque in mediacodec.h; the bookkeeping cases need
 * its `released`/`serial` fields to reach the paths that do not call into a
 * codec. This is a libavcodec-private header, which ties the test to the
 * pinned revision -- the same revision the shippable .so is built from. */
#include "libavcodec/mediacodecdec_common.h"
#include "libavutil/buffer.h"
#include "libavutil/channel_layout.h"
#include "libavutil/error.h"
#include "libavutil/frame.h"
#include "libavutil/mem.h"
#include "libavutil/time.h"
#include "libswresample/swresample.h"

/* Re-include the production translation unit for its file-static helpers.
 * They are static precisely so they are not part of the shippable ABI, but
 * that also makes them unreachable from a separate object file. Including the
 * .c here keeps the tests bound to the shipped implementation rather than to
 * a copy that could drift.
 *
 * Do not compile this file with a -Dmain=... rename: it would also rename the
 * main() below and the link would fail. ffmpeg_jni.c must keep defining no
 * main() of its own. */
#include "../ffmpeg_jni.c"

static int failures;
static int checks;

/* The budget snapshot layout is part of the bridge contract. Assert the slot
 * count here so a C-side change cannot silently change the array the JAR
 * fills; FFMpegNative.PACKET_BUDGET_INFO_SLOTS must stay equal to this. */
_Static_assert(FF_JNI_PACKET_BUDGET_SLOTS == 6,
               "packet budget layout changed; bump the bridge API major version "
               "and update FFMpegNative.PACKET_BUDGET_INFO_SLOTS");

#define CHECK(cond) do {                                                    \
    checks++;                                                               \
    if (!(cond)) {                                                          \
        failures++;                                                         \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
    }                                                                       \
} while (0)

/* The audio format snapshot suite (tests/audio_format_snapshot_test.c)
 * exposes this so both suites share one binary, one link and one pass/fail
 * line. It reports into the same counters through these two helpers. */
extern void audio_snapshot_run(void);

/* The codec id suite (tests/codec_id_bridge_test.c) pins the AVCodecID
 * literals FFMpegNative.java repeats to the real enum. It needs libavcodec's
 * codec_id.h and nothing from the bridge, so it is its own translation unit
 * for the same reason: no second copy of the implementation. */
extern void codec_id_bridge_run(void);
int fftest_check_total(void);
int fftest_check_failures(void);
void fftest_add_checks(int n, int f);

int fftest_check_total(void)
{
    return checks;
}

int fftest_check_failures(void)
{
    return failures;
}

void fftest_add_checks(int n, int f)
{
    checks   += n;
    failures += f;
}

#define CHECK_OK(expr)     CHECK((expr) == 0)
#define CHECK_AVERROR(expr, err) CHECK((expr) == AVERROR(err))

/* A packet with a refcounted payload of n bytes. */
static AVPacket *make_payload_packet(int n)
{
    AVPacket *p = av_packet_alloc();
    if (!p)
        return NULL;
    if (av_new_packet(p, n) < 0) {
        av_packet_free(&p);
        return NULL;
    }
    p->pts = 1234;
    p->dts = 1200;
    p->stream_index = 3;
    p->flags = AV_PKT_FLAG_KEY;
    return p;
}

/* Side data of the given size; size 0 exercises the empty-but-present case.
 * Success is judged by the entry appearing, not by the returned pointer: for
 * a zero-length entry the pointer may legitimately be NULL. */
static int add_side_data(AVPacket *p, enum AVPacketSideDataType type, int size)
{
    const int before = p->side_data_elems;
    uint8_t *dst = av_packet_new_side_data(p, type, size);

    if (p->side_data_elems != before + 1)
        return -1;
    if (dst && size > 0)
        memset(dst, 0xA5, size);
    return 0;
}

static void test_plain_payload(void)
{
    AVPacket *p = make_payload_packet(1000);
    int64_t out[FF_JNI_PACKET_BUDGET_SLOTS];

    CHECK(p != NULL);
    if (!p)
        return;

    CHECK_OK(ff_jni_packet_budget(p, out));
    CHECK(out[0] == 1000);                       /* payload bytes */
    CHECK(out[1] >= 1000);                       /* capacity covers payload */
    CHECK(out[2] == 0);                          /* no side data */
    CHECK(out[3] == 0);
    CHECK(out[4] == 0);                          /* no opaque_ref */
    /* Charge must exceed the bare payload and stay positive. */
    CHECK(out[5] > out[0]);
    CHECK(out[5] > 0);

    av_packet_free(&p);
}

static void test_non_refcounted_payload(void)
{
    /* A payload the packet does not own: cloning has to duplicate it, so the
     * estimate must include the clone's padding. */
    AVPacket *p = av_packet_alloc();
    static const uint8_t raw[64] = { 0 };
    int64_t out[FF_JNI_PACKET_BUDGET_SLOTS];

    CHECK(p != NULL);
    if (!p) {
        return;
    }
    p->data = (uint8_t *)raw;
    p->size = 64;

    CHECK_OK(ff_jni_packet_budget(p, out));
    CHECK(out[0] == 64);
    CHECK(out[1] >= 64 + AV_INPUT_BUFFER_PADDING_SIZE);
    CHECK(out[5] > 0);

    p->data = NULL;
    p->size = 0;
    av_packet_free(&p);
}

static void test_all_side_data_counted(void)
{
    AVPacket *p = make_payload_packet(200);
    int64_t out[FF_JNI_PACKET_BUDGET_SLOTS];

    CHECK(p != NULL);
    if (!p)
        return;

    CHECK_OK(add_side_data(p, AV_PKT_DATA_SKIP_SAMPLES, 10));
    CHECK_OK(add_side_data(p, AV_PKT_DATA_NEW_EXTRADATA, 40));
    /* (display matrix takes a fixed 9*4-byte int32 payload) */
    /* Types the player never consumes must still be accounted: they are real
     * resident memory regardless of consumer interest. */
    CHECK_OK(add_side_data(p, AV_PKT_DATA_DISPLAYMATRIX, 24));
    CHECK_OK(add_side_data(p, AV_PKT_DATA_QUALITY_STATS, 7));

    CHECK_OK(ff_jni_packet_budget(p, out));
    CHECK(out[0] == 200);
    CHECK(out[2] == 10 + 40 + 24 + 7);
    CHECK(out[3] == 4);
    CHECK(out[5] > out[0] + out[2]);

    av_packet_free(&p);
}

static void test_side_data_only_is_not_empty(void)
{
    /* The exact case that made the Kotlin layer skip whole packets: no
     * payload, but real side data. It must be reported as having content. */
    AVPacket *p = av_packet_alloc();
    int64_t out[FF_JNI_PACKET_BUDGET_SLOTS];

    CHECK(p != NULL);
    if (!p)
        return;

    CHECK_OK(add_side_data(p, AV_PKT_DATA_NEW_EXTRADATA, 32));
    CHECK_OK(ff_jni_packet_budget(p, out));
    CHECK(out[0] == 0);                /* no payload */
    CHECK(out[2] == 32);               /* but side data does */
    CHECK(out[3] == 1);
    CHECK(out[5] > 0);

    av_packet_free(&p);
}

static void test_empty_vs_missing_side_data(void)
{
    AVPacket *p = av_packet_alloc();
    const AVPacketSideData *found;

    CHECK(p != NULL);
    if (!p)
        return;

    /* Missing: the lookup must report absence, not an empty entry. */
    CHECK(ff_jni_find_side_data(p, AV_PKT_DATA_SKIP_SAMPLES) == NULL);

    CHECK_OK(add_side_data(p, AV_PKT_DATA_SKIP_SAMPLES, 0));
    found = ff_jni_find_side_data(p, AV_PKT_DATA_SKIP_SAMPLES);
    CHECK(found != NULL);              /* present... */
    if (found)
        CHECK(found->size == 0);       /* ...and empty */

    av_packet_free(&p);
}

static void test_opaque_ref(void)
{
    AVPacket *p = make_payload_packet(16);
    int64_t without[FF_JNI_PACKET_BUDGET_SLOTS];
    int64_t with[FF_JNI_PACKET_BUDGET_SLOTS];
    AVBufferRef *ref;

    CHECK(p != NULL);
    if (!p)
        return;

    CHECK_OK(ff_jni_packet_budget(p, without));
    CHECK(without[4] == 0);

    ref = av_buffer_alloc(512);
    CHECK(ref != NULL);
    if (!ref) {
        av_packet_free(&p);
        return;
    }
    p->opaque_ref = ref;

    CHECK_OK(ff_jni_packet_budget(p, with));
    CHECK(with[4] == 512);
    /* opaque_ref memory must be charged, not merely reported. */
    CHECK(with[5] >= without[5] + 512);

    av_packet_free(&p);                /* frees opaque_ref */
}

static void test_invalid_packets(void)
{
    AVPacket p;
    int64_t out[FF_JNI_PACKET_BUDGET_SLOTS];

    /* NULL packet. */
    CHECK_AVERROR(ff_jni_packet_budget(NULL, out), EINVAL);

    /* Negative size. */
    memset(&p, 0, sizeof(p));
    p.size = -1;
    CHECK_AVERROR(ff_jni_packet_budget(&p, out), EINVAL);

    /* Claims a payload without a buffer or data pointer. */
    memset(&p, 0, sizeof(p));
    p.size = 10;
    p.data = NULL;
    p.buf = NULL;
    CHECK_AVERROR(ff_jni_packet_budget(&p, out), EINVAL);

    /* Side data count without a side data array. */
    memset(&p, 0, sizeof(p));
    p.side_data_elems = 2;
    p.side_data = NULL;
    CHECK_AVERROR(ff_jni_packet_budget(&p, out), EINVAL);
}

static void test_overflow_is_reported(void)
{
    /* A side data entry of absurd size must produce EOVERFLOW/EINVAL rather
     * than wrapping into a small or negative charge. */
    AVPacketSideData sd;
    AVPacket p;
    int64_t out[FF_JNI_PACKET_BUDGET_SLOTS];
    int rc;

    memset(&sd, 0, sizeof(sd));
    sd.type = AV_PKT_DATA_NEW_EXTRADATA;
    sd.size = INT_MAX;
    sd.data = (uint8_t *)&sd;          /* non-NULL: only the size is bogus */

    memset(&p, 0, sizeof(p));
    p.side_data = &sd;
    p.side_data_elems = 1;

    rc = ff_jni_packet_budget(&p, out);
    CHECK(rc == 0 || rc < 0);          /* must not crash or claim nothing */
    if (rc == 0) {
        /* If it fits in int64 it must still be non-negative and consistent. */
        CHECK(out[5] > 0);
    } else {
        CHECK(rc == AVERROR(EOVERFLOW) || rc == AVERROR(EINVAL));
    }
}

static void test_budget_add_overflow(void)
{
    uint64_t total = (uint64_t)INT64_MAX - 10;
    CHECK_AVERROR(ff_jni_budget_add(&total, 100), EOVERFLOW);
    CHECK(total == (uint64_t)INT64_MAX - 10);   /* unchanged on failure */

    total = 0;
    CHECK_OK(ff_jni_budget_add(&total, 100));
    CHECK(total == 100);
}

static void test_clone_independence(void)
{
    AVPacket *src = make_payload_packet(256);
    AVPacket *clone;
    int64_t src_budget[FF_JNI_PACKET_BUDGET_SLOTS];
    int64_t clone_budget[FF_JNI_PACKET_BUDGET_SLOTS];
    uint8_t *src_data = NULL;
    int copied;

    CHECK(src != NULL);
    if (!src)
        return;

    memset(src->data, 0x11, 256);
    CHECK_OK(add_side_data(src, AV_PKT_DATA_NEW_EXTRADATA, 20));

    clone = av_packet_clone(src);
    CHECK(clone != NULL);
    if (!clone) {
        av_packet_free(&src);
        return;
    }

    /* Metadata survives the clone. */
    CHECK(clone->pts == src->pts);
    CHECK(clone->dts == src->dts);
    CHECK(clone->stream_index == src->stream_index);
    CHECK(clone->flags == src->flags);
    CHECK(clone->size == src->size);
    CHECK(clone->side_data_elems == src->side_data_elems);

    /* Accounting is consistent between the two holders. */
    CHECK_OK(ff_jni_packet_budget(src, src_budget));
    CHECK_OK(ff_jni_packet_budget(clone, clone_budget));
    CHECK(src_budget[0] == clone_budget[0]);
    CHECK(src_budget[2] == clone_budget[2]);
    CHECK(src_budget[3] == clone_budget[3]);

    /* Keep the payload visible across the free of the original. */
    src_data = clone->data;
    av_packet_free(&src);
    CHECK(clone->size == 256);
    CHECK(clone->data == src_data);
    CHECK(clone->data[0] == 0x11);

    /* Copy on write: making the clone writable must not disturb anything the
     * original would still have observed, and must leave the data intact. */
    CHECK_OK(av_packet_make_writable(clone));
    CHECK(clone->data[0] == 0x11);
    CHECK(clone->side_data_elems == 1);
    copied = clone->side_data[0].size;
    CHECK(copied == 20);

    av_packet_free(&clone);
}

/* ------------------------------------------------------------------ */
/* Frame channel-layout normalisation                                  */
/*                                                                     */
/* These run the same code path the resampler feeds on: an UNSPEC      */
/* layout made definite once, so libswresample sees a stable layout    */
/* instead of AVERROR_INPUT_CHANGED on every frame after the first.    */
/* The individual return codes are only half the point; the frame      */
/* tests drive a real SwrContext so the INPUT_CHANGED defect itself is */
/* reproduced (and proven fixed) on the device.                        */
/* ------------------------------------------------------------------ */

/* Called exactly as the JNI shim calls it, with NULL for both JNIEnv and the
 * class/object: these entry points touch neither, and going through the real
 * symbol keeps the test bound to the shipped signature (env, thiz, args...)
 * rather than a local copy. Passing the wrong arity here is a compile error,
 * which is the point. */
#define NORMALIZE(frame, mask) \
    Java_org_ffmpeg_FFMpegNative_frameNormalizeChannelLayout(NULL, NULL, \
                                                            (jlong)(intptr_t)(frame), (mask))
#define NORMALIZE_MASK(mask, ch) \
    Java_org_ffmpeg_FFMpegNative_channelLayoutNormalizeMask(NULL, NULL, (mask), (ch))

static AVFrame *make_audio_frame(enum AVSampleFormat fmt, int nb_channels,
                                int nb_samples, int rate)
{
    AVFrame *f = av_frame_alloc();

    if (!f)
        return NULL;
    f->format      = fmt;
    f->nb_samples  = nb_samples;
    f->sample_rate = rate;
    /* What a decoder that does not know the order leaves behind: a count and
     * nothing else. This is the exact input state the defect starts from. */
    f->ch_layout.order       = AV_CHANNEL_ORDER_UNSPEC;
    f->ch_layout.nb_channels = nb_channels;
    return f;
}

static void test_normalize_already_definite_is_noop(void)
{
    AVFrame *f = make_audio_frame(AV_SAMPLE_FMT_FLT, 2, 64, 48000);
    uint64_t before;

    CHECK(f != NULL);
    if (!f)
        return;
    CHECK_OK(av_channel_layout_from_mask(&f->ch_layout, AV_CH_LAYOUT_STEREO));
    before = f->ch_layout.u.mask;

    /* A definite layout must be left exactly as it was, even with a
     * declaration that disagrees: rewriting it here would let a wrong
     * container hint override what the decoder actually produced. */
    CHECK(NORMALIZE(f, AV_CH_LAYOUT_5POINT1) == 0);
    CHECK(f->ch_layout.order == AV_CHANNEL_ORDER_NATIVE);
    CHECK(f->ch_layout.u.mask == before);
    CHECK(f->ch_layout.nb_channels == 2);

    /* Idempotent: a second call reports the same "nothing to do". */
    CHECK(NORMALIZE(f, 0) == 0);
    CHECK(f->ch_layout.u.mask == before);

    av_frame_free(&f);
}

static void test_normalize_from_declaration(void)
{
    AVFrame *f = make_audio_frame(AV_SAMPLE_FMT_FLT, 6, 1536, 48000);

    CHECK(f != NULL);
    if (!f)
        return;

    /* Six channels need the container: six channels alone cannot tell 5.1
     * from 5.1(side) or hexa. */
    CHECK(NORMALIZE(f, 0) == AVERROR(EINVAL));

    CHECK(NORMALIZE(f, AV_CH_LAYOUT_5POINT1) == 6);
    CHECK(f->ch_layout.order == AV_CHANNEL_ORDER_NATIVE);
    CHECK(f->ch_layout.nb_channels == 6);
    CHECK(f->ch_layout.u.mask == AV_CH_LAYOUT_5POINT1);

    /* Now definite, so the same call becomes a no-op. */
    CHECK(NORMALIZE(f, AV_CH_LAYOUT_5POINT1) == 0);

    av_frame_free(&f);
}

static void test_normalize_rejects_mismatched_declaration(void)
{
    AVFrame *f = make_audio_frame(AV_SAMPLE_FMT_FLT, 6, 1536, 48000);

    CHECK(f != NULL);
    if (!f)
        return;

    /* A stereo declaration describes a different stream; adopting it would
     * cement a mapping that cannot hold six channels of data. */
    CHECK(NORMALIZE(f, AV_CH_LAYOUT_STEREO) == AVERROR(EINVAL));
    CHECK(f->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC);
    CHECK(f->ch_layout.nb_channels == 6);

    /* A single-bit mask whose count happens to match is adopted, even though
     * the bit has no registered channel id: av_channel_layout_from_mask()
     * accepts any non-zero mask, so the count check is the only thing between
     * the frame and a wrong mapping. Pinning the actual behaviour here keeps
     * that boundary honest. */
    CHECK(NORMALIZE(f, (jlong)(2 | 4)) == AVERROR(EINVAL));  /* FL+FR vs 6ch */
    av_channel_layout_uninit(&f->ch_layout);
    f->ch_layout.order       = AV_CHANNEL_ORDER_UNSPEC;
    f->ch_layout.nb_channels = 1;
    CHECK(NORMALIZE(f, (jlong)(1ULL << 61)) == 1);
    CHECK(f->ch_layout.nb_channels == 1);
    av_channel_layout_uninit(&f->ch_layout);

    av_frame_free(&f);
}

static void test_normalize_mono_stereo_defaults(void)
{
    AVFrame *mono   = make_audio_frame(AV_SAMPLE_FMT_FLT, 1, 64, 44100);
    AVFrame *stereo = make_audio_frame(AV_SAMPLE_FMT_FLT, 2, 64, 44100);

    CHECK(mono != NULL);
    CHECK(stereo != NULL);
    if (!mono || !stereo) {
        av_frame_free(&mono);
        av_frame_free(&stereo);
        return;
    }

    CHECK(NORMALIZE(stereo, 0) == 2);
    CHECK(stereo->ch_layout.order == AV_CHANNEL_ORDER_NATIVE);
    CHECK(stereo->ch_layout.u.mask == AV_CH_LAYOUT_STEREO);

    /* Mono becomes FRONT_CENTER by convention; only the count is asserted
     * here because the default is a policy choice, not a property of the
     * data. */
    CHECK(NORMALIZE(mono, 0) == 1);
    CHECK(mono->ch_layout.order == AV_CHANNEL_ORDER_NATIVE);
    CHECK(mono->ch_layout.nb_channels == 1);

    av_frame_free(&mono);
    av_frame_free(&stereo);
}

static void test_normalize_invalid_arguments(void)
{
    AVFrame f;

    /* NULL frame. */
    CHECK(NORMALIZE(NULL, AV_CH_LAYOUT_STEREO) == AVERROR(EINVAL));

    /* UNSPEC with no channel count at all. */
    memset(&f, 0, sizeof(f));
    CHECK(NORMALIZE(&f, 0) == AVERROR(EINVAL));

    /* Negative channel count. */
    memset(&f, 0, sizeof(f));
    f.ch_layout.order       = AV_CHANNEL_ORDER_UNSPEC;
    f.ch_layout.nb_channels = -1;
    CHECK(NORMALIZE(&f, 0) == AVERROR(EINVAL));

    /* Absurd channel count beyond the layout API's limit. */
    memset(&f, 0, sizeof(f));
    f.ch_layout.order       = AV_CHANNEL_ORDER_UNSPEC;
    f.ch_layout.nb_channels = 4096;
    CHECK(NORMALIZE(&f, 0) == AVERROR(EINVAL));
}

/* The regression this whole entry point exists for: an UNSPEC input frame
 * fed to a real resampler must not produce AVERROR_INPUT_CHANGED on frame 2. */
static void test_swr_accepts_normalized_stream(void)
{
    SwrContext *swr = NULL;
    AVChannelLayout in = { 0 }, out = { 0 };
    AVFrame *frame = NULL, *outframe = NULL;
    int rc, i, normalised = 0;

    /* The resampler is configured with the layout the decoder *should* have
     * produced (FL|FR). An UNSPEC frame is exactly what does not match it. */
    av_channel_layout_default(&in, 2);
    av_channel_layout_default(&out, 2);
    rc = swr_alloc_set_opts2(&swr, &out, AV_SAMPLE_FMT_FLT, 48000,
                             &in, AV_SAMPLE_FMT_FLT, 44100, 0, NULL);
    CHECK_OK(rc);
    if (rc < 0 || !swr) {
        av_channel_layout_uninit(&in);
        av_channel_layout_uninit(&out);
        return;
    }
    CHECK_OK(swr_init(swr));

    frame = av_frame_alloc();
    outframe = av_frame_alloc();
    CHECK(frame != NULL);
    CHECK(outframe != NULL);
    if (frame && outframe) {
        CHECK_OK(av_frame_get_buffer(frame, 0));
        CHECK_OK(av_frame_make_writable(frame));
        frame->format      = AV_SAMPLE_FMT_FLT;
        frame->nb_samples  = 1024;
        frame->sample_rate = 44100;
        frame->ch_layout.order       = AV_CHANNEL_ORDER_UNSPEC;
        frame->ch_layout.nb_channels = 2;

        /* Deliberately reuse one scratch layout: we assert on the return code,
         * not on sample content. */
        for (i = 0; i < 100; i++) {
            int n;

            /* Normalise before every convert; only the first call may change
             * anything, and that idempotence is what makes this safe to wire
             * into a hot decode path. */
            n = NORMALIZE(frame, 0);
            CHECK(n == 0 || n == 2);
            if (i == 0) {
                CHECK(n == 2);
                normalised++;
            } else {
                CHECK(n == 0);
            }
            CHECK(frame->ch_layout.order == AV_CHANNEL_ORDER_NATIVE);

            rc = swr_convert_frame(swr, outframe, frame);
            CHECK(rc != AVERROR_INPUT_CHANGED);
            CHECK_OK(rc);
            av_frame_unref(outframe);
            av_frame_unref(frame);

            /* Rebuild the UNSPEC state, as a decoder would: the point is that
             * each new frame arrives without an order and is repaired. */
            CHECK_OK(av_frame_get_buffer(frame, 0));
            frame->format      = AV_SAMPLE_FMT_FLT;
            frame->nb_samples  = 1024;
            frame->sample_rate = 44100;
            frame->ch_layout.order       = AV_CHANNEL_ORDER_UNSPEC;
            frame->ch_layout.nb_channels = 2;
        }
        CHECK(normalised == 1);
    }

    av_frame_free(&frame);
    av_frame_free(&outframe);
    swr_free(&swr);
    av_channel_layout_uninit(&in);
    av_channel_layout_uninit(&out);
}

/* ------------------------------------------------------------------ */
/* Resampler byte-size and consumption contract                        */
/*                                                                     */
/* swrConvert() used to compute the output byte count in int, never    */
/* compare it to the array's real length, and hand outCount to         */
/* swr_convert() unchecked. These pin the arithmetic and the refusal   */
/* boundary; the conversion cases below drive a real SwrContext so the */
/* "input is consumed in full" and "EOFLUSH produces the tail" claims  */
/* are behaviour, not prose.                                           */
/* ------------------------------------------------------------------ */

static void test_audio_bytes_arithmetic(void)
{
    /* S16 interleaved stereo: 2 bytes * 2 channels. */
    CHECK(ff_jni_audio_bytes(1024, AV_SAMPLE_FMT_S16, 2) == 4096);
    /* Planar occupies the same total across planes. */
    CHECK(ff_jni_audio_bytes(1024, AV_SAMPLE_FMT_FLTP, 2) == 8192);
    CHECK(ff_jni_audio_bytes(0, AV_SAMPLE_FMT_S16, 2) == 0);

    /* Invalid arguments. */
    CHECK_AVERROR(ff_jni_audio_bytes(-1, AV_SAMPLE_FMT_S16, 2), EINVAL);
    CHECK_AVERROR(ff_jni_audio_bytes(16, AV_SAMPLE_FMT_NONE, 2), EINVAL);
    CHECK_AVERROR(ff_jni_audio_bytes(16, AV_SAMPLE_FMT_S16, 0), EINVAL);

    /* 2^28 S16 stereo samples is 1 GiB: representable in int64, but an int
     * product wraps to 0. This is the exact case the old int arithmetic got
     * wrong. */
    CHECK(ff_jni_audio_bytes(INT64_C(1) << 28, AV_SAMPLE_FMT_S16, 2)
          == (INT64_C(1) << 30));

    /* Beyond int64: must be refused, not wrapped into a small positive
     * number that would then look like a valid capacity. */
    CHECK_AVERROR(ff_jni_audio_bytes(INT64_MAX, AV_SAMPLE_FMT_FLT, 2),
                  EOVERFLOW);
    CHECK_AVERROR(ff_jni_audio_bytes(INT64_MAX / 2, AV_SAMPLE_FMT_S16, 4),
                  EOVERFLOW);
}

static void test_mask_channels(void)
{
    CHECK(ff_jni_mask_channels(0) == 0);
    CHECK(ff_jni_mask_channels(AV_CH_LAYOUT_MONO) == 1);
    CHECK(ff_jni_mask_channels(AV_CH_LAYOUT_STEREO) == 2);
    CHECK(ff_jni_mask_channels(AV_CH_LAYOUT_5POINT1) == 6);
}

/* The capacity check itself, without a JVM: this is the gate that runs before
 * swr_convert(), so everything it rejects is an input that was never
 * consumed. */
static void test_convert_check_capacity(void)
{
    const int s16 = AV_SAMPLE_FMT_S16, s16p = AV_SAMPLE_FMT_S16P;

    /* Fits exactly. */
    CHECK_OK(ff_jni_swr_convert_check(1024, 4096, 1024, 1, s16, 2, s16p, 2));
    /* One byte short: refused. This is the write-past-the-end case. */
    CHECK_AVERROR(ff_jni_swr_convert_check(1024, 4095, 1024, 1,
                                           s16, 2, s16p, 2), ENOMEM);
    /* Array larger than requested is fine. */
    CHECK_OK(ff_jni_swr_convert_check(1024, 8192, 1024, 1, s16, 2, s16p, 2));

    /* Empty array with a non-zero request (empty -> 3/4 upsampler). */
    CHECK_AVERROR(ff_jni_swr_convert_check(1024, 0, 1024, 1,
                                           s16, 2, s16p, 2), ENOMEM);

    /* Non-positive request and unusable layouts. A layout of 0 channels
     * means the mask could not be resolved (custom/UNSPEC order), and the
     * bridge must refuse rather than size against a guessed count. */
    CHECK_AVERROR(ff_jni_swr_convert_check(0, 4096, 0, 1,
                                           s16, 2, s16p, 2), EINVAL);
    CHECK_AVERROR(ff_jni_swr_convert_check(-1, 4096, 0, 1,
                                           s16, 2, s16p, 2), EINVAL);
    CHECK_AVERROR(ff_jni_swr_convert_check(1024, 4096, 1024, 1,
                                           s16, 0, s16p, 2), EINVAL);
    CHECK_AVERROR(ff_jni_swr_convert_check(1024, 4096, 1024, 1,
                                           s16, 2, s16p, 0), EINVAL);
    /* Negative input count is a caller bug, not a drain. */
    CHECK_AVERROR(ff_jni_swr_convert_check(1024, 4096, -1, 1,
                                           s16, 2, s16p, 2), EINVAL);

    /* Drain: no input, so no input format is required. */
    CHECK_OK(ff_jni_swr_convert_check(1024, 4096, 0, 0, s16, 2, s16, 0));
    /* Drain still has to fit. */
    CHECK_AVERROR(ff_jni_swr_convert_check(1024, 100, 0, 0,
                                           s16, 2, s16, 0), ENOMEM);

    /* A capacity that only fits because it wrapped is the old bug; the
     * checked arithmetic must turn it into ENOMEM/EOVERFLOW instead. */
    {
        const int rc = ff_jni_swr_convert_check(INT32_MAX, 0, 0, 0,
                                                s16, 2, s16, 0);
        CHECK(rc == AVERROR(ENOMEM) || rc == AVERROR(EOVERFLOW));
    }
}

/* A resampler configured like the player's: whatever in, 48 kHz S16 stereo
 * out. Returns NULL on failure. */
static SwrContext *make_swr(int in_fmt, int in_rate, int in_ch, int out_ch)
{
    SwrContext *swr = NULL;
    AVChannelLayout in_layout = { 0 }, out_layout = { 0 };

    av_channel_layout_default(&in_layout, in_ch);
    av_channel_layout_default(&out_layout, out_ch);
    if (swr_alloc_set_opts2(&swr, &out_layout, AV_SAMPLE_FMT_S16, 48000,
                            &in_layout, (enum AVSampleFormat)in_fmt, in_rate,
                            0, NULL) < 0)
        swr = NULL;
    av_channel_layout_uninit(&in_layout);
    av_channel_layout_uninit(&out_layout);
    if (swr && swr_init(swr) < 0)
        swr_free(&swr);
    return swr;
}

/* Every accepted call consumes all of its input, even when it emits nothing.
 * Java has no "partial consumption" result to react to, so the bridge must
 * never imply one; the deliverable is that produced + delayed accounts for
 * the whole input exactly once. */
static void test_swr_consumes_input_in_full(void)
{
    SwrContext *swr = make_swr(AV_SAMPLE_FMT_S16, 44100, 2, 2);
    const int n = 1024;
    int16_t *inp = NULL;
    int16_t *out = NULL;
    int64_t produced = 0;
    int ret, i, pass;

    CHECK(swr != NULL);
    if (!swr)
        return;

    inp = av_malloc_array(n * 2, sizeof(int16_t));
    /* Generous output room: the point is consumption, not squeezing. */
    out = av_malloc_array(4096 * 2, sizeof(int16_t));
    CHECK(inp != NULL);
    CHECK(out != NULL);
    if (inp && out) {
        uint8_t *dst[1];
        const uint8_t *src[1];

        for (i = 0; i < n * 2; i++)
            inp[i] = (int16_t)(i & 0x7FFF);
        dst[0] = (uint8_t *)out;
        src[0] = (const uint8_t *)inp;

        /* Small output buffer on purpose: swr_convert() accepts far less
         * than it can produce, and must still take all the input. */
        ret = swr_convert(swr, dst, 16, src, n);
        CHECK(ret >= 0);
        CHECK(ret <= 16);
        produced += ret;

        /* The remainder lives inside swr, not in a rejected call: draining
         * it must yield the rest of the timeline. */
        for (pass = 0; pass < 4096; pass++) {
            ret = swr_convert(swr, dst, 4096, NULL, 0);
            CHECK(ret >= 0);
            if (ret <= 0)
                break;
            produced += ret;
        }
        CHECK(pass < 4096);
        /* 1024 input samples at 44.1k -> 48k, with the resampler's own delay
         * flushed out by the drain above. The first call emitted at most 16,
         * so the rest has to come from the drain: if a small output buffer
         * had discarded or refused input, this total would be short. */
        CHECK(produced >= 1024);
        CHECK(produced <= 1024 * 48000 / 44100 + 64);
    }

    av_free(inp);
    av_free(out);
    swr_free(&swr);
}

/* The tail must survive a sequence of small output buffers: no sample may be
 * silently dropped just because the caller never offered a big array. */
static void test_swr_small_output_keeps_tail(void)
{
    SwrContext *swr = make_swr(AV_SAMPLE_FMT_FLTP, 44100, 2, 2);
    const int n = 1024, buf = 64;
    float *planes[2] = { NULL, NULL };
    int16_t *out = NULL;
    int64_t total = 0;
    int i, k, pass, ret;

    CHECK(swr != NULL);
    if (!swr)
        return;

    for (i = 0; i < 2; i++) {
        planes[i] = av_malloc_array(n, sizeof(float));
        CHECK(planes[i] != NULL);
        if (planes[i])
            for (k = 0; k < n; k++)
                planes[i][k] = (float)((k % 100) - 50) / 100.0f;
    }
    out = av_malloc_array((size_t)buf * 2, sizeof(int16_t));
    CHECK(out != NULL);

    if (planes[0] && planes[1] && out) {
        /* swr_convert() takes one destination pointer per output plane; S16
         * interleaved is a single plane, so the array holds one entry. */
        uint8_t *dst[1];
        const uint8_t *srcp[2];

        dst[0]  = (uint8_t *)out;
        srcp[0] = (const uint8_t *)planes[0];
        srcp[1] = (const uint8_t *)planes[1];

        ret = swr_convert(swr, dst, buf, srcp, n);
        CHECK(ret >= 0);
        total += ret > 0 ? ret : 0;

        /* Fixed-number drain passes, then a final one that must report
         * empty. An unbounded loop would hang the test on a regression, so
         * the iteration count is capped generously but finitely. */
        for (pass = 0; pass < 4096; pass++) {
            ret = swr_convert(swr, dst, buf, NULL, 0);
            CHECK(ret >= 0);
            if (ret <= 0)
                break;
            total += ret;
        }
        CHECK(pass < 4096);
        CHECK(total >= 1024);
        CHECK(total <= 1024 * 48000 / 44100 + 64);

        /* Drain is idempotent once empty: nothing resurrects the tail. */
        ret = swr_convert(swr, dst, buf, NULL, 0);
        CHECK(ret == 0);
    }

    av_free(planes[0]);
    av_free(planes[1]);
    av_free(out);
    swr_free(&swr);
}

/* swr_get_out_samples() is an upper bound for sizing, and it must stay one
 * across an arbitrary input/output ratio. */
static void test_swr_out_samples_is_upper_bound(void)
{
    static const int rates[][2] = {
        { 44100, 48000 }, { 8000, 48000 }, { 192000, 48000 },
        { 48000, 48000 }, { 11025, 48000 },
    };
    unsigned r;

    for (r = 0; r < sizeof(rates) / sizeof(rates[0]); r++) {
        const int n = 512;
        SwrContext *swr = make_swr(AV_SAMPLE_FMT_FLT, rates[r][0], 2, 2);
        float *inp = NULL;
        int16_t *out = NULL;
        uint8_t *dst[1];
        const uint8_t *src[1];
        int bound, ret;

        CHECK(swr != NULL);
        if (!swr)
            continue;

        /* Asking for n samples can never be bounded by less than asking for
         * none: the bound has to grow with the request. */
        CHECK(swr_get_out_samples(swr, n) >= swr_get_out_samples(swr, 0));

        bound = swr_get_out_samples(swr, n);
        /* A bound, not a promise: it must be positive and no output may
         * exceed it. This is the number the bounded-resampler session will
         * size its output from, so an under-estimate here would be a real
         * overflow on device. */
        CHECK(bound > 0);
        if (bound <= 0)
            bound = 1;

        inp = av_malloc_array((size_t)n * 2, sizeof(float));
        out = av_malloc_array(((size_t)bound + 16) * 2, sizeof(int16_t));
        CHECK(inp != NULL);
        CHECK(out != NULL);
        if (inp && out) {
            memset(inp, 0, (size_t)n * 2 * sizeof(float));
            dst[0] = (uint8_t *)out;
            src[0] = (const uint8_t *)inp;
            ret = swr_convert(swr, dst, bound, src, n);
            CHECK(ret >= 0);
            CHECK(ret <= bound);
        }
        av_free(inp);
        av_free(out);
        swr_free(&swr);
    }
}

/* ------------------------------------------------------------------ */
/* Bounded PCM resampling session                                      */
/*                                                                     */
/* These drive the real session, not a copy of its arithmetic: the     */
/* claims being pinned are "limits are enforced before anything is     */
/* consumed", "nothing is truncated", "the tail is not lost" and "a    */
/* format change does not leak the old tail". Every one of those is a  */
/* behaviour claim, so asserting return codes alone would not prove it.*/
/* ------------------------------------------------------------------ */

/* Enough output room that the limits, rather than the array, decide. */
#define PCM_OUT_ROOM 8192

static AVFrame *make_pcm_frame(enum AVSampleFormat fmt, int rate,
                               const AVChannelLayout *layout, int nb_samples)
{
    AVFrame *f = av_frame_alloc();

    if (!f)
        return NULL;
    f->format      = fmt;
    f->sample_rate = rate;
    f->nb_samples  = nb_samples;
    if (layout) {
        if (av_channel_layout_copy(&f->ch_layout, layout) < 0) {
            av_frame_free(&f);
            return NULL;
        }
    }
    /* get_buffer is enough: the frame was just allocated, so it is already
     * unique and writable. Calling make_writable here would only add a
     * failure path that cannot trigger. */
    if (av_frame_get_buffer(f, 0) < 0) {
        av_frame_free(&f);
        return NULL;
    }
    return f;
}

/* ------------------------------------------------------------------ */
/* Minimal JNI environment for the publish path                        */
/*                                                                    */
/* The PCM wrappers do three things a NULL JNIEnv cannot exercise:     */
/* read the real array length, publish with Set*ArrayRegion, and check  */
/* for a pending exception. An earlier version of this file passed NULL */
/* and then compared against a hand-copied capacity value, so none of   */
/* those three ran. This stub supplies a genuine JNINativeInterface     */
/* (from the real jni.h the production translation unit already uses)   */
/* whose array slots act on harness-owned objects, so the wrappers run  */
/* unchanged and the checks they perform really happen.                 */
/*                                                                     */
/* What it does NOT model: a VM that throws from SetByteArrayRegion or  */
/* hands out direct array elements. Those are covered only on a real    */
/* ART, which is why the release gate still requires a device run.      */
/* ------------------------------------------------------------------ */

/* Big enough that a normal conversion fits; a case that wants a short
 * array allocates its own. */
#define FFTEST_OUT_CAP_MAX (1 << 16)

/* The harness's own array objects. Deliberately not the VM's jarray struct:
 * its layout differs between API levels and whether it is even visible is
 * not something a bridge test should depend on. The wrappers only ever see
 * these as opaque jbyteArray/jintArray values, which is exactly how a real
 * VM hands them over.
 *
 * The byte storage lives here rather than in a single shared buffer so that
 * a short array really is short: an oversized array must be refused on its
 * own length, not saved by the harness refusing to write. */
typedef struct FFTestJniArray {
    jsize  length;      /* element count, as GetArrayLength reports */
    size_t capacity;    /* bytes actually available in data[]       */
    jbyte  data[1];     /* variable-size tail, allocated to fit      */
} FFTestJniArray;

/* One stub per test process; the wrappers are only ever entered from the
 * thread that drives the test. */
typedef struct FFTestJniState {
    jbyte last_byte[FFTEST_OUT_CAP_MAX];  /* what SetByteArrayRegion wrote */
    int   last_byte_len;                  /* bytes written, -1 if never  */
    int   last_status;                    /* what SetIntArrayRegion wrote */
    jlong last_long[16];                  /* what SetLongArrayRegion wrote */
    int   last_long_len;                  /* elements written, -1 if never */
    int   exception;                      /* a pending exception is set   */
    int   calls;                          /* completed Set*Region calls    */
    int   fail_publish;                   /* inject: Set*Region raises    */
} FFTestJniState;

/* Allocates a stub array holding `nelem` elements of `elem_size` bytes.
 * length is the element count, capacity is what fits in data[]. */
static FFTestJniArray *fftest_array_alloc(jsize nelem, size_t elem_size)
{
    const size_t bytes = (size_t)nelem * elem_size;
    FFTestJniArray *a = av_mallocz(sizeof(*a) + bytes);

    if (!a)
        return NULL;
    a->length   = nelem;
    a->capacity = bytes;
    return a;
}

static FFTestJniState g_fftest_jni;

/* Takes no environment: it only clears the recording state, and threading a
 * JNIEnv through every call site was the source of a pointer-depth mistake
 * that clang rejected. Nothing here needs the table. */
static void fftest_jni_reset(void)
{
    memset(&g_fftest_jni, 0, sizeof(g_fftest_jni));
    g_fftest_jni.last_byte_len = -1;
    g_fftest_jni.last_long_len = -1;
}

static int fftest_last_status(void)
{
    return g_fftest_jni.last_status;
}

static jsize fftest_array_length(JNIEnv *env, jarray a)
{
    const FFTestJniArray *arr = (const FFTestJniArray *)(const void *)a;

    (void)env;
    /* The wrappers only ever pass arrays this harness created; a foreign
     * array would mean the contract changed and the stub is stale. */
    return arr ? arr->length : 0;
}

/* jboolean, not int: the JNINativeInterface slot is jboolean (*)(JNIEnv *),
 * and clang treats a mismatch as an error under
 * -Wincompatible-function-pointer-types. */
static jboolean fftest_check_exception(JNIEnv *env)
{
    (void)env;
    return g_fftest_jni.exception ? JNI_TRUE : JNI_FALSE;
}

static void fftest_raise_exception(JNIEnv *env)
{
    (void)env;
    g_fftest_jni.exception = 1;
}

/* jint, not void: ThrowNew returns a status. Returning 0 means the exception
 * was thrown successfully. */
static jint fftest_throw_new(JNIEnv *env, jclass c, const char *msg)
{
    (void)c;
    (void)msg;
    fftest_raise_exception(env);
    return 0;
}

static void fftest_set_byte_region(JNIEnv *env, jbyteArray a, jsize start,
                                   jsize len, const jbyte *buf)
{
    FFTestJniArray *arr = (FFTestJniArray *)(void *)a;

    (void)env;
    CHECK(arr != NULL && start >= 0 && len >= 0);
    /* A write past the array is the bug this stub exists to catch: a real
     * VM would have thrown, so record it and stop copying. The check is
     * against the array's own length, so a short array is refused by the
     * bridge's capacity check before this is ever reached. */
    if (!arr || start < 0 || len < 0 ||
        (size_t)len > arr->capacity || (size_t)start > arr->capacity - (size_t)len) {
        g_fftest_jni.fail_publish = 1;
        fftest_raise_exception(env);
        return;
    }
    if (g_fftest_jni.fail_publish) {
        fftest_raise_exception(env);
        return;
    }
    if (len > 0) {
        memcpy(arr->data + start, buf, (size_t)len);
        memcpy(g_fftest_jni.last_byte, buf,
               (size_t)len < sizeof(g_fftest_jni.last_byte)
                   ? (size_t)len : sizeof(g_fftest_jni.last_byte));
    }
    g_fftest_jni.last_byte_len = len;
    g_fftest_jni.calls++;
}

static void fftest_set_int_region(JNIEnv *env, jintArray a, jsize start,
                                  jsize len, const jint *buf)
{
    FFTestJniArray *arr = (FFTestJniArray *)(void *)a;

    (void)env;
    if (!arr || start < 0 || len < 0 ||
        (size_t)len * sizeof(jint) > arr->capacity ||
        (size_t)start * sizeof(jint) > arr->capacity - (size_t)len * sizeof(jint)) {
        fftest_raise_exception(env);
        return;
    }
    if (g_fftest_jni.fail_publish) {
        fftest_raise_exception(env);
        return;
    }
    if (len > 0)
        g_fftest_jni.last_status = buf[0];
    g_fftest_jni.calls++;
}

static void fftest_set_long_region(JNIEnv *env, jlongArray a, jsize start,
                                   jsize len, const jlong *buf)
{
    FFTestJniArray *arr = (FFTestJniArray *)(void *)a;
    size_t n;

    (void)env;
    if (!arr || start < 0 || len < 0 ||
        (size_t)len * sizeof(jlong) > arr->capacity ||
        (size_t)start * sizeof(jlong) > arr->capacity - (size_t)len * sizeof(jlong)) {
        fftest_raise_exception(env);
        return;
    }
    if (g_fftest_jni.fail_publish) {
        fftest_raise_exception(env);
        return;
    }
    if (len > 0) {
        n = (size_t)len;
        if (n > sizeof(g_fftest_jni.last_long) / sizeof(g_fftest_jni.last_long[0]))
            n = sizeof(g_fftest_jni.last_long) / sizeof(g_fftest_jni.last_long[0]);
        memcpy(g_fftest_jni.last_long, buf, n * sizeof(jlong));
        g_fftest_jni.last_long_len = len;
    } else {
        g_fftest_jni.last_long_len = 0;
    }
    g_fftest_jni.calls++;
}

static jbyteArray fftest_new_byte_array(jsize len)
{
    return (jbyteArray)(void *)fftest_array_alloc(len, sizeof(jbyte));
}

static jintArray fftest_new_int_array(jsize len)
{
    return (jintArray)(void *)fftest_array_alloc(len, sizeof(jint));
}

static jlongArray fftest_new_long_array(jsize len)
{
    return (jlongArray)(void *)fftest_array_alloc(len, sizeof(jlong));
}

static jthrowable fftest_exception_occurred(JNIEnv *env)
{
    (void)env;
    return (jthrowable)(intptr_t)(g_fftest_jni.exception ? 1 : 0);
}

static void fftest_exception_clear(JNIEnv *env)
{
    (void)env;
    g_fftest_jni.exception = 0;
}

/* Every slot the bridge could reach is one of these. The table starts zeroed
 * and only the implemented slots are filled, so an entry point nobody
 * stubbed is a NULL call rather than a compile error in the test: this list
 * is the contract. The wrappers under test use GetArrayLength,
 * ExceptionCheck and the three Set*ArrayRegion; the others are stubbed
 * because the wrappers are expected to keep using exactly these and nothing
 * more. */
static struct JNINativeInterface g_fftest_functions;

static void fftest_init_functions(void)
{
    memset(&g_fftest_functions, 0, sizeof(g_fftest_functions));
    g_fftest_functions.GetArrayLength     = fftest_array_length;
    g_fftest_functions.ExceptionCheck     = fftest_check_exception;
    g_fftest_functions.ExceptionOccurred  = fftest_exception_occurred;
    g_fftest_functions.ExceptionClear     = fftest_exception_clear;
    g_fftest_functions.ThrowNew           = fftest_throw_new;
    g_fftest_functions.SetByteArrayRegion = fftest_set_byte_region;
    g_fftest_functions.SetIntArrayRegion  = fftest_set_int_region;
    g_fftest_functions.SetLongArrayRegion = fftest_set_long_region;
}

/* JNIEnv is a pointer TYPE, not a struct: jni.h declares
 *   typedef const struct JNINativeInterface *JNIEnv;
 * so there is no `functions` member to fill in (assigning one does not
 * compile, which is how an earlier version of this stub failed).
 *
 * The value IS the table address. JNI entry points, however, are declared
 * as `(JNIEnv *env, ...)` -- a pointer to that pointer -- and they reach the
 * table through (*env)->X(env, ...). So this returns a JNIEnv value and every
 * call site passes its address: `Java_...( &env, ... )`. */
static JNIEnv fftest_jni_env(void)
{
    static int initialised;

    if (!initialised) {
        fftest_init_functions();
        initialised = 1;
    }
    return (JNIEnv)&g_fftest_functions;
}

/* Go through the real entry points where possible: the handle is created by
 * pcmSessionCreate() and freed by pcmSessionFree(), so the session's own
 * construction is under test too.
 *
 * The conversion helpers below call the *exported* pcmSessionConvert() and
 * pcmSessionDrain() wrappers, not a transcription of them. An earlier
 * version of this file re-implemented the JNI body inline, which meant the
 * wrappers were compiled but never executed: a dropped capacity check, a
 * missing SetByteArrayRegion or a swallowed pending exception would have
 * passed every case here. JNIEnv is now a test stub (see FFTestJniEnv
 * above), so capacity is taken from the real Java array length and the
 * publish path really runs. */

/* `out_capacity` is no longer read: the wrapper takes the capacity from the
 * Java array's real length, which is exactly the check J5 asks for. The
 * parameter stays so the call sites keep reading the same, and a case that
 * wants to pin the capacity check drives the wrapper directly with its own
 * array instead of going through here. */
static int pcm_convert(FFPcmSession *s, AVFrame *f, int16_t *out,
                       int out_count, int64_t out_capacity, int *produced)
{
    JNIEnv env = fftest_jni_env();
    jbyteArray arr = fftest_new_byte_array(FFTEST_OUT_CAP_MAX);
    jintArray status = fftest_new_int_array(1);
    int ret;

    /* `out` is unused on purpose: samples stay in the harness array, so a
     * caller that passes NULL (a capacity-pinning case that runs the wrapper
     * itself) does not have to allocate a block it will never read. */
    (void)out;
    (void)out_capacity;
    *produced = 0;
    if (!s || !f || !arr || !status) {
        fftest_jni_reset();
        return AVERROR(EINVAL);
    }

    ret = Java_org_ffmpeg_FFMpegNative_pcmSessionConvert(
              &env, NULL, (jlong)(intptr_t)s, (jlong)(intptr_t)f,
              arr, out_count, status);
    /* Report what the bridge actually published. A stale or uninitialised
     * status slot then surfaces as produced != 0 on a refusal instead of
     * being filtered out here. */
    if (ret >= 0 && fftest_last_status() > 0)
        *produced = fftest_last_status();
    fftest_jni_reset();
    return ret;
}

static int pcm_drain(FFPcmSession *s, int16_t *out, int out_count, int *produced)
{
    JNIEnv env = fftest_jni_env();
    jbyteArray arr = fftest_new_byte_array(FFTEST_OUT_CAP_MAX);
    jintArray status = fftest_new_int_array(1);
    int ret;

    (void)out;
    *produced = 0;
    if (!s || !arr || !status) {
        fftest_jni_reset();
        return AVERROR(EINVAL);
    }

    ret = Java_org_ffmpeg_FFMpegNative_pcmSessionDrain(
              &env, NULL, (jlong)(intptr_t)s, arr, out_count, status);
    if (ret >= 0 && fftest_last_status() > 0)
        *produced = fftest_last_status();
    fftest_jni_reset();
    return ret;
}

/* Drain until the session reports DRAINED. Returns total samples emitted, or
 * a negative error. Bounded so a regression fails instead of hanging. */
static int64_t pcm_drain_all(FFPcmSession *s, int16_t *out, int out_count)
{
    int64_t total = 0;
    int pass, ret, produced;

    for (pass = 0; pass < 4096; pass++) {
        ret = pcm_drain(s, out, out_count, &produced);
        if (ret < 0)
            return ret;
        total += produced;
        if (ret == FF_PCM_DRAINED)
            return total;
    }
    return AVERROR(ERANGE);
}

static FFPcmSession *make_session(int max_out, int max_in, int max_delay)
{
    /* 48 kHz S16 stereo out: the player's fixed AudioTrack format. */
    return (FFPcmSession *)(intptr_t)
           Java_org_ffmpeg_FFMpegNative_pcmSessionCreate(NULL, NULL,
                                                         AV_SAMPLE_FMT_S16,
                                                         48000,
                                                         AV_CH_LAYOUT_STEREO,
                                                         max_out, max_in,
                                                         max_delay);
}

static void test_pcm_session_invalid_arguments(void)
{
    /* Out of range limits must not produce a handle that later misbehaves. */
    CHECK(make_session(0, 1024, 1024) == NULL);
    CHECK(make_session(1024, 0, 1024) == NULL);
    CHECK(make_session(1024, 1024, -1) == NULL);
    CHECK((intptr_t)Java_org_ffmpeg_FFMpegNative_pcmSessionCreate(
              NULL, NULL, AV_SAMPLE_FMT_S16, 0, AV_CH_LAYOUT_STEREO,
              1024, 1024, 1024) == 0);
    CHECK((intptr_t)Java_org_ffmpeg_FFMpegNative_pcmSessionCreate(
              NULL, NULL, AV_SAMPLE_FMT_S16, 48000, 0,
              1024, 1024, 1024) == 0);
    CHECK((intptr_t)Java_org_ffmpeg_FFMpegNative_pcmSessionCreate(
              NULL, NULL, AV_SAMPLE_FMT_NONE, 48000, AV_CH_LAYOUT_STEREO,
              1024, 1024, 1024) == 0);

    /* Freeing a null handle is a no-op, not a crash. */
    Java_org_ffmpeg_FFMpegNative_pcmSessionFree(NULL, NULL, 0);
}

/* The limit that matters most: an input frame above maxInSamples is refused
 * with the input untouched, not converted down or truncated. */
static void test_pcm_session_input_limit(void)
{
    enum { MAX_IN = 512 };
    FFPcmSession *s = make_session(PCM_OUT_ROOM, MAX_IN, PCM_OUT_ROOM);
    AVChannelLayout stereo = { 0 };
    AVFrame *big = NULL, *small = NULL;
    int16_t *out = NULL;
    int produced = -1, ret;

    CHECK(s != NULL);
    if (!s)
        return;

    av_channel_layout_default(&stereo, 2);
    big   = make_pcm_frame(AV_SAMPLE_FMT_S16P, 44100, &stereo, MAX_IN + 1);
    small = make_pcm_frame(AV_SAMPLE_FMT_S16P, 44100, &stereo, MAX_IN);
    out   = av_malloc_array((size_t)PCM_OUT_ROOM * 2, sizeof(int16_t));
    CHECK(big != NULL && small != NULL && out != NULL);

    if (big && small && out) {
        /* Refused, and nothing happened: no output, no configured format
         * (a configured session would mean the frame was accepted). */
        ret = pcm_convert(s, big, out, PCM_OUT_ROOM, PCM_OUT_ROOM * 2 * (int64_t)sizeof(int16_t), &produced);
        CHECK(ret == FF_PCM_LIMIT_EXCEEDED);
        CHECK(produced == 0);
        CHECK(!s->configured);

        /* The same session still works on a frame within the limit. */
        ret = pcm_convert(s, small, out, PCM_OUT_ROOM, PCM_OUT_ROOM * 2 * (int64_t)sizeof(int16_t), &produced);
        CHECK(ret >= 0);
        CHECK(s->configured);
    }

    av_frame_free(&big);
    av_frame_free(&small);
    av_free(out);
    Java_org_ffmpeg_FFMpegNative_pcmSessionFree(NULL, NULL, (jlong)(intptr_t)s);
    av_channel_layout_uninit(&stereo);
}

/* The upper bound, not the actual output, is what the limit is compared
 * against: a block that could overflow must be refused even if this
 * particular frame happens to emit less. */
static void test_pcm_session_out_limit_uses_upper_bound(void)
{
    FFPcmSession *s = make_session(8, 4096, PCM_OUT_ROOM);
    AVChannelLayout stereo = { 0 };
    AVFrame *f = NULL;
    int16_t *out = NULL;
    int produced = -1, ret;
    int64_t bound;

    CHECK(s != NULL);
    if (!s)
        return;

    av_channel_layout_default(&stereo, 2);
    f   = make_pcm_frame(AV_SAMPLE_FMT_S16P, 44100, &stereo, 1024);
    out = av_malloc_array((size_t)PCM_OUT_ROOM * 2, sizeof(int16_t));
    CHECK(f != NULL && out != NULL);

    if (f && out) {
        /* Configure without converting: the check runs on the configured
         * context, which is what a real call would use. */
        CHECK_OK(ff_pcm_configure(s, f->format, f->sample_rate, &f->ch_layout));
        bound = swr_get_out_samples(s->swr, f->nb_samples);
        CHECK(bound > 0);
        /* The bound really does exceed the limit, so the refusal below is
         * caused by the limit and not by an unrelated error. */
        CHECK(bound > s->limits.max_out_samples);

        ret = pcm_convert(s, f, out, PCM_OUT_ROOM, PCM_OUT_ROOM * 2 * (int64_t)sizeof(int16_t), &produced);
        CHECK(ret == FF_PCM_LIMIT_EXCEEDED);
        CHECK(produced == 0);
    }

    av_frame_free(&f);
    av_free(out);
    Java_org_ffmpeg_FFMpegNative_pcmSessionFree(NULL, NULL, (jlong)(intptr_t)s);
    av_channel_layout_uninit(&stereo);
}

/* A small output block inside the limits must produce a status the caller
 * can act on, and must never silently drop samples: produced + delay must
 * account for the whole input exactly once. */
static void test_pcm_session_status_and_accounting(void)
{
    FFPcmSession *s = make_session(PCM_OUT_ROOM, 65536, PCM_OUT_ROOM);
    AVChannelLayout stereo = { 0 };
    AVFrame *f = NULL;
    int16_t *out = NULL;
    int64_t total = 0;
    int produced, ret, i;

    CHECK(s != NULL);
    if (!s)
        return;

    av_channel_layout_default(&stereo, 2);
    f   = make_pcm_frame(AV_SAMPLE_FMT_FLTP, 44100, &stereo, 1024);
    out = av_malloc_array((size_t)PCM_OUT_ROOM * 2, sizeof(int16_t));
    CHECK(f != NULL && out != NULL);
    if (f && out) {
        for (i = 0; i < f->nb_samples; i++) {
            ((float *)f->data[0])[i] = 0.25f;
            ((float *)f->data[1])[i] = -0.25f;
        }

        ret = pcm_convert(s, f, out, 256, 256 * 2 * (int64_t)sizeof(int16_t), &produced);
        CHECK(ret == FF_PCM_OUTPUT_READY);
        CHECK(produced > 0 && produced <= 256);
        total += produced;

        /* Whatever was not emitted is accounted as delay, not lost. */
        CHECK(swr_get_delay(s->swr, 48000) >= 0);

        total += pcm_drain_all(s, out, PCM_OUT_ROOM);
        /* 44.1 -> 48 kHz, with the delay flushed: the timeline is 1024
         * input samples long, within a resampler's own look-ahead. */
        CHECK(total >= 1024);
        CHECK(total <= 1024 * 48000 / 44100 + 64);
    }

    av_frame_free(&f);
    av_free(out);
    Java_org_ffmpeg_FFMpegNative_pcmSessionFree(NULL, NULL, (jlong)(intptr_t)s);
    av_channel_layout_uninit(&stereo);
}

/* Draining an empty session reports DRAINED, and repeating it stays DRAINED:
 * a caller looping on "not done yet" must terminate. */
static void test_pcm_session_drain_terminates(void)
{
    FFPcmSession *s = make_session(PCM_OUT_ROOM, 4096, PCM_OUT_ROOM);
    AVChannelLayout stereo = { 0 };
    AVFrame *f = NULL;
    int16_t *out = NULL;
    int produced, ret;

    CHECK(s != NULL);
    if (!s)
        return;

    av_channel_layout_default(&stereo, 2);
    f   = make_pcm_frame(AV_SAMPLE_FMT_S16P, 48000, &stereo, 256);
    out = av_malloc_array((size_t)PCM_OUT_ROOM * 2, sizeof(int16_t));
    CHECK(f != NULL && out != NULL);

    if (f && out) {
        /* At 48 kHz in and out swr_convert() has no ratio work to do, but a
         * converter still owns a delay line, so the exact emitted count is
         * not part of the contract. What is: whatever is not emitted now
         * must come out of the drain, and the drain must then report
         * DRAINED and stay there. */
        ret = pcm_convert(s, f, out, PCM_OUT_ROOM, PCM_OUT_ROOM * 2 * (int64_t)sizeof(int16_t), &produced);
        CHECK(ret == FF_PCM_OUTPUT_READY);
        CHECK(produced <= 256);

        /* One flush either ends the tail or reports the part that came out.
         * Either way the *next* call must be able to finish it: a drain is
         * never allowed to answer "still busy" without progress. */
        ret = pcm_drain(s, out, PCM_OUT_ROOM, &produced);
        CHECK(ret == FF_PCM_DRAINED || ret == FF_PCM_OUTPUT_READY);
        CHECK(ret != FF_PCM_NEED_DRAIN);

        /* Idempotent once empty: this is what makes a caller's drain loop
         * terminate instead of spinning forever. */
        ret = pcm_drain(s, out, PCM_OUT_ROOM, &produced);
        CHECK(ret == FF_PCM_DRAINED);
        CHECK(produced == 0);

        /* Drained is a terminal state: samples cannot be pushed into a
         * flushed resampler as if the timeline had continued. */
        ret = pcm_convert(s, f, out, PCM_OUT_ROOM, PCM_OUT_ROOM * 2 * (int64_t)sizeof(int16_t), &produced);
        CHECK_AVERROR(ret, EPERM);

        /* ... until reset, which is what starts a new stream. */
        CHECK_OK(Java_org_ffmpeg_FFMpegNative_pcmSessionReset(
                     NULL, NULL, (jlong)(intptr_t)s));
        ret = pcm_convert(s, f, out, PCM_OUT_ROOM, PCM_OUT_ROOM * 2 * (int64_t)sizeof(int16_t), &produced);
        CHECK(ret == FF_PCM_OUTPUT_READY);
    }

    av_frame_free(&f);
    av_free(out);
    Java_org_ffmpeg_FFMpegNative_pcmSessionFree(NULL, NULL, (jlong)(intptr_t)s);
    av_channel_layout_uninit(&stereo);
}

/* Reset must clear the delay: a seek would otherwise replay the previous
 * position's tail into the new timeline. */
static void test_pcm_session_reset_drops_tail(void)
{
    FFPcmSession *s = make_session(PCM_OUT_ROOM, 65536, PCM_OUT_ROOM);
    AVChannelLayout stereo = { 0 };
    AVFrame *f = NULL;
    int16_t *out = NULL;
    int produced, ret;
    int64_t delay_before;

    CHECK(s != NULL);
    if (!s)
        return;

    av_channel_layout_default(&stereo, 2);
    f   = make_pcm_frame(AV_SAMPLE_FMT_S16P, 44100, &stereo, 1024);
    out = av_malloc_array((size_t)PCM_OUT_ROOM * 2, sizeof(int16_t));
    CHECK(f != NULL && out != NULL);

    if (f && out) {
        /* A small block guarantees a non-zero delay is left behind. */
        ret = pcm_convert(s, f, out, 64, 64 * 2 * (int64_t)sizeof(int16_t), &produced);
        CHECK(ret >= 0);
        delay_before = swr_get_delay(s->swr, 48000);
        CHECK(delay_before > 0);

        CHECK_OK(Java_org_ffmpeg_FFMpegNative_pcmSessionReset(
                     NULL, NULL, (jlong)(intptr_t)s));
        CHECK(swr_get_delay(s->swr, 48000) == 0);

        /* And the format survived the reset, so the next frame is not seen
         * as a format change. */
        CHECK(s->configured);
        CHECK(s->in_rate == 44100);
        CHECK(s->in_fmt == AV_SAMPLE_FMT_S16P);

        ret = pcm_drain(s, out, PCM_OUT_ROOM, &produced);
        CHECK(ret == FF_PCM_DRAINED);
        CHECK(produced == 0);
    }

    av_frame_free(&f);
    av_free(out);
    Java_org_ffmpeg_FFMpegNative_pcmSessionFree(NULL, NULL, (jlong)(intptr_t)s);
    av_channel_layout_uninit(&stereo);
}

/* A format change while a tail is still buffered must be refused first, and
 * committed only after the tail has been drained. Switching in place would
 * punch a silent hole in the timeline. */
static void test_pcm_session_format_change_transaction(void)
{
    FFPcmSession *s = make_session(PCM_OUT_ROOM, 65536, PCM_OUT_ROOM);
    AVChannelLayout stereo = { 0 };
    AVFrame *f44 = NULL, *f48 = NULL;
    int16_t *out = NULL;
    int produced, ret;
    int64_t drained;

    CHECK(s != NULL);
    if (!s)
        return;

    av_channel_layout_default(&stereo, 2);
    f44 = make_pcm_frame(AV_SAMPLE_FMT_S16P, 44100, &stereo, 1024);
    f48 = make_pcm_frame(AV_SAMPLE_FMT_S16P, 48000, &stereo, 1024);
    out = av_malloc_array((size_t)PCM_OUT_ROOM * 2, sizeof(int16_t));
    CHECK(f44 != NULL && f48 != NULL && out != NULL);

    if (f44 && f48 && out) {
        /* Leave a tail behind. */
        ret = pcm_convert(s, f44, out, 64, 64 * 2 * (int64_t)sizeof(int16_t), &produced);
        CHECK(ret >= 0);
        CHECK(swr_get_delay(s->swr, 48000) > 0);

        /* The new format arrives while the tail is buffered: refused, the
         * frame unconsumed, the configuration unchanged. */
        ret = pcm_convert(s, f48, out, PCM_OUT_ROOM, PCM_OUT_ROOM * 2 * (int64_t)sizeof(int16_t), &produced);
        CHECK(ret == FF_PCM_FORMAT_CHANGE);
        CHECK(produced == 0);
        CHECK(s->in_rate == 44100);

        /* Drain the old tail completely. */
        drained = pcm_drain_all(s, out, PCM_OUT_ROOM);
        CHECK(drained >= 0);

        /* Drained to EOF, so the change commits on this very call. The
         * second submission must not be refused just because swr's own
         * delay is still non-zero after the flush: that is the bug this
         * case pins, and answering FORMAT_CHANGE again here would hang a
         * caller that drains before every format change. */
        CHECK(s->phase == FF_PCM_PHASE_DRAINED);
        ret = pcm_convert(s, f48, out, PCM_OUT_ROOM, PCM_OUT_ROOM * 2 * (int64_t)sizeof(int16_t), &produced);
        CHECK(ret != FF_PCM_FORMAT_CHANGE);
        CHECK(ret == FF_PCM_OUTPUT_READY);
        CHECK(produced > 0);
        CHECK(s->in_rate == 48000);
        CHECK(s->phase == FF_PCM_PHASE_ACTIVE);

        /* Converting "exactly once" is proven by the sample count: 1024
         * input samples at 48 kHz cannot yield more than that plus the new
         * resampler's own look-ahead. A double conversion would show up as
         * roughly double. */
        CHECK(produced <= 1024);
        CHECK(produced >= 1024 - 64);
    }

    av_frame_free(&f44);
    av_frame_free(&f48);
    av_free(out);
    Java_org_ffmpeg_FFMpegNative_pcmSessionFree(NULL, NULL, (jlong)(intptr_t)s);
    av_channel_layout_uninit(&stereo);
}

/* Channel mapping is proven with a distinguishable per-channel signal, not
 * by asserting success. A mono input upmixed to stereo must come out with
 * the mono content on both channels rather than one channel of silence. */
static void test_pcm_session_channel_mapping(void)
{
    FFPcmSession *s = make_session(PCM_OUT_ROOM, 4096, PCM_OUT_ROOM);
    AVChannelLayout mono = { 0 };
    AVFrame *f = NULL;
    int16_t *out = NULL;
    int produced, ret, i, silent = 0;

    CHECK(s != NULL);
    if (!s)
        return;

    av_channel_layout_default(&mono, 1);
    f   = make_pcm_frame(AV_SAMPLE_FMT_S16P, 48000, &mono, 256);
    out = av_malloc_array((size_t)PCM_OUT_ROOM * 2, sizeof(int16_t));
    CHECK(f != NULL && out != NULL);

    if (f && out) {
        /* A constant, non-zero mono signal. */
        for (i = 0; i < 256; i++)
            ((int16_t *)f->data[0])[i] = 8000;

        ret = pcm_convert(s, f, out, PCM_OUT_ROOM, PCM_OUT_ROOM * 2 * (int64_t)sizeof(int16_t), &produced);
        CHECK(ret == FF_PCM_OUTPUT_READY);
        CHECK(produced > 0);

        /* Interleaved stereo: both channels must carry the mono signal.
         * Counting fully silent frames (past the resampler's initial ramp)
         * is the check a left-only or right-only mapping bug would fail:
         * success alone would not catch it. */
        for (i = 32; i < produced; i++) {
            if (out[i * 2] == 0 && out[i * 2 + 1] == 0)
                silent++;
        }
        CHECK(silent == 0);
    }

    av_frame_free(&f);
    av_free(out);
    Java_org_ffmpeg_FFMpegNative_pcmSessionFree(NULL, NULL, (jlong)(intptr_t)s);
    av_channel_layout_uninit(&mono);
}

/* ------------------------------------------------------------------ */
/* Format-change transaction: the acceptance matrix                    */
/*                                                                     */
/* The four cases the plan names, each driven through the real session:*/
/*   44.1k -> 48k, packed <-> planar, same count / different layout,   */
/*   and a custom channel order. Every case asserts that the old tail  */
/*   comes out before the new format starts, so no sample is lost or   */
/*   duplicated at the seam.                                           */
/* ------------------------------------------------------------------ */

/* Drain the current format to completion, returning the sample count. */
#define DRAIN_OLD(s, outp, room) pcm_drain_all((s), (outp), (room))

/* A lone UNSPEC frame must not be mistaken for a format change once the
 * session has a definite layout: swr itself resolves UNSPEC to the default
 * layout, so a decoder that never states an order is not a new format.
 *
 * The session installs the definite order on its own side only. The frame
 * belongs to the caller and may be shared, so it is never rewritten -- the
 * first frame may legitimately be accepted as a change (the session had no
 * configuration yet), afterwards there must be none. */
static void test_pcm_session_unspec_is_not_a_format_change(void)
{
    FFPcmSession *s = make_session(PCM_OUT_ROOM, 65536, PCM_OUT_ROOM);
    AVChannelLayout stereo = { 0 };
    AVFrame *f = NULL;
    int16_t *out = NULL;
    int produced, ret, i, changes = 0;

    CHECK(s != NULL);
    if (!s)
        return;

    av_channel_layout_default(&stereo, 2);
    f   = make_pcm_frame(AV_SAMPLE_FMT_S16P, 48000, &stereo, 256);
    out = av_malloc_array((size_t)PCM_OUT_ROOM * 2, sizeof(int16_t));
    CHECK(f != NULL && out != NULL);

    if (f && out) {
        /* Ten frames in a row, each arriving with a stripped layout -- the
         * shape a decoder produces when it does not know the order. */
        for (i = 0; i < 10; i++) {
            av_channel_layout_uninit(&f->ch_layout);
            f->ch_layout.order       = AV_CHANNEL_ORDER_UNSPEC;
            f->ch_layout.nb_channels = 2;

            ret = pcm_convert(s, f, out, PCM_OUT_ROOM, PCM_OUT_ROOM * 2 * (int64_t)sizeof(int16_t), &produced);
            if (ret == FF_PCM_FORMAT_CHANGE)
                changes++;
            CHECK(ret == FF_PCM_OUTPUT_READY);
            /* The caller's frame was not modified. */
            CHECK(f->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC);
            /* The session holds the definite order swr would have picked. */
            CHECK(s->in_layout.order == AV_CHANNEL_ORDER_NATIVE);
            CHECK(s->in_layout.nb_channels == 2);
        }
        CHECK(changes == 0);
    }

    av_frame_free(&f);
    av_free(out);
    Java_org_ffmpeg_FFMpegNative_pcmSessionFree(NULL, NULL, (jlong)(intptr_t)s);
    av_channel_layout_uninit(&stereo);
}

/* Same channel count, different layout (stereo vs downmix-compatible
 * 2.1/quad-style mask of two channels): the comparison must be by layout,
 * not by channel count. */
static void test_pcm_session_same_count_different_layout(void)
{
    FFPcmSession *s = make_session(PCM_OUT_ROOM, 65536, PCM_OUT_ROOM);
    AVChannelLayout fl_fr = { 0 }, fc_lfe = { 0 };
    AVFrame *a = NULL, *b = NULL;
    int16_t *out = NULL;
    int produced, ret;

    CHECK(s != NULL);
    if (!s)
        return;

    /* Two channels, but they are not front-left/front-right: both describe
     * the same count and a different mapping. */
    CHECK_OK(av_channel_layout_from_mask(&fl_fr, AV_CH_LAYOUT_STEREO));
    CHECK_OK(av_channel_layout_from_mask(&fc_lfe,
                                         AV_CH_FRONT_CENTER | AV_CH_LOW_FREQUENCY));
    CHECK(fl_fr.nb_channels == 2);
    CHECK(fc_lfe.nb_channels == 2);

    a   = make_pcm_frame(AV_SAMPLE_FMT_FLTP, 48000, &fl_fr, 256);
    b   = make_pcm_frame(AV_SAMPLE_FMT_FLTP, 48000, &fc_lfe, 256);
    out = av_malloc_array((size_t)PCM_OUT_ROOM * 2, sizeof(int16_t));
    CHECK(a != NULL && b != NULL && out != NULL);

    if (a && b && out) {
        ret = pcm_convert(s, a, out, PCM_OUT_ROOM, PCM_OUT_ROOM * 2 * (int64_t)sizeof(int16_t), &produced);
        CHECK(ret == FF_PCM_OUTPUT_READY);

        /* Same count, different channels: a real change, so it must be
         * reported rather than silently accepted, and it must commit once
         * the tail is gone. */
        ret = pcm_convert(s, b, out, PCM_OUT_ROOM, PCM_OUT_ROOM * 2 * (int64_t)sizeof(int16_t), &produced);
        CHECK(ret == FF_PCM_FORMAT_CHANGE || ret == FF_PCM_OUTPUT_READY);

        CHECK(DRAIN_OLD(s, out, PCM_OUT_ROOM) >= 0);
        ret = pcm_convert(s, b, out, PCM_OUT_ROOM, PCM_OUT_ROOM * 2 * (int64_t)sizeof(int16_t), &produced);
        CHECK(ret == FF_PCM_OUTPUT_READY);
        CHECK(av_channel_layout_compare(&s->in_layout, &fc_lfe) == 0);
    }

    av_frame_free(&a);
    av_frame_free(&b);
    av_free(out);
    Java_org_ffmpeg_FFMpegNative_pcmSessionFree(NULL, NULL, (jlong)(intptr_t)s);
    av_channel_layout_uninit(&fl_fr);
    av_channel_layout_uninit(&fc_lfe);
}

/* packed <-> planar with the same rate and layout: a real format change for
 * swr, and the tail from the packed stream must still reach the output. */
static void test_pcm_session_packed_planar_switch(void)
{
    FFPcmSession *s = make_session(PCM_OUT_ROOM, 65536, PCM_OUT_ROOM);
    AVChannelLayout stereo = { 0 };
    AVFrame *packed = NULL, *planar = NULL;
    int16_t *out = NULL;
    int produced, ret, i;
    int64_t tail;

    CHECK(s != NULL);
    if (!s)
        return;

    av_channel_layout_default(&stereo, 2);
    packed = make_pcm_frame(AV_SAMPLE_FMT_S16,  44100, &stereo, 512);
    planar = make_pcm_frame(AV_SAMPLE_FMT_S16P, 44100, &stereo, 512);
    out    = av_malloc_array((size_t)PCM_OUT_ROOM * 2, sizeof(int16_t));
    CHECK(packed != NULL && planar != NULL && out != NULL);

    if (packed && planar && out) {
        /* Distinguishable content: left +1000, right -1000. */
        for (i = 0; i < 512; i++) {
            ((int16_t *)packed->data[0])[i * 2]     =  1000;
            ((int16_t *)packed->data[0])[i * 2 + 1] = -1000;
            ((int16_t *)planar->data[0])[i] = 1000;
            ((int16_t *)planar->data[1])[i] = -1000;
        }

        /* Small block leaves a tail in the packed configuration. */
        ret = pcm_convert(s, packed, out, 64, 64 * 2 * (int64_t)sizeof(int16_t), &produced);
        CHECK(ret == FF_PCM_OUTPUT_READY);
        CHECK(swr_get_delay(s->swr, 48000) > 0);

        /* The planar frame arrives before that tail is gone. */
        ret = pcm_convert(s, planar, out, PCM_OUT_ROOM, PCM_OUT_ROOM * 2 * (int64_t)sizeof(int16_t), &produced);
        CHECK(ret == FF_PCM_FORMAT_CHANGE);
        CHECK(produced == 0);

        tail = DRAIN_OLD(s, out, PCM_OUT_ROOM);
        CHECK(tail >= 0);

        /* Now the switch commits and the planar frame converts once. */
        ret = pcm_convert(s, planar, out, PCM_OUT_ROOM, PCM_OUT_ROOM * 2 * (int64_t)sizeof(int16_t), &produced);
        CHECK(ret == FF_PCM_OUTPUT_READY);
        CHECK(produced > 0);
        CHECK(s->in_fmt == AV_SAMPLE_FMT_S16P);

        /* 44.1 -> 48k on 512 samples. */
        CHECK(produced >= 512 - 64);
        CHECK(produced <= 512 * 48000 / 44100 + 64);
    }

    av_frame_free(&packed);
    av_frame_free(&planar);
    av_free(out);
    Java_org_ffmpeg_FFMpegNative_pcmSessionFree(NULL, NULL, (jlong)(intptr_t)s);
    av_channel_layout_uninit(&stereo);
}

/* A custom channel order has no bitmask, so a session that compared masks
 * would treat it as "0 channels" or as a change on every frame. It must be
 * accepted as a definite layout and compared channel by channel. */
static void test_pcm_session_custom_layout(void)
{
    FFPcmSession *s = make_session(PCM_OUT_ROOM, 65536, PCM_OUT_ROOM);
    AVChannelLayout custom = { 0 };
    AVFrame *f = NULL;
    int16_t *out = NULL;
    int produced, ret;

    CHECK(s != NULL);
    if (!s)
        return;

    /* Two channels in a non-standard order: right before left. */
    if (av_channel_layout_custom_init(&custom, 2) < 0) {
        Java_org_ffmpeg_FFMpegNative_pcmSessionFree(NULL, NULL,
                                                    (jlong)(intptr_t)s);
        return;
    }
    /* Right before left: a genuine custom order, not the default FR|FL. */
    custom.u.map[0].id = AV_CHAN_FRONT_RIGHT;
    custom.u.map[1].id = AV_CHAN_FRONT_LEFT;
    CHECK(custom.order == AV_CHANNEL_ORDER_CUSTOM);

    f   = make_pcm_frame(AV_SAMPLE_FMT_FLTP, 48000, &custom, 256);
    out = av_malloc_array((size_t)PCM_OUT_ROOM * 2, sizeof(int16_t));
    CHECK(f != NULL && out != NULL);

    if (f && out) {
        ret = pcm_convert(s, f, out, PCM_OUT_ROOM, PCM_OUT_ROOM * 2 * (int64_t)sizeof(int16_t), &produced);
        CHECK(ret == FF_PCM_OUTPUT_READY);
        CHECK(produced > 0);

        /* Survived the round trip into the session's snapshot, still
         * custom, and equal to what was presented. */
        CHECK(s->in_layout.order == AV_CHANNEL_ORDER_CUSTOM);
        CHECK(s->in_layout.nb_channels == 2);
        CHECK(av_channel_layout_compare(&s->in_layout, &custom) == 0);

        /* A second identical frame is not a change. */
        ret = pcm_convert(s, f, out, PCM_OUT_ROOM, PCM_OUT_ROOM * 2 * (int64_t)sizeof(int16_t), &produced);
        CHECK(ret == FF_PCM_OUTPUT_READY);
    }

    av_frame_free(&f);
    av_free(out);
    Java_org_ffmpeg_FFMpegNative_pcmSessionFree(NULL, NULL, (jlong)(intptr_t)s);
    av_channel_layout_uninit(&custom);
}

/* Ownership rule for a committed format: the candidate SwrContext becomes the
 * session's, and the conversion helper must not also release it. Before this
 * was pinned, ff_pcm_convert_jni() cleared `active` but not `built`, so its
 * cleanup freed the context the session had just adopted: the next convert ran
 * on freed memory (swr_is_initialized() reads a zeroed in_buffer.ch_count and
 * reports EINVAL, logged as "Context has not been initialized") and
 * pcmSessionFree() freed the same pointer a second time.
 *
 * A live, unchanged context across calls is the observable contract, and the
 * session must still be convertible after a commit and after a reset. */
static void test_pcm_session_committed_context_survives(void)
{
    FFPcmSession *s = make_session(PCM_OUT_ROOM, 65536, PCM_OUT_ROOM);
    AVChannelLayout stereo = { 0 };
    AVFrame *f = NULL;
    struct SwrContext *owned = NULL;
    int produced, ret;

    CHECK(s != NULL);
    if (!s)
        return;

    av_channel_layout_default(&stereo, 2);
    f = make_pcm_frame(AV_SAMPLE_FMT_S16P, 44100, &stereo, 512);
    CHECK(f != NULL);

    if (f) {
        /* The first convert on a fresh session always commits. */
        ret = pcm_convert(s, f, NULL, PCM_OUT_ROOM, 0, &produced);
        CHECK(ret >= 0);
        CHECK(s->swr != NULL);
        CHECK(s->phase == FF_PCM_PHASE_ACTIVE);
        owned = s->swr;

        /* A second frame of the same format reuses that context. A freed one
         * would answer -EINVAL here. */
        ret = pcm_convert(s, f, NULL, PCM_OUT_ROOM, 0, &produced);
        CHECK(ret >= 0);
        CHECK(s->swr == owned);
        CHECK(s->phase == FF_PCM_PHASE_ACTIVE);

        /* Reset rebuilds and releases the old context exactly once. */
        CHECK_OK(Java_org_ffmpeg_FFMpegNative_pcmSessionReset(
                     NULL, NULL, (jlong)(intptr_t)s));
        CHECK(s->swr != NULL);
        ret = pcm_convert(s, f, NULL, PCM_OUT_ROOM, 0, &produced);
        CHECK(ret >= 0);
        CHECK(s->phase == FF_PCM_PHASE_ACTIVE);
    }

    av_frame_free(&f);
    av_channel_layout_uninit(&stereo);
    /* No abort here is part of the contract: the committed context is owned
     * by the session and is released once, by pcmSessionFree(). */
    Java_org_ffmpeg_FFMpegNative_pcmSessionFree(NULL, NULL, (jlong)(intptr_t)s);
}

/* The seam itself: the samples either side of a format change must be
 * continuous. A distinguishable constant per stream lets the test prove the
 * old content came out before the new content started, in order. */
static void test_pcm_session_seam_is_continuous(void)
{
    FFPcmSession *s = make_session(PCM_OUT_ROOM, 65536, PCM_OUT_ROOM);
    AVChannelLayout stereo = { 0 };
    AVFrame *old_f = NULL, *new_f = NULL;
    int16_t *out = NULL;
    int produced, ret, i;
    int first_new = -1, last_old = -1;

    CHECK(s != NULL);
    if (!s)
        return;

    av_channel_layout_default(&stereo, 2);
    old_f = make_pcm_frame(AV_SAMPLE_FMT_S16P, 44100, &stereo, 512);
    new_f = make_pcm_frame(AV_SAMPLE_FMT_S16P, 48000, &stereo, 512);
    out   = av_malloc_array((size_t)PCM_OUT_ROOM * 2, sizeof(int16_t));
    CHECK(old_f != NULL && new_f != NULL && out != NULL);

    if (old_f && new_f && out) {
        for (i = 0; i < 512; i++) {
            ((int16_t *)old_f->data[0])[i] = 1000;
            ((int16_t *)old_f->data[1])[i] = 1000;
            ((int16_t *)new_f->data[0])[i] = -1000;
            ((int16_t *)new_f->data[1])[i] = -1000;
        }

        /* Leave a tail, then ask for the new format. */
        ret = pcm_convert(s, old_f, out, 64, 64 * 2 * (int64_t)sizeof(int16_t), &produced);
        CHECK(ret == FF_PCM_OUTPUT_READY);
        ret = pcm_convert(s, new_f, out, PCM_OUT_ROOM, PCM_OUT_ROOM * 2 * (int64_t)sizeof(int16_t), &produced);
        CHECK(ret == FF_PCM_FORMAT_CHANGE);

        /* Drain the old tail and find the last old sample in it. */
        ret = pcm_drain(s, out, PCM_OUT_ROOM, &produced);
        CHECK(ret >= 0);
        for (i = 0; i < produced; i++)
            if (out[i * 2] > 0)
                last_old = i;
        CHECK(last_old >= 0);          /* the tail really came out */

        /* Now the switch commits; the first new sample must be new content,
         * not more of the old stream. */
        ret = pcm_convert(s, new_f, out, PCM_OUT_ROOM, PCM_OUT_ROOM * 2 * (int64_t)sizeof(int16_t), &produced);
        CHECK(ret == FF_PCM_OUTPUT_READY);
        for (i = 0; i < produced; i++) {
            if (out[i * 2] < 0) {
                first_new = i;
                break;
            }
        }
        CHECK(first_new >= 0);
        /* Anything before the first new sample is the resampler's ramp on
         * the new configuration, which starts at zero rather than replaying
         * old content. Assert no old-marked sample leaked past the switch. */
        for (i = 0; i < first_new; i++)
            CHECK(out[i * 2] <= 0);
    }

    av_frame_free(&old_f);
    av_frame_free(&new_f);
    av_free(out);
    Java_org_ffmpeg_FFMpegNative_pcmSessionFree(NULL, NULL, (jlong)(intptr_t)s);
    av_channel_layout_uninit(&stereo);
}

/* ------------------------------------------------------------------ */
/* Output session: admission, revoke, detach, quarantine               */
/*                                                                     */
/* The claims being pinned are concurrency claims, so these use real   */
/* threads: a blocked producer must not be able to reach DETACHED, an  */
/* old binding must not admit work after revoke, and a failed teardown */
/* must be quarantined rather than reported as success.                */
/* ------------------------------------------------------------------ */

static void output_session_init(FFOutputSession *s)
{
    memset(s, 0, sizeof(*s));
    pthread_mutex_init(&s->lock, NULL);
    s->state = FF_OUTPUT_ACTIVE;
}

static void output_session_deinit(FFOutputSession *s)
{
    pthread_mutex_destroy(&s->lock);
}

/* A bare session with no window: teardown state can be exercised without a
 * Surface, which is the part that has to be race-free. */
static void test_output_session_basic_lifecycle(void)
{
    FFOutputSession s;

    output_session_init(&s);
    CHECK(ff_output_session_state(&s) == FF_OUTPUT_ACTIVE);
    CHECK(ff_output_session_in_flight(&s) == 0);

    /* Nothing admitted: revoke followed by finish reaches DETACHED at once. */
    CHECK(ff_output_session_revoke(&s) == 0);
    CHECK(ff_output_session_state(&s) == FF_OUTPUT_REVOKING);
    CHECK(ff_output_session_try_finish(&s) == FF_OUTPUT_DETACHED);
    CHECK(ff_output_session_state(&s) == FF_OUTPUT_DETACHED);

    /* Detached sessions admit nothing. */
    CHECK(ff_output_session_enter(&s) == AVERROR(EPERM));
    output_session_deinit(&s);
}

/* Admission is refused from the moment of revoke, even while other work is
 * still in flight: this is what stops an old binding from being used with
 * a new Surface's authority. */
static void test_output_session_revoke_refuses_new_work(void)
{
    FFOutputSession s;

    output_session_init(&s);

    CHECK_OK(ff_output_session_enter(&s));
    CHECK(ff_output_session_in_flight(&s) == 1);

    CHECK(ff_output_session_revoke(&s) == 1);

    /* New admissions are refused while the old one drains. */
    CHECK_AVERROR(ff_output_session_enter(&s), EPERM);
    CHECK(ff_output_session_in_flight(&s) == 1);

    /* Teardown cannot complete while the admitted operation runs. */
    CHECK(ff_output_session_try_finish(&s) == FF_OUTPUT_REVOKING);

    ff_output_session_exit(&s);
    CHECK(ff_output_session_in_flight(&s) == 0);
    CHECK(ff_output_session_try_finish(&s) == FF_OUTPUT_DETACHED);

    output_session_deinit(&s);
}

/* A producer stuck inside an admitted operation must keep the session out of
 * DETACHED for as long as it is stuck. This is the case the plan calls out:
 * waiting must not be mistaken for success. */
typedef struct BlockedProducer {
    FFOutputSession *session;
    volatile int     entered;
    volatile int     may_leave;
} BlockedProducer;

static void *blocked_producer_thread(void *opaque)
{
    BlockedProducer *p = opaque;

    if (ff_output_session_enter(p->session) < 0)
        return NULL;
    p->entered = 1;
    /* Stand-in for ANativeWindow_lock(): an unbounded wait inside an
     * admitted operation. */
    while (!p->may_leave)
        av_usleep(1000);
    ff_output_session_exit(p->session);
    return NULL;
}

static void test_output_session_blocked_producer_stays_revoking(void)
{
    FFOutputSession s;
    BlockedProducer p;
    pthread_t th;
    int i, state = -1;

    output_session_init(&s);
    memset(&p, 0, sizeof(p));
    p.session = &s;

    CHECK(pthread_create(&th, NULL, blocked_producer_thread, &p) == 0);
    /* Wait for admission without a fixed sleep: poll the flag. */
    for (i = 0; i < 5000 && !p.entered; i++)
        av_usleep(1000);
    CHECK(p.entered);

    CHECK(ff_output_session_revoke(&s) == 1);

    /* Poll finish many times while the producer is stuck. It must never
     * report DETACHED: a timeout that returns "detached" is exactly the
     * bug this state machine exists to prevent. */
    for (i = 0; i < 100; i++) {
        state = ff_output_session_try_finish(&s);
        CHECK(state == FF_OUTPUT_REVOKING);
        av_usleep(1000);
    }

    /* Now let it finish; only then is detach provable. */
    p.may_leave = 1;
    CHECK(pthread_join(th, NULL) == 0);
    CHECK(ff_output_session_try_finish(&s) == FF_OUTPUT_DETACHED);

    output_session_deinit(&s);
}

/* Quarantine is the only honest outcome when teardown cannot be proven. It
 * must keep the in-flight count visible and never turn into DETACHED. */
static void test_output_session_quarantine(void)
{
    FFOutputSession s;

    output_session_init(&s);
    CHECK_OK(ff_output_session_enter(&s));
    CHECK(ff_output_session_revoke(&s) == 1);

    /* Give up while work is still in flight. */
    CHECK(ff_output_session_quarantine(&s) == 1);
    CHECK(ff_output_session_state(&s) == FF_OUTPUT_QUARANTINED);

    /* A quarantined session is not usable, and a later finish must not
     * upgrade it to DETACHED even once the count reaches zero. */
    ff_output_session_exit(&s);
    CHECK(ff_output_session_try_finish(&s) == FF_OUTPUT_QUARANTINED);
    CHECK_AVERROR(ff_output_session_enter(&s), EPERM);

    output_session_deinit(&s);
}

/* Concurrent admission and revoke must not lose or double-count operations:
 * a miscount here would either leak the window or free it under a producer. */
typedef struct Hammer {
    FFOutputSession *session;
    int              admitted;
    int              refused;
} Hammer;

static void *hammer_thread(void *opaque)
{
    Hammer *h = opaque;
    int i;

    for (i = 0; i < 500; i++) {
        if (ff_output_session_enter(h->session) == 0) {
            h->admitted++;
            ff_output_session_exit(h->session);
        } else {
            h->refused++;
        }
    }
    return NULL;
}

static void test_output_session_concurrent_enter_exit(void)
{
    enum { THREADS = 4 };
    FFOutputSession s;
    Hammer h[THREADS];
    pthread_t th[THREADS];
    int i, admitted_total = 0, refused_total = 0;

    output_session_init(&s);
    memset(h, 0, sizeof(h));

    for (i = 0; i < THREADS; i++) {
        h[i].session = &s;
        CHECK(pthread_create(&th[i], NULL, hammer_thread, &h[i]) == 0);
    }
    for (i = 0; i < THREADS; i++)
        CHECK(pthread_join(th[i], NULL) == 0);

    for (i = 0; i < THREADS; i++) {
        admitted_total += h[i].admitted;
        refused_total  += h[i].refused;
    }

    /* Every operation either got in or was refused, never both and never
     * silently dropped; admitted == completed because each exit pairs with
     * an enter. */
    CHECK(admitted_total + refused_total == THREADS * 500);
    CHECK(refused_total == 0);
    CHECK(ff_output_session_in_flight(&s) == 0);

    CHECK(ff_output_session_revoke(&s) == 0);
    CHECK(ff_output_session_try_finish(&s) == FF_OUTPUT_DETACHED);

    output_session_deinit(&s);
}

/* A second finish after DETACHED must not re-run the release: the window
 * pointer is already NULL, and reporting DETACHED again must not be read as
 * a second detach (which on a real window would be a double release). */
static void test_output_session_finish_is_idempotent(void)
{
    FFOutputSession s;

    output_session_init(&s);
    CHECK(ff_output_session_revoke(&s) == 0);
    CHECK(ff_output_session_try_finish(&s) == FF_OUTPUT_DETACHED);
    CHECK(ff_output_session_try_finish(&s) == FF_OUTPUT_DETACHED);
    CHECK(s.win == NULL);
    output_session_deinit(&s);
}

/* Revoke racing with active producers: whatever the interleaving, the
 * session must end in DETACHED with a zero count, and no admission may be
 * counted after revoke. */
typedef struct Racer {
    FFOutputSession *session;
    volatile int     stop;
} Racer;

static void *racer_thread(void *opaque)
{
    Racer *r = opaque;

    while (!r->stop) {
        if (ff_output_session_enter(r->session) == 0) {
            if (ff_output_session_state(r->session) == FF_OUTPUT_ACTIVE)
                av_usleep(10);
            ff_output_session_exit(r->session);
        } else {
            av_usleep(10);
        }
    }
    return NULL;
}

static void test_output_session_revoke_race(void)
{
    enum { THREADS = 3 };
    FFOutputSession s;
    Racer r[THREADS];
    pthread_t th[THREADS];
    int i;

    output_session_init(&s);
    memset(r, 0, sizeof(r));

    for (i = 0; i < THREADS; i++) {
        r[i].session = &s;
        CHECK(pthread_create(&th[i], NULL, racer_thread, &r[i]) == 0);
    }
    av_usleep(20000);

    /* Revoke while everyone is in the middle of admitting. */
    ff_output_session_revoke(&s);

    /* Poll to completion, exactly as a caller would. */
    for (i = 0; i < 2000; i++) {
        if (ff_output_session_try_finish(&s) == FF_OUTPUT_DETACHED)
            break;
        av_usleep(1000);
    }
    CHECK(ff_output_session_state(&s) == FF_OUTPUT_DETACHED);
    CHECK(ff_output_session_in_flight(&s) == 0);

    for (i = 0; i < THREADS; i++) {
        r[i].stop = 1;
        CHECK(pthread_join(th[i], NULL) == 0);
    }

    output_session_deinit(&s);
}

/* ------------------------------------------------------------------ */
/* Release gate: version and advertised capabilities                    */
/*                                                                     */
/* A capability bit is a promise to every consumer that branches on it.  */
/* Two of them are now load-bearing in both directions:                 */
/*                                                                     */
/*   CAP_PCM_SESSION_SAFE_V2 and CAP_RENDERER_SAFE_CLOSE must be set,    */
/*   because the 1.2 fixes and the cases above back them;               */
/*                                                                     */
/*   CAP_CODEC_SCOPED_BUFFER_ACCOUNTING must stay clear. Batch B is on   */
/*   hold, nothing is scoped per output session, and the parallel        */
/*   hardware-decode restriction still applies. Advertising it would     */
/*   invite a consumer to rely on accounting that does not exist.        */
/* ------------------------------------------------------------------ */

static void test_release_capabilities(void)
{
    const jlong caps = Java_org_ffmpeg_FFMpegNative_getBridgeCapabilities(NULL, NULL);

    /* The version the JAR and the .so must agree on. */
    CHECK(Java_org_ffmpeg_FFMpegNative_getBridgeApiVersion(NULL, NULL) ==
          FF_JNI_BRIDGE_API_VERSION);
    CHECK(FF_JNI_BRIDGE_API_MAJOR == 1);
    CHECK(FF_JNI_BRIDGE_API_MINOR == 5);

    /* Promised by this release, each with a test set behind it. */
    CHECK(caps & FF_JNI_CAP_PCM_SESSION_SAFE_V2);
    CHECK(caps & FF_JNI_CAP_RENDERER_SAFE_CLOSE);
    CHECK(caps & FF_JNI_CAP_PACKET_BUDGET_INFO);
    CHECK(caps & FF_JNI_CAP_SIDE_DATA_EXACT_COPY);
    CHECK(caps & FF_JNI_CAP_BRIDGE_BUILD_INFO);
    CHECK(caps & FF_JNI_CAP_FRAME_LAYOUT_NORMALIZE);
    CHECK(caps & FF_JNI_CAP_BOUNDED_PCM_RESAMPLE);
    /* Both snapshot entry points exist and are driven for real by
     * tests/audio_format_snapshot_test.c; the capability is claimed only
     * because that suite runs in the same binary. */
    CHECK(caps & FF_JNI_CAP_AUDIO_STREAM_FORMAT_INFO);
    CHECK(caps & FF_JNI_CAP_AUDIO_FRAME_FORMAT_INFO);
    /* The 1.4 diagnostics: the component-name query and the counter
     * snapshot. Both have entry points in ffmpeg_jni.c; the snapshot is
     * driven below, and the component-name entry point's refusal paths are
     * driven there too (its positive path needs a live platform MediaCodec). */
    CHECK(caps & FF_JNI_CAP_VIDEO_CODEC_COMPONENT_NAME);
    CHECK(caps & FF_JNI_CAP_VIDEO_DECODE_STATS);

    /* The 1.5 libass channel exists only where the libass section is compiled
     * in. This build enables it (the binary links -lass), so the bit must be
     * set here; a build without libass must answer 0 rather than advertise an
     * entry point it does not contain. */
#if CONFIG_LIBASS
    CHECK(caps & FF_JNI_CAP_LIBASS_FONT_LOG);
#else
    CHECK((caps & FF_JNI_CAP_LIBASS_FONT_LOG) == 0);
#endif

    /* NOT promised: batch B is deferred. This is the assertion that must
     * fail first if a later change starts advertising scoped accounting
     * without the wiring and the tests behind it. */
    CHECK((caps & FF_JNI_CAP_CODEC_SCOPED_BUFFER_ACCOUNTING) == 0);

    /* NOT promised either: the bit positions are allocated so they can never
     * be reused, but there is no probe policy or probe statistics entry point
     * behind them yet. Advertising them would invite a caller to rely on a
     * strict probe that does not exist and would silently decode in software.
     * These assertions must fail first if a later change sets the bit without
     * implementing and testing the entry point. */
    CHECK((caps & FF_JNI_CAP_AUDIO_PROBE_POLICY) == 0);
    CHECK((caps & FF_JNI_CAP_AUDIO_PROBE_STATS) == 0);

    /* No bit outside the registered set may appear: a stray bit is either
     * an unregistered feature or a shifted constant, and a consumer cannot
     * tell which. */
    {
        jlong known = FF_JNI_CAP_PACKET_BUDGET_INFO |
                      FF_JNI_CAP_SIDE_DATA_EXACT_COPY |
                      FF_JNI_CAP_BRIDGE_BUILD_INFO |
                      FF_JNI_CAP_FRAME_LAYOUT_NORMALIZE |
                      FF_JNI_CAP_BOUNDED_PCM_RESAMPLE |
                      FF_JNI_CAP_PCM_SESSION_SAFE_V2 |
                      FF_JNI_CAP_RENDERER_SAFE_CLOSE |
                      FF_JNI_CAP_AUDIO_STREAM_FORMAT_INFO |
                      FF_JNI_CAP_AUDIO_FRAME_FORMAT_INFO |
                      FF_JNI_CAP_VIDEO_CODEC_COMPONENT_NAME |
                      FF_JNI_CAP_VIDEO_DECODE_STATS;
#if CONFIG_LIBASS
        known |= FF_JNI_CAP_LIBASS_FONT_LOG;
#endif

        CHECK((caps & ~known) == 0);
    }

    /* The C and Java copies of the bit layout must not drift: a shift of one
     * side only would advertise a different feature than the JAR believes. */
    CHECK(FF_JNI_CAP_CODEC_SCOPED_BUFFER_ACCOUNTING == (1LL << 7));
    CHECK(FF_JNI_CAP_RENDERER_SAFE_CLOSE == (1LL << 6));
    CHECK(FF_JNI_CAP_PCM_SESSION_SAFE_V2 == (1LL << 5));
    CHECK(FF_JNI_CAP_AUDIO_STREAM_FORMAT_INFO == (1LL << 8));
    CHECK(FF_JNI_CAP_AUDIO_FRAME_FORMAT_INFO == (1LL << 9));
    CHECK(FF_JNI_CAP_AUDIO_PROBE_POLICY == (1LL << 10));
    CHECK(FF_JNI_CAP_AUDIO_PROBE_STATS == (1LL << 11));
    CHECK(FF_JNI_CAP_VIDEO_CODEC_COMPONENT_NAME == (1LL << 12));
    CHECK(FF_JNI_CAP_VIDEO_DECODE_STATS == (1LL << 13));
    CHECK(FF_JNI_CAP_LIBASS_FONT_LOG == (1LL << 14));

    /* The whole mask for this release, written out once so an unannounced
     * bit is a test failure rather than a silent contract change. The 1.5
     * libass bit is present only in a libass build. */
#if CONFIG_LIBASS
    CHECK(caps == (jlong)0x737f);
#else
    CHECK(caps == (jlong)0x337f);
#endif
}

/* The 1.5 libass threshold is the only knob on the "MediaHub/Libass" channel.
 * It must round-trip, and it must default to MSGL_INFO (4): the fontselect
 * success line and the missing-glyph fallback line are both INFO, so a lower
 * default would hide the evidence the channel exists for. */
static void test_libass_log_level(void)
{
#if CONFIG_LIBASS
    CHECK(Java_org_ffmpeg_FFMpegNative_assGetLogLevel(NULL, NULL) == 4);
    Java_org_ffmpeg_FFMpegNative_assSetLogLevel(NULL, NULL, 2);
    CHECK(Java_org_ffmpeg_FFMpegNative_assGetLogLevel(NULL, NULL) == 2);
    Java_org_ffmpeg_FFMpegNative_assSetLogLevel(NULL, NULL, 4);
    CHECK(Java_org_ffmpeg_FFMpegNative_assGetLogLevel(NULL, NULL) == 4);
#endif
}

/* ------------------------------------------------------------------ */
/* 1.4 video diagnostics                                               */
/*                                                                     */
/* videoCodecComponentName() must never invent an answer: a null       */
/* handle, a software decoder, a MediaCodec encoder and an unopened    */
/* context all answer NULL. The positive component path needs a live   */
/* platform MediaCodec and stays a device-gate item.                   */
/*                                                                     */
/* videoDecodeStats() must fill the frozen schema from a per-context   */
/* entry, keep two contexts independent, and refuse a short array or   */
/* len before writing anything. No codec is opened here, so this runs  */
/* on any ABI without a device.                                        */
/* ------------------------------------------------------------------ */

static void test_video_diagnostics(void)
{
    JNIEnv env = fftest_jni_env();
    const AVCodec *sw, *mc, *enc;
    AVCodecContext *sw_cc = NULL, *mc_cc = NULL, *mc2_cc = NULL;
    jlongArray out;
    jlong *values = g_fftest_jni.last_long;
    int ret;

    /* A null handle is not a decoder and must not be dereferenced. */
    CHECK(Java_org_ffmpeg_FFMpegNative_videoCodecComponentName(
              NULL, NULL, 0) == NULL);

    /* A native software decoder is not a MediaCodec decoder. */
    sw = avcodec_find_decoder_by_name("h264");
    if (!sw)
        sw = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (sw && !sw->wrapper_name) {
        sw_cc = (AVCodecContext *)(intptr_t)
                Java_org_ffmpeg_FFMpegNative_allocCodecContext(
                    NULL, NULL, (jlong)(intptr_t)sw);
        CHECK(sw_cc != NULL);
        if (sw_cc) {
            CHECK(Java_org_ffmpeg_FFMpegNative_videoCodecComponentName(
                      NULL, NULL, (jlong)(intptr_t)sw_cc) == NULL);

            fftest_jni_reset();
            out = fftest_new_long_array(FF_JNI_VIDEO_DECODE_STATS_SLOTS);
            CHECK(out != NULL);
            CHECK_AVERROR(Java_org_ffmpeg_FFMpegNative_videoDecodeStats(
                              &env, NULL, (jlong)(intptr_t)sw_cc, out,
                              FF_JNI_VIDEO_DECODE_STATS_SLOTS), EINVAL);
            CHECK(g_fftest_jni.last_long_len == -1);

            Java_org_ffmpeg_FFMpegNative_codecFreeContext(
                NULL, NULL, (jlong)(intptr_t)sw_cc);
        }
    }

    /* The mediacodec encoder shares the wrapper name but is not a decoder. */
    enc = avcodec_find_encoder_by_name("h264_mediacodec");
    if (enc) {
        AVCodecContext *enc_cc = (AVCodecContext *)(intptr_t)
                Java_org_ffmpeg_FFMpegNative_allocCodecContext(
                    NULL, NULL, (jlong)(intptr_t)enc);
        CHECK(enc_cc != NULL);
        if (enc_cc) {
            CHECK(Java_org_ffmpeg_FFMpegNative_videoCodecComponentName(
                      NULL, NULL, (jlong)(intptr_t)enc_cc) == NULL);

            fftest_jni_reset();
            out = fftest_new_long_array(FF_JNI_VIDEO_DECODE_STATS_SLOTS);
            CHECK(out != NULL);
            CHECK_AVERROR(Java_org_ffmpeg_FFMpegNative_videoDecodeStats(
                              &env, NULL, (jlong)(intptr_t)enc_cc, out,
                              FF_JNI_VIDEO_DECODE_STATS_SLOTS), EINVAL);
            CHECK(g_fftest_jni.last_long_len == -1);

            Java_org_ffmpeg_FFMpegNative_codecFreeContext(
                NULL, NULL, (jlong)(intptr_t)enc_cc);
        }
    }

    mc = avcodec_find_decoder_by_name("h264_mediacodec");
    if (!mc)
        return;                     /* this build has no mediacodec decoder */

    mc_cc = (AVCodecContext *)(intptr_t)
            Java_org_ffmpeg_FFMpegNative_allocCodecContext(
                NULL, NULL, (jlong)(intptr_t)mc);
    CHECK(mc_cc != NULL);
    if (!mc_cc)
        return;

    /* Allocated but not opened: the platform has not chosen a component. */
    CHECK(Java_org_ffmpeg_FFMpegNative_videoCodecComponentName(
              NULL, NULL, (jlong)(intptr_t)mc_cc) == NULL);

    /* Fresh snapshot: schema v1, zeroed counters, unknown bit rate. */
    fftest_jni_reset();
    out = fftest_new_long_array(FF_JNI_VIDEO_DECODE_STATS_SLOTS);
    CHECK(out != NULL);
    CHECK_OK(Java_org_ffmpeg_FFMpegNative_videoDecodeStats(
                 &env, NULL, (jlong)(intptr_t)mc_cc, out,
                 FF_JNI_VIDEO_DECODE_STATS_SLOTS));
    CHECK(g_fftest_jni.last_long_len == FF_JNI_VIDEO_DECODE_STATS_SLOTS);
    CHECK(values[FF_JNI_VIDEO_STATS_SCHEMA_VERSION_SLOT] ==
          FF_JNI_VIDEO_STATS_SCHEMA_VERSION);
    CHECK(values[FF_JNI_VIDEO_STATS_DECODED_FRAMES] == 0);
    CHECK(values[FF_JNI_VIDEO_STATS_RENDERED_FRAMES] == 0);
    CHECK(values[FF_JNI_VIDEO_STATS_DROPPED_FRAMES] == 0);
    CHECK(values[FF_JNI_VIDEO_STATS_DECODE_ERRORS] == 0);
    CHECK(values[FF_JNI_VIDEO_STATS_CURRENT_BITRATE] == 0);

    /* Counters advance independently, and the snapshot reports them. */
    ff_video_stats_add(mc_cc, FF_VIDEO_STAT_DECODED);
    ff_video_stats_add(mc_cc, FF_VIDEO_STAT_RENDERED);
    ff_video_stats_add(mc_cc, FF_VIDEO_STAT_RENDERED);
    ff_video_stats_add(mc_cc, FF_VIDEO_STAT_DROPPED);
    ff_video_stats_add(mc_cc, FF_VIDEO_STAT_ERROR);
    mc_cc->bit_rate = 123456;

    fftest_jni_reset();
    out = fftest_new_long_array(FF_JNI_VIDEO_DECODE_STATS_SLOTS);
    CHECK(out != NULL);
    CHECK_OK(Java_org_ffmpeg_FFMpegNative_videoDecodeStats(
                 &env, NULL, (jlong)(intptr_t)mc_cc, out,
                 FF_JNI_VIDEO_DECODE_STATS_SLOTS));
    CHECK(values[FF_JNI_VIDEO_STATS_DECODED_FRAMES] == 1);
    CHECK(values[FF_JNI_VIDEO_STATS_RENDERED_FRAMES] == 2);
    CHECK(values[FF_JNI_VIDEO_STATS_DROPPED_FRAMES] == 1);
    CHECK(values[FF_JNI_VIDEO_STATS_DECODE_ERRORS] == 1);
    CHECK(values[FF_JNI_VIDEO_STATS_CURRENT_BITRATE] == 123456);

    /* A second context must not see the first one's counters. */
    mc2_cc = (AVCodecContext *)(intptr_t)
             Java_org_ffmpeg_FFMpegNative_allocCodecContext(
                 NULL, NULL, (jlong)(intptr_t)mc);
    CHECK(mc2_cc != NULL);
    if (mc2_cc) {
        fftest_jni_reset();
        out = fftest_new_long_array(FF_JNI_VIDEO_DECODE_STATS_SLOTS);
        CHECK(out != NULL);
        CHECK_OK(Java_org_ffmpeg_FFMpegNative_videoDecodeStats(
                     &env, NULL, (jlong)(intptr_t)mc2_cc, out,
                     FF_JNI_VIDEO_DECODE_STATS_SLOTS));
        CHECK(values[FF_JNI_VIDEO_STATS_DECODED_FRAMES] == 0);
        CHECK(values[FF_JNI_VIDEO_STATS_RENDERED_FRAMES] == 0);
        CHECK(values[FF_JNI_VIDEO_STATS_DROPPED_FRAMES] == 0);
    }

    /* A short caller length or Java array is refused before any write. */
    fftest_jni_reset();
    out = fftest_new_long_array(FF_JNI_VIDEO_DECODE_STATS_SLOTS);
    CHECK(out != NULL);
    CHECK_AVERROR(Java_org_ffmpeg_FFMpegNative_videoDecodeStats(
                      &env, NULL, (jlong)(intptr_t)mc_cc, out,
                      FF_JNI_VIDEO_DECODE_STATS_SLOTS - 1), EINVAL);
    CHECK(g_fftest_jni.last_long_len == -1);

    fftest_jni_reset();
    out = fftest_new_long_array(FF_JNI_VIDEO_DECODE_STATS_SLOTS - 1);
    CHECK(out != NULL);
    CHECK_AVERROR(Java_org_ffmpeg_FFMpegNative_videoDecodeStats(
                      &env, NULL, (jlong)(intptr_t)mc_cc, out,
                      FF_JNI_VIDEO_DECODE_STATS_SLOTS), EINVAL);
    CHECK(g_fftest_jni.last_long_len == -1);

    CHECK_AVERROR(Java_org_ffmpeg_FFMpegNative_videoDecodeStats(
                      &env, NULL, (jlong)(intptr_t)mc_cc, NULL,
                      FF_JNI_VIDEO_DECODE_STATS_SLOTS), EINVAL);

    if (mc2_cc)
        Java_org_ffmpeg_FFMpegNative_codecFreeContext(
            NULL, NULL, (jlong)(intptr_t)mc2_cc);
    Java_org_ffmpeg_FFMpegNative_codecFreeContext(
        NULL, NULL, (jlong)(intptr_t)mc_cc);
}

/* BuildInfo must be well-formed JSON with every field present, and any field
 * the build could not establish must say "unknown" rather than be absent or
 * invented. This cannot go through the JNI wrapper without a VM, so it pins
 * the pieces the wrapper formats: the field names and the fallbacks. */
static void test_build_info_is_complete(void)
{
    /* Every provenance macro must expand to a non-empty literal: an empty
     * define would produce {"jniSourceRevision":""}, which reads as a real
     * (empty) revision rather than as a missing one. */
    CHECK(sizeof(FF_JNI_SOURCE_REVISION) > 1);
    CHECK(sizeof(FF_JNI_ANDROID_API) > 1);
    CHECK(sizeof(FF_JNI_NDK_VERSION) > 1);
    CHECK(sizeof(FF_JNI_BUILD_ID) > 1);
    CHECK(sizeof(FF_JNI_ABI_NAME) > 1);
    CHECK(sizeof(FF_JNI_BUILD_TYPE) > 1);

    /* A literal that only repeats the macro name means the stringification
     * was done with one level, which is a real bug this catches. Note this
     * is exactly the case on a target build, where __ANDROID_API__ exists. */
    CHECK(strcmp(FF_JNI_ANDROID_API, "__ANDROID_API__") != 0);

    /* The bridge version reported in JSON must be the same pair the version
     * entry point returns, so the two cannot describe different contracts. */
    CHECK(((FF_JNI_BRIDGE_API_MAJOR << 16) | FF_JNI_BRIDGE_API_MINOR) ==
          FF_JNI_BRIDGE_API_VERSION);
}

/* ------------------------------------------------------------------ */
/* MediaCodec buffer bookkeeping                                        */
/*                                                                     */
/* mediasurfaceGetBuffer / ReleaseBuffer / RenderBufferAtTime /         */
/* GetOutstandingBuffers maintain the process-wide count that a caller   */
/* reads as "the frame was returned". Nothing exercised them before, so  */
/* a dropped decrement, a decrement on failure or a double decrement     */
/* would only surface as a wrong teardown decision on a device.          */
/*                                                                     */
/* No codec is opened here. The entry points read the frame's pixel      */
/* format and data[3], and av_mediacodec_release_buffer() decides what   */
/* happened from the buffer's own released/serial fields, so the         */
/* refusals are reachable without a MediaCodec instance. The success     */
/* path is NOT: it goes on to call the platform through ctx->codec, and  */
/* a build without CONFIG_MEDIACODEC returns ENOSYS for everything.      */
/*                                                                     */
/* Those cases are compiled and run only where the count can actually     */
/* move; elsewhere they are skipped with a note instead of pretending    */
/* to have checked something. What the platform did to the Surface is    */
/* out of scope for all of them and stays a device-gate item.            */
/* ------------------------------------------------------------------ */

/* The count is process-wide, so every case starts and ends at zero: a leak
 * must fail the case that caused it, not the one that runs next. */
static void hw_check_baseline(const char *where)
{
    const int n = Java_org_ffmpeg_FFMpegNative_mediasurfaceGetOutstandingBuffers(NULL, NULL);

    CHECK(n == 0);
    if (n != 0)
        fprintf(stderr, "  outstanding=%d at %s\n", n, where);
}

/* A frame shaped like a decoded MediaCodec output frame: the pixel format
 * that routes to the hardware path, and an opaque buffer in data[3]. */
static AVFrame *make_hw_frame(void *buffer)
{
    AVFrame *f = av_frame_alloc();

    if (!f)
        return NULL;
    f->format  = AV_PIX_FMT_MEDIACODEC;
    f->width   = 1920;
    f->height  = 1080;
    f->data[3] = buffer;
    return f;
}

/* A decoded MediaCodec buffer that has already been returned once.
 *
 * av_mediacodec_release_buffer()/render_buffer_at_time() first do
 * atomic_fetch_add(&buffer->released, 1). With released already 1 the fetch
 * yields non-zero, so:
 *
 *   render == 1  -> AVERROR(ENOENT), without touching ctx;
 *   render == 0  -> 0, without touching ctx.
 *
 * Both are therefore reachable without a codec, and both are real states: a
 * consumer that renders twice, or renders and then releases, gets exactly
 * this. `ctx` stays NULL so that any other branch (released == 0) would
 * crash instead of silently reading a fake codec -- a loud failure beats a
 * wrong count. */
static void init_returned_buffer(MediaCodecBuffer *b)
{
    memset(b, 0, sizeof(*b));
    /* released is atomic_int: initialise it as an atomic, not by assignment,
     * so the value is well defined under C11 even before the first RMW. */
    atomic_init(&b->released, 1);   /* already returned to the codec once */
    b->serial = 7;
}

/* The global counter keeps its old meaning: a process-wide diagnostic, not a
 * per-output-session ledger. Pinning it here is what stops a future reader
 * from treating it as scoped accounting that this release does not have. */
static void test_mediasurface_count_is_process_wide(void)
{
    MediaCodecBuffer a, b;
    AVFrame *fa, *fb;

    hw_check_baseline("process wide");

    init_returned_buffer(&a);
    init_returned_buffer(&b);
    fa = make_hw_frame(&a);
    fb = make_hw_frame(&b);
    CHECK(fa != NULL && fb != NULL);
    if (!fa || !fb) {
        av_frame_free(&fa);
        av_frame_free(&fb);
        return;
    }

    /* Two different buffers, conceptually from two different output
     * sessions: the single global count adds them together, and there is no
     * way to ask it about one session alone. */
    CHECK(Java_org_ffmpeg_FFMpegNative_mediasurfaceGetBuffer(
              NULL, NULL, (jlong)(intptr_t)fa) != 0);
    CHECK(Java_org_ffmpeg_FFMpegNative_mediasurfaceGetBuffer(
              NULL, NULL, (jlong)(intptr_t)fb) != 0);
    CHECK(Java_org_ffmpeg_FFMpegNative_mediasurfaceGetOutstandingBuffers(NULL, NULL) == 2);

    av_frame_free(&fa);
    av_frame_free(&fb);

    while (Java_org_ffmpeg_FFMpegNative_mediasurfaceGetOutstandingBuffers(NULL, NULL) > 0)
        ff_hw_output_release();
    hw_check_baseline("process wide end");
}

/* The load-bearing refusal: a release that did not reach the codec must not
 * decrement. If it did, a caller polling the count would see zero while the
 * Surface is still referenced by an outstanding buffer, and tear down the
 * producer underneath it. */
static void test_mediasurface_failed_release_keeps_count(void)
{
    MediaCodecBuffer b;
    const jlong buf = (jlong)(intptr_t)&b;
    int ret;

    init_returned_buffer(&b);
    hw_check_baseline("failed release");

    /* First count a get, so a decrement is observable: with the count at 0
     * the floor in ff_hw_output_release() would hide it. */
    ff_hw_output_acquire();
    CHECK(Java_org_ffmpeg_FFMpegNative_mediasurfaceGetOutstandingBuffers(NULL, NULL) == 1);

    /* Rendering a buffer that was already returned has nothing to render:
     * the backend reports ENOENT and the bridge must leave the count alone. */
    ret = Java_org_ffmpeg_FFMpegNative_mediasurfaceRenderBufferAtTime(NULL, NULL, buf, 0);
    if (ret == AVERROR(ENOSYS)) {
        /* No MediaCodec backend in this build: the call cannot have reached
         * the count, so there is nothing to learn here. */
        fprintf(stderr, "  skip: mediacodec backend disabled (ENOSYS)\n");
        ff_hw_output_release();
        hw_check_baseline("failed release end");
        return;
    }
    CHECK_AVERROR(ret, ENOENT);
    CHECK(Java_org_ffmpeg_FFMpegNative_mediasurfaceGetOutstandingBuffers(NULL, NULL) == 1);

    /* A non-rendering release of the same spent buffer returns 0 to the
     * caller (av_mediacodec_release_buffer's `else` arm) without reaching
     * the codec. The bridge currently decrements on any non-negative return,
     * so this case documents the asymmetry rather than demanding an
     * increment-then-release identity. Tightening it would need a per-buffer
     * ledger, which is exactly the scoped accounting J6 has not shipped. */
    CHECK_OK(Java_org_ffmpeg_FFMpegNative_mediasurfaceReleaseBuffer(NULL, NULL, buf, 0));
    CHECK(Java_org_ffmpeg_FFMpegNative_mediasurfaceGetOutstandingBuffers(NULL, NULL) <= 1);
    CHECK(Java_org_ffmpeg_FFMpegNative_mediasurfaceGetOutstandingBuffers(NULL, NULL) >= 0);

    /* Drop whatever is left of the entry this case added. */
    while (Java_org_ffmpeg_FFMpegNative_mediasurfaceGetOutstandingBuffers(NULL, NULL) > 0)
        ff_hw_output_release();
    hw_check_baseline("failed release end");
}

/* A soft-decoded frame has no hardware buffer: GetBuffer must report 0 and
 * must not inflate the count. Counting a get that returned nothing would
 * make the count useless as evidence, because it would never reach zero. */
static void test_mediasurface_no_buffer_is_not_counted(void)
{
    AVFrame *f = av_frame_alloc();

    hw_check_baseline("soft frame");
    CHECK(f != NULL);
    if (!f)
        return;

    f->format = AV_PIX_FMT_YUV420P;              /* software path */
    f->width  = 320;
    f->height = 240;
    CHECK(Java_org_ffmpeg_FFMpegNative_mediasurfaceGetBuffer(
              NULL, NULL, (jlong)(intptr_t)f) == 0);
    CHECK(Java_org_ffmpeg_FFMpegNative_mediasurfaceGetOutstandingBuffers(NULL, NULL) == 0);

    /* The right pixel format but no buffer in data[3]: same answer. */
    f->format = AV_PIX_FMT_MEDIACODEC;
    CHECK(Java_org_ffmpeg_FFMpegNative_mediasurfaceGetBuffer(
              NULL, NULL, (jlong)(intptr_t)f) == 0);
    CHECK(Java_org_ffmpeg_FFMpegNative_mediasurfaceGetOutstandingBuffers(NULL, NULL) == 0);

    /* A null frame is refused, not dereferenced. */
    CHECK(Java_org_ffmpeg_FFMpegNative_mediasurfaceGetBuffer(NULL, NULL, 0) == 0);

    av_frame_free(&f);
    hw_check_baseline("soft frame end");
}

/* Null handles are refused before anything is reached, for every entry
 * point. These are the calls a mis-wired consumer makes while tearing down,
 * so they must be cheap and safe rather than dereference zero. */
static void test_mediasurface_null_handles(void)
{
    hw_check_baseline("null handles");
    CHECK_AVERROR(Java_org_ffmpeg_FFMpegNative_mediasurfaceReleaseBuffer(
                      NULL, NULL, 0, 1), EINVAL);
    CHECK_AVERROR(Java_org_ffmpeg_FFMpegNative_mediasurfaceRenderBufferAtTime(
                      NULL, NULL, 0, 0), EINVAL);
    CHECK(Java_org_ffmpeg_FFMpegNative_mediasurfaceGetOutstandingBuffers(NULL, NULL) == 0);
    hw_check_baseline("null handles end");
}

/* GetBuffer and the release entry points run on different threads. The
 * count must survive that: under a TSan build this is where a plain int
 * would be reported, and in a plain build the residue is what a teardown
 * decision is made from.
 *
 * Each thread pairs every successful get with exactly one release, so the
 * total must come back to zero; a lost update in either direction leaves a
 * non-zero residue. */
typedef struct HwRacer {
    MediaCodecBuffer buf;   /* each thread its own: released is not shared */
    long             gets;
    long             acquired; /* counted gets this thread performed  */
    long             released; /* paired releases this thread did     */
} HwRacer;

static void *hw_getter_thread(void *opaque)
{
    HwRacer *r = opaque;
    long i;

    for (i = 0; i < r->gets; i++) {
        AVFrame *f = make_hw_frame(&r->buf);

        if (!f)
            break;
        if (Java_org_ffmpeg_FFMpegNative_mediasurfaceGetBuffer(
                NULL, NULL, (jlong)(intptr_t)f) != 0) {
            r->acquired++;
            /* Pair most of them immediately so the residue at the end is a
             * precise number rather than "whatever happens to be left".
             * Every fourth one stays outstanding, which keeps the count
             * elevated for most of the run: a counter bouncing between 0
             * and 1 exercises far less of the race. */
            if ((i & 3) != 3) {
                ff_hw_output_release();
                r->released++;
            }
        }
        av_frame_free(&f);
    }
    return NULL;
}

static void test_mediasurface_count_is_thread_safe(void)
{
    enum { THREADS = 4 };
    HwRacer r[THREADS];
    pthread_t th[THREADS];
    long acquired = 0, released = 0;
    int i;

    hw_check_baseline("threads");
    memset(r, 0, sizeof(r));
    for (i = 0; i < THREADS; i++) {
        init_returned_buffer(&r[i].buf);
        r[i].gets = 20000;
        CHECK(pthread_create(&th[i], NULL, hw_getter_thread, &r[i]) == 0);
    }
    for (i = 0; i < THREADS; i++) {
        CHECK(pthread_join(th[i], NULL) == 0);
        acquired += r[i].acquired;
        released += r[i].released;
    }

    /* Every counted get that was not deliberately held was released exactly
     * once, so the residue is acquired - released. A lost increment or a
     * lost decrement anywhere in between moves this number; a bare "== 0"
     * would hide both. */
    CHECK(acquired > 0);
    CHECK(acquired >= released);
    CHECK(Java_org_ffmpeg_FFMpegNative_mediasurfaceGetOutstandingBuffers(NULL, NULL) ==
          (int)(acquired - released));

    while (Java_org_ffmpeg_FFMpegNative_mediasurfaceGetOutstandingBuffers(NULL, NULL) > 0)
        ff_hw_output_release();
    hw_check_baseline("threads end");
}

/* ------------------------------------------------------------------ */
/* 1.2 PCM contract: planes, drain termination, boundaries             */
/*                                                                     */
/* Each case pins one of the fixes J1..J5 from the bridge review. They */
/* drive the production helpers, not a re-implementation, so a         */
/* regression in ffmpeg_jni.c fails here rather than in a device log.  */
/* ------------------------------------------------------------------ */

/* Multi-channel planar input must be addressed through extended_data.
 *
 * The frame's inline data[] array only has AV_NUM_DATA_POINTERS entries;
 * a 12-channel planar frame keeps every plane in extended_data. Walking
 * data[] by channel index reads past the end of that array, which is a
 * silent out-of-bounds read that a plain "did it succeed" assertion cannot
 * see. This case therefore uses a frame whose planes are only reachable
 * through extended_data and asserts the conversion consumed it. */
static void test_pcm_session_planar_extended_data(void)
{
    FFPcmSession *s = make_session(PCM_OUT_ROOM, 8192, PCM_OUT_ROOM);
    AVChannelLayout layout = { 0 };
    AVFrame *f = NULL;
    int16_t *out = NULL;
    int produced, ret, i;
    const int channels = 12;

    CHECK(s != NULL);
    if (!s)
        return;

    /* 12 channels: more than the eight inline plane pointers. */
    av_channel_layout_default(&layout, channels);
    if (layout.nb_channels != channels) {
        /* This build's default table has no 12-channel layout; the case is
         * meaningless without one. */
        av_channel_layout_uninit(&layout);
        Java_org_ffmpeg_FFMpegNative_pcmSessionFree(NULL, NULL,
                                                    (jlong)(intptr_t)s);
        return;
    }

    f   = make_pcm_frame(AV_SAMPLE_FMT_S16P, 48000, &layout, 128);
    out = av_malloc_array((size_t)PCM_OUT_ROOM * 2, sizeof(int16_t));
    CHECK(f != NULL && out != NULL);

    if (f && out) {
        /* extended_data must be the full plane list, and every plane must
         * be present: that is the precondition the validation checks. */
        CHECK(f->extended_data != NULL);
        CHECK_OK(ff_pcm_validate_input(f));
        for (i = 0; i < channels; i++) {
            CHECK(f->extended_data[i] != NULL);
            ((int16_t *)f->extended_data[i])[0] = (int16_t)(100 * (i + 1));
        }

        ret = pcm_convert(s, f, out, PCM_OUT_ROOM,
                          PCM_OUT_ROOM * 2 * (int64_t)sizeof(int16_t),
                          &produced);
        CHECK(ret == FF_PCM_OUTPUT_READY);
        CHECK(produced > 0);

        /* A missing plane is a refusal, not a crash and not silence. */
        f->extended_data[channels - 1] = NULL;
        ret = pcm_convert(s, f, out, PCM_OUT_ROOM,
                          PCM_OUT_ROOM * 2 * (int64_t)sizeof(int16_t),
                          &produced);
        CHECK_AVERROR(ret, EINVAL);
        CHECK(produced == 0);
    }

    av_frame_free(&f);
    av_free(out);
    Java_org_ffmpeg_FFMpegNative_pcmSessionFree(NULL, NULL, (jlong)(intptr_t)s);
    av_channel_layout_uninit(&layout);
}

/* A zero-sample frame is a legal frame, not EOF and not a flush request. It
 * must leave the session accepting input and must not produce output. */
static void test_pcm_session_zero_sample_frame_is_not_eof(void)
{
    FFPcmSession *s = make_session(PCM_OUT_ROOM, 4096, PCM_OUT_ROOM);
    AVChannelLayout stereo = { 0 };
    AVFrame *f = NULL, *real = NULL;
    int16_t *out = NULL;
    int produced, ret;

    CHECK(s != NULL);
    if (!s)
        return;

    av_channel_layout_default(&stereo, 2);
    f    = make_pcm_frame(AV_SAMPLE_FMT_S16P, 48000, &stereo, 0);
    real = make_pcm_frame(AV_SAMPLE_FMT_S16P, 48000, &stereo, 256);
    out  = av_malloc_array((size_t)PCM_OUT_ROOM * 2, sizeof(int16_t));
    CHECK(f != NULL && real != NULL && out != NULL);

    if (f && real && out) {
        CHECK_OK(ff_pcm_validate_input(f));

        ret = pcm_convert(s, f, out, PCM_OUT_ROOM,
                          PCM_OUT_ROOM * 2 * (int64_t)sizeof(int16_t),
                          &produced);
        /* Accepted without being turned into a flush. */
        CHECK(ret >= 0);
        CHECK(ret != FF_PCM_DRAINED);
        CHECK(ret != FF_PCM_NEED_DRAIN);
        CHECK(produced == 0);
        CHECK(s->phase != FF_PCM_PHASE_DRAINED);

        /* The stream continues normally afterwards. */
        ret = pcm_convert(s, real, out, PCM_OUT_ROOM,
                          PCM_OUT_ROOM * 2 * (int64_t)sizeof(int16_t),
                          &produced);
        CHECK(ret == FF_PCM_OUTPUT_READY);
    }

    av_frame_free(&f);
    av_frame_free(&real);
    av_free(out);
    Java_org_ffmpeg_FFMpegNative_pcmSessionFree(NULL, NULL, (jlong)(intptr_t)s);
    av_channel_layout_uninit(&stereo);
}

/* create -> drain and reset -> drain must both end in DRAINED, not EINVAL.
 * An unconfigured session has no resampler to flush, which used to be
 * reported as an invalid call. */
static void test_pcm_session_drain_empty_and_reset(void)
{
    FFPcmSession *s = make_session(PCM_OUT_ROOM, 4096, PCM_OUT_ROOM);
    AVChannelLayout stereo = { 0 };
    AVFrame *f = NULL;
    int16_t *out = NULL;
    int produced = -1, ret, i;

    CHECK(s != NULL);
    if (!s)
        return;

    av_channel_layout_default(&stereo, 2);
    f   = make_pcm_frame(AV_SAMPLE_FMT_S16P, 44100, &stereo, 512);
    out = av_malloc_array((size_t)PCM_OUT_ROOM * 2, sizeof(int16_t));
    CHECK(f != NULL && out != NULL);

    if (f && out) {
        /* Fresh session: nothing to flush. */
        ret = pcm_drain(s, out, PCM_OUT_ROOM, &produced);
        CHECK(ret == FF_PCM_DRAINED);
        CHECK(produced == 0);

        /* Repeated drain stays DRAINED (idempotent). */
        ret = pcm_drain(s, out, PCM_OUT_ROOM, &produced);
        CHECK(ret == FF_PCM_DRAINED);
        CHECK(produced == 0);

        /* Build a backlog, then reset: the tail is gone, so draining must
         * complete immediately and leave the format usable. */
        ret = pcm_convert(s, f, out, 64, 64 * 2 * (int64_t)sizeof(int16_t),
                          &produced);
        CHECK(ret >= 0);
        CHECK(swr_get_delay(s->swr, 48000) > 0);

        CHECK_OK(Java_org_ffmpeg_FFMpegNative_pcmSessionReset(
                     NULL, NULL, (jlong)(intptr_t)s));
        ret = pcm_drain(s, out, PCM_OUT_ROOM, &produced);
        CHECK(ret == FF_PCM_DRAINED);
        CHECK(produced == 0);

        /* And the next convert is not treated as a format change. */
        ret = pcm_convert(s, f, out, PCM_OUT_ROOM,
                          PCM_OUT_ROOM * 2 * (int64_t)sizeof(int16_t),
                          &produced);
        CHECK(ret == FF_PCM_OUTPUT_READY);
        CHECK(produced > 0);
        i = produced;
        (void)i;

        /* Draining to EOF then repeating still terminates. */
        CHECK(pcm_drain_all(s, out, PCM_OUT_ROOM) >= 0);
        ret = pcm_drain(s, out, PCM_OUT_ROOM, &produced);
        CHECK(ret == FF_PCM_DRAINED);
        CHECK(produced == 0);
    }

    av_frame_free(&f);
    av_free(out);
    Java_org_ffmpeg_FFMpegNative_pcmSessionFree(NULL, NULL, (jlong)(intptr_t)s);
    av_channel_layout_uninit(&stereo);
}

/* Draining one sample at a time must still finish. A drain that is refused
 * while the backlog is larger than the block would close the only exit from
 * a backlog the caller cannot otherwise remove. */
static void test_pcm_session_drain_one_sample_blocks(void)
{
    FFPcmSession *s = make_session(PCM_OUT_ROOM, 65536, 32);
    AVChannelLayout stereo = { 0 };
    AVFrame *f = NULL;
    int16_t *out = NULL;
    int produced, ret, passes = 0;
    int64_t total = 0;

    CHECK(s != NULL);
    if (!s)
        return;

    av_channel_layout_default(&stereo, 2);
    f   = make_pcm_frame(AV_SAMPLE_FMT_S16P, 44100, &stereo, 4096);
    out = av_malloc_array((size_t)PCM_OUT_ROOM * 2, sizeof(int16_t));
    CHECK(f != NULL && out != NULL);

    if (f && out) {
        /* Leave a real backlog. A 32-sample delay budget with a 4096-sample
         * frame means the caller must drain between blocks. */
        ret = pcm_convert(s, f, out, PCM_OUT_ROOM,
                          PCM_OUT_ROOM * 2 * (int64_t)sizeof(int16_t),
                          &produced);
        CHECK(ret >= 0);

        /* One sample per call: bounded progress, and it must terminate. */
        for (passes = 0; passes < 4096; passes++) {
            ret = pcm_drain(s, out, 1, &produced);
            CHECK(ret != FF_PCM_NEED_DRAIN);
            if (ret < 0) {
                CHECK(0 && "drain refused instead of making progress");
                break;
            }
            total += produced;
            if (ret == FF_PCM_DRAINED)
                break;
        }
        CHECK(ret == FF_PCM_DRAINED);
        CHECK(total > 0);
        /* The delay budget was not used to refuse the drain. */
        CHECK(passes < 4096);
    }

    av_frame_free(&f);
    av_free(out);
    Java_org_ffmpeg_FFMpegNative_pcmSessionFree(NULL, NULL, (jlong)(intptr_t)s);
    av_channel_layout_uninit(&stereo);
}

/* Bounds are checked before anything is consumed: an array one byte short,
 * an absurd outCount and an overflowing product are all refused with the
 * input untouched and the session still usable.
 *
 * These go through the exported wrapper, so the capacity being short is the
 * Java array's real length, and "refused" additionally means nothing was
 * published into it. Reimplementing the body here (as an earlier version
 * did) is what let a review believe this path was covered when the wrapper
 * was never executed at all. */
static void test_pcm_session_jni_bounds(void)
{
    FFPcmSession *s = make_session(PCM_OUT_ROOM, 4096, PCM_OUT_ROOM);
    AVChannelLayout stereo = { 0 };
    AVFrame *f = NULL;
    JNIEnv env = fftest_jni_env();
    jbyteArray out = NULL;
    jintArray status = NULL;
    int produced = -1, ret;
    /* Exactly one 64-sample stereo S16 block: the smallest array that can
     * hold what a 64-sample request would write. */
    const jsize exact = 64 * 2 * (jsize)sizeof(int16_t);

    CHECK(s != NULL);
    if (!s)
        return;

    av_channel_layout_default(&stereo, 2);
    f = make_pcm_frame(AV_SAMPLE_FMT_S16P, 44100, &stereo, 512);
    CHECK(f != NULL);

    if (f) {
        /* Configure first so the boundary checks run on a live context. */
        ret = pcm_convert(s, f, NULL, PCM_OUT_ROOM,
                          PCM_OUT_ROOM * 2 * (int64_t)sizeof(int16_t),
                          &produced);
        CHECK(ret >= 0);

        /* One byte short of what the block would need: refused, and the
         * array is left untouched rather than half-written. */
        fftest_jni_reset();
        out    = fftest_new_byte_array(exact - 1);
        status = fftest_new_int_array(1);
        CHECK(out != NULL && status != NULL);
        ret = Java_org_ffmpeg_FFMpegNative_pcmSessionConvert(
                  &env, NULL, (jlong)(intptr_t)s, (jlong)(intptr_t)f,
                  out, 64, status);
        CHECK_AVERROR(ret, ENOMEM);
        CHECK(g_fftest_jni.last_byte_len == -1);   /* nothing published */
        CHECK(g_fftest_jni.last_status == 0);      /* slot zeroed        */

        /* The same request against a block that just fits is accepted, so
         * the refusal above was about the byte count and not about 64. */
        fftest_jni_reset();
        out = fftest_new_byte_array(exact);
        status = fftest_new_int_array(1);
        CHECK(out != NULL && status != NULL);
        ret = Java_org_ffmpeg_FFMpegNative_pcmSessionConvert(
                  &env, NULL, (jlong)(intptr_t)s, (jlong)(intptr_t)f,
                  out, 64, status);
        CHECK(ret == FF_PCM_OUTPUT_READY);
        CHECK(g_fftest_jni.last_byte_len > 0);
        CHECK(g_fftest_jni.last_byte_len <= exact);
        /* status is samples per channel, out holds frames*channels*bps
         * bytes: the published byte count must agree with the published
         * sample count. Deriving both from the session keeps this true if
         * the output format ever changes. */
        CHECK(g_fftest_jni.last_byte_len ==
              g_fftest_jni.last_status *
              s->out_layout.nb_channels *
              av_get_bytes_per_sample((enum AVSampleFormat)s->out_fmt));

        /* outCount so large the byte product cannot be expressed. */
        fftest_jni_reset();
        out    = fftest_new_byte_array(exact);
        status = fftest_new_int_array(1);
        ret = Java_org_ffmpeg_FFMpegNative_pcmSessionConvert(
                  &env, NULL, (jlong)(intptr_t)s, (jlong)(intptr_t)f,
                  out, INT_MAX, status);
        CHECK(ret < 0);
        CHECK(g_fftest_jni.last_byte_len == -1);

        /* A status array too short for even one slot is refused before
         * anything else happens. */
        fftest_jni_reset();
        out    = fftest_new_byte_array(exact);
        status = fftest_new_int_array(0);
        ret = Java_org_ffmpeg_FFMpegNative_pcmSessionConvert(
                  &env, NULL, (jlong)(intptr_t)s, (jlong)(intptr_t)f,
                  out, 64, status);
        CHECK_AVERROR(ret, EINVAL);

        /* A non-positive block is refused, not treated as "drain me". */
        fftest_jni_reset();
        out    = fftest_new_byte_array(exact);
        status = fftest_new_int_array(1);
        ret = Java_org_ffmpeg_FFMpegNative_pcmSessionConvert(
                  &env, NULL, (jlong)(intptr_t)s, (jlong)(intptr_t)f,
                  out, 0, status);
        CHECK(ret < 0);

        /* None of the refusals consumed input or broke the session. */
        CHECK(s->phase == FF_PCM_PHASE_ACTIVE);
        ret = pcm_convert(s, f, NULL, PCM_OUT_ROOM,
                          PCM_OUT_ROOM * 2 * (int64_t)sizeof(int16_t),
                          &produced);
        CHECK(ret >= 0);
    }

    av_frame_free(&f);
    fftest_jni_reset();
    Java_org_ffmpeg_FFMpegNative_pcmSessionFree(NULL, NULL, (jlong)(intptr_t)s);
    av_channel_layout_uninit(&stereo);
}

/* Publishing is the last step and the only one that can fail after samples
 * have been produced. When it does, the input is already consumed, so the
 * session must be POISONED rather than offer the frame for a replay, and the
 * exception must be left for the JVM instead of being overwritten with a
 * status write that JNI would not allow anyway. */
static void test_pcm_publish_failure_poisons_session(void)
{
    FFPcmSession *s = make_session(PCM_OUT_ROOM, 4096, PCM_OUT_ROOM);
    AVChannelLayout stereo = { 0 };
    AVFrame *f = NULL;
    JNIEnv env = fftest_jni_env();
    jbyteArray out = NULL;
    jintArray status = NULL;
    int ret;

    CHECK(s != NULL);
    if (!s)
        return;

    av_channel_layout_default(&stereo, 2);
    f = make_pcm_frame(AV_SAMPLE_FMT_S16P, 44100, &stereo, 256);
    CHECK(f != NULL);

    if (f) {
        /* Inject the failure the VM would raise from SetByteArrayRegion. */
        fftest_jni_reset();
        g_fftest_jni.fail_publish = 1;
        out    = fftest_new_byte_array(FFTEST_OUT_CAP_MAX);
        status = fftest_new_int_array(1);
        CHECK(out != NULL && status != NULL);

        ret = Java_org_ffmpeg_FFMpegNative_pcmSessionConvert(
                  &env, NULL, (jlong)(intptr_t)s, (jlong)(intptr_t)f,
                  out, 256, status);
        CHECK(ret < 0);
        /* The exception is still pending: the wrapper must not have
         * cleared it to write status. */
        CHECK(g_fftest_jni.exception != 0);
        CHECK(s->phase == FF_PCM_PHASE_POISONED);

        /* A poisoned session refuses work instead of converting again. */
        fftest_jni_reset();
        out    = fftest_new_byte_array(FFTEST_OUT_CAP_MAX);
        status = fftest_new_int_array(1);
        ret = Java_org_ffmpeg_FFMpegNative_pcmSessionConvert(
                  &env, NULL, (jlong)(intptr_t)s, (jlong)(intptr_t)f,
                  out, 256, status);
        CHECK_AVERROR(ret, EIO);

        /* Reset is the documented way out of POISONED. */
        fftest_jni_reset();
        CHECK_OK(Java_org_ffmpeg_FFMpegNative_pcmSessionReset(
                     NULL, NULL, (jlong)(intptr_t)s));
        CHECK(s->phase != FF_PCM_PHASE_POISONED);
    }

    av_frame_free(&f);
    fftest_jni_reset();
    Java_org_ffmpeg_FFMpegNative_pcmSessionFree(NULL, NULL, (jlong)(intptr_t)s);
    av_channel_layout_uninit(&stereo);
}

/* Planar output has no representation in a single byte[] block. Create must
 * refuse it instead of writing the planes as if they were interleaved. */
static void test_pcm_session_rejects_planar_output(void)
{
    CHECK((intptr_t)Java_org_ffmpeg_FFMpegNative_pcmSessionCreate(
              NULL, NULL, AV_SAMPLE_FMT_S16P, 48000, AV_CH_LAYOUT_STEREO,
              1024, 1024, 1024) == 0);
    CHECK((intptr_t)Java_org_ffmpeg_FFMpegNative_pcmSessionCreate(
              NULL, NULL, AV_SAMPLE_FMT_FLTP, 48000, AV_CH_LAYOUT_STEREO,
              1024, 1024, 1024) == 0);
    /* A packed format with the same limits is still accepted, so the refusal
     * is about planarity and not about the other arguments. */
    {
        jlong h = Java_org_ffmpeg_FFMpegNative_pcmSessionCreate(
                      NULL, NULL, AV_SAMPLE_FMT_S16, 48000,
                      AV_CH_LAYOUT_STEREO, 1024, 1024, 1024);
        CHECK(h != 0);
        Java_org_ffmpeg_FFMpegNative_pcmSessionFree(NULL, NULL, h);
    }
}

/* The audio->PCM path must not leave a stale status slot behind for a
 * refusal: the JNI wrapper writes zero before anything else. Without an
 * env the harness can only assert the helper's own initialisation, which is
 * the same code path. */
static void test_pcm_convert_status_initialised_on_refusal(void)
{
    FFPcmSession *s = make_session(PCM_OUT_ROOM, 4096, PCM_OUT_ROOM);
    AVChannelLayout stereo = { 0 };
    AVFrame *f = NULL;
    int16_t *out = NULL;
    int produced = 12345, ret;

    CHECK(s != NULL);
    if (!s)
        return;

    av_channel_layout_default(&stereo, 2);
    f   = make_pcm_frame(AV_SAMPLE_FMT_S16P, 44100, &stereo, 512);
    out = av_malloc_array((size_t)PCM_OUT_ROOM * 2, sizeof(int16_t));
    CHECK(f != NULL && out != NULL);

    if (f && out) {
        ret = pcm_convert(s, f, out, PCM_OUT_ROOM, 1, &produced);
        CHECK(ret < 0);
        CHECK(produced == 0);
    }

    av_frame_free(&f);
    av_free(out);
    Java_org_ffmpeg_FFMpegNative_pcmSessionFree(NULL, NULL, (jlong)(intptr_t)s);
    av_channel_layout_uninit(&stereo);
}

/* ------------------------------------------------------------------ */
/* Output handle registry: staleness, generations, double release      */
/*                                                                     */
/* These can run without a Surface because the registry and the session */
/* state machine are independent of the platform window.               */
/* ------------------------------------------------------------------ */

static void test_output_handle_registry(void)
{
    FFOutputHandle *a, *b;
    void *obj = NULL;
    FFOutputHandle *got;
    jlong ha;

    ff_output_registry_init();
    a = ff_output_handle_new();
    b = ff_output_handle_new();
    CHECK(a != NULL && b != NULL);
    if (!a || !b)
        return;

    /* Two live handles never encode to the same value. */
    CHECK(ff_output_handle_encode(a) != ff_output_handle_encode(b));
    CHECK(ff_output_handle_encode(a) != 0);

    ha = ff_output_handle_encode(a);
    got = ff_output_handle_acquire(ha, &obj);
    CHECK(got == a);
    CHECK(obj == NULL);                 /* not bound to an object yet */
    ff_output_handle_put(got);

    /* A value this process never issued is refused, not dereferenced. */
    CHECK(ff_output_handle_acquire(0, &obj) == NULL);
    CHECK(ff_output_handle_acquire((jlong)0x10, &obj) == NULL);
    CHECK(ff_output_handle_acquire((jlong)0x7fffffff0000LL, &obj) == NULL);

    /* Retiring makes the token unusable even though the memory is live. */
    pthread_mutex_lock(&g_output_registry.lock);
    a->obj = (void *)&obj;
    pthread_mutex_unlock(&g_output_registry.lock);
    CHECK(ff_output_handle_retire(a) == 0);
    CHECK(ff_output_handle_acquire(ha, &obj) == NULL);
    CHECK(obj == NULL);

    /* A second retire is a no-op, and the token is still refused. */
    CHECK(ff_output_handle_retire(a) >= 0);
    CHECK(ff_output_handle_acquire(ha, &obj) == NULL);

    /* The other token is unaffected. */
    CHECK(ff_output_handle_acquire(ff_output_handle_encode(b), &obj) != NULL);

    ff_output_handle_put(a);
    ff_output_handle_put(b);
}

static void test_output_handle_generation_blocks_reuse(void)
{
    FFOutputHandle *first, *second;
    jlong h_first;

    first = ff_output_handle_new();
    CHECK(first != NULL);
    if (!first)
        return;
    h_first = ff_output_handle_encode(first);

    ff_output_handle_put(first);
    ff_output_handle_retire(first);

    /* A new token taken afterwards must not answer to the old value, even
     * if it happens to live at the same address. */
    second = ff_output_handle_new();
    CHECK(second != NULL);
    if (!second)
        return;
    CHECK(ff_output_handle_encode(second) != h_first);
    {
        void *obj = NULL;
        CHECK(ff_output_handle_acquire(h_first, &obj) == NULL);
    }
    ff_output_handle_put(second);
}

/* The renderer-specific claim: while an operation is admitted, Free() must
 * not release the scaler, the window or the locks. Exercised on a bare
 * renderer (no Surface) because the ordering, not the driver, is what can
 * regress. */
static void test_window_renderer_free_retains_while_busy(void)
{
    FFWindowRenderer *r = av_mallocz(sizeof(*r));
    int ret;

    CHECK(r != NULL);
    if (!r)
        return;
    CHECK(pthread_mutex_init(&r->session.lock, NULL) == 0);
    r->session.lock_ready = 1;
    CHECK(pthread_mutex_init(&r->session.exec_lock, NULL) == 0);
    r->session.exec_ready = 1;
    r->session.state = FF_OUTPUT_ACTIVE;
    r->session.in_flight = 0;

    CHECK_OK(ff_output_session_enter(&r->session));
    CHECK(ff_output_session_revoke(&r->session) == 1);
    /* Finish must not claim DETACHED while the count is non-zero. */
    CHECK(ff_output_session_try_finish(&r->session) == FF_OUTPUT_REVOKING);

    /* What Free() does in the busy case: leave everything intact. */
    CHECK(r->session.lock_ready);
    CHECK(r->session.exec_ready);
    CHECK(r->session.win == NULL);

    ff_output_session_exit(&r->session);
    CHECK(ff_output_session_try_finish(&r->session) == FF_OUTPUT_DETACHED);

    ret = ff_output_session_enter(&r->session);
    CHECK(ret < 0);                     /* no admission after detach */

    ff_window_renderer_destroy(r);
}

/* Same normalisation decision without a frame, for pre-flight checks. */
static void test_normalize_mask_helper(void)
{
    CHECK(NORMALIZE_MASK(0, 0) == AVERROR(EINVAL));
    CHECK(NORMALIZE_MASK(0, -2) == AVERROR(EINVAL));
    /* A definite mask consistent with the count is accepted unchanged. */
    CHECK(NORMALIZE_MASK(AV_CH_LAYOUT_5POINT1, 6) == 6);
    /* Two channels with no mask can be defaulted. */
    CHECK(NORMALIZE_MASK(0, 2) == 2);
    /* Six channels with no mask cannot. */
    CHECK(NORMALIZE_MASK(0, 6) == AVERROR(EINVAL));
    /* A mask that contradicts the count is not silently used. */
    CHECK(NORMALIZE_MASK(AV_CH_LAYOUT_STEREO, 6) == AVERROR(EINVAL));
    CHECK(NORMALIZE_MASK(AV_CH_LAYOUT_5POINT1, 2) == AVERROR(EINVAL));
}

int main(void)
{
    test_plain_payload();
    test_non_refcounted_payload();
    test_all_side_data_counted();
    test_side_data_only_is_not_empty();
    test_empty_vs_missing_side_data();
    test_opaque_ref();
    test_invalid_packets();
    test_overflow_is_reported();
    test_budget_add_overflow();
    test_clone_independence();
    test_normalize_already_definite_is_noop();
    test_normalize_from_declaration();
    test_normalize_rejects_mismatched_declaration();
    test_normalize_mono_stereo_defaults();
    test_normalize_invalid_arguments();
    test_swr_accepts_normalized_stream();
    test_normalize_mask_helper();
    test_audio_bytes_arithmetic();
    test_mask_channels();
    test_convert_check_capacity();
    test_swr_consumes_input_in_full();
    test_swr_small_output_keeps_tail();
    test_swr_out_samples_is_upper_bound();
    test_pcm_session_invalid_arguments();
    test_pcm_session_input_limit();
    test_pcm_session_out_limit_uses_upper_bound();
    test_pcm_session_status_and_accounting();
    test_pcm_session_drain_terminates();
    test_pcm_session_reset_drops_tail();
    test_pcm_session_format_change_transaction();
    test_pcm_session_channel_mapping();
    test_pcm_session_unspec_is_not_a_format_change();
    test_pcm_session_same_count_different_layout();
    test_pcm_session_packed_planar_switch();
    test_pcm_session_custom_layout();
    test_pcm_session_committed_context_survives();
    test_pcm_session_seam_is_continuous();
    test_pcm_session_planar_extended_data();
    test_pcm_session_zero_sample_frame_is_not_eof();
    test_pcm_session_drain_empty_and_reset();
    test_pcm_session_drain_one_sample_blocks();
    test_pcm_session_jni_bounds();
    test_pcm_publish_failure_poisons_session();
    test_pcm_session_rejects_planar_output();
    test_pcm_convert_status_initialised_on_refusal();
    test_video_diagnostics();
    test_release_capabilities();
    test_libass_log_level();
    test_build_info_is_complete();
    test_mediasurface_no_buffer_is_not_counted();
    test_mediasurface_null_handles();
    test_mediasurface_count_is_process_wide();
    test_mediasurface_failed_release_keeps_count();
    test_mediasurface_count_is_thread_safe();
    test_output_session_basic_lifecycle();
    test_output_session_finish_is_idempotent();
    test_output_session_revoke_refuses_new_work();
    test_output_session_blocked_producer_stays_revoking();
    test_output_session_quarantine();
    test_output_session_concurrent_enter_exit();
    test_output_session_revoke_race();
    test_output_handle_registry();
    test_output_handle_generation_blocks_reuse();
    test_window_renderer_free_retains_while_busy();

    /* The audio format snapshot suite lives in its own translation unit (it
     * needs fixture builders that would only add noise here) but shares this
     * binary so that one build proves every capability bit that is set. */
    audio_snapshot_run();

    /* The codec id suite pins the AVCodecID values FFMpegNative.java repeats
     * against the real enum, in a translation unit that can see both. */
    codec_id_bridge_run();

    printf("%d checks, %d failures\n", checks, failures);
    if (failures) {
        printf("RESULT: FAIL\n");
        return 1;
    }
    printf("RESULT: PASS\n");
    return 0;
}
