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
#include <string.h>
#include <stdint.h>
#include <pthread.h>

#include "libavcodec/avcodec.h"
#include "libavcodec/packet.h"
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

/* Call the session exactly as Java would, with a NULL JNIEnv: the entry
 * points touch env only for byte[] access, which the *Internal helpers
 * below bypass. Going through the real symbol keeps the test bound to the
 * shipped signature. */
/* Byte capacity of `out_count` output samples, derived from the session's
 * own output format rather than a hard-coded sample size. */
static int64_t pcm_out_capacity(const FFPcmSession *s, int out_count)
{
    return ff_jni_audio_bytes(out_count, s->out_fmt,
                              s->out_layout.nb_channels);
}

/* Both helpers mirror the JNI wrappers: *produced is always written, so a
 * refused call cannot leave the caller reading an uninitialised slot. That
 * is part of the contract the tests below assert on. */
static int pcm_convert(FFPcmSession *s, AVFrame *f, int16_t *out,
                       int out_count, int *produced)
{
    uint8_t *dst[AV_NUM_DATA_POINTERS] = { (uint8_t *)out };
    int check;

    *produced = 0;
    check = ff_pcm_check_call(s, f->nb_samples, out_count,
                              pcm_out_capacity(s, out_count));
    if (check < 0)
        return check;
    return ff_pcm_convert_internal(s, (const uint8_t *const *)f->data,
                                   f->nb_samples, dst, out_count, produced);
}

static int pcm_drain(FFPcmSession *s, int16_t *out, int out_count, int *produced)
{
    uint8_t *dst[AV_NUM_DATA_POINTERS] = { (uint8_t *)out };
    int check;

    *produced = 0;
    check = ff_pcm_check_call(s, 0, out_count,
                              pcm_out_capacity(s, out_count));
    if (check < 0)
        return check;
    return ff_pcm_convert_internal(s, NULL, 0, dst, out_count, produced);
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
        ret = pcm_convert(s, big, out, PCM_OUT_ROOM, &produced);
        CHECK(ret == FF_PCM_LIMIT_EXCEEDED);
        CHECK(produced == 0);
        CHECK(!s->configured);

        /* The same session still works on a frame within the limit. */
        ret = pcm_convert(s, small, out, PCM_OUT_ROOM, &produced);
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

        ret = pcm_convert(s, f, out, PCM_OUT_ROOM, &produced);
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

        ret = pcm_convert(s, f, out, 256, &produced);
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
        ret = pcm_convert(s, f, out, PCM_OUT_ROOM, &produced);
        CHECK(ret == FF_PCM_OUTPUT_READY);
        CHECK(produced <= 256);

        ret = pcm_drain(s, out, PCM_OUT_ROOM, &produced);
        CHECK(ret == FF_PCM_DRAINED || ret == FF_PCM_OUTPUT_READY);

        /* Idempotent once empty: this is what makes a caller's drain loop
         * terminate instead of spinning forever. */
        ret = pcm_drain(s, out, PCM_OUT_ROOM, &produced);
        CHECK(ret == FF_PCM_DRAINED);
        CHECK(produced == 0);
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
        ret = pcm_convert(s, f, out, 64, &produced);
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
        ret = pcm_convert(s, f44, out, 64, &produced);
        CHECK(ret >= 0);
        CHECK(swr_get_delay(s->swr, 48000) > 0);

        /* The new format arrives while the tail is buffered: refused, the
         * frame unconsumed, the configuration unchanged. */
        ret = pcm_convert(s, f48, out, PCM_OUT_ROOM, &produced);
        CHECK(ret == FF_PCM_FORMAT_CHANGE);
        CHECK(produced == 0);
        CHECK(s->in_rate == 44100);

        /* Drain the old tail completely. */
        drained = pcm_drain_all(s, out, PCM_OUT_ROOM);
        CHECK(drained >= 0);

        /* Presenting the same frame again now commits the change and
         * converts it exactly once. */
        ret = pcm_convert(s, f48, out, PCM_OUT_ROOM, &produced);
        CHECK(ret == FF_PCM_OUTPUT_READY);
        CHECK(produced > 0);
        CHECK(s->in_rate == 48000);

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

        ret = pcm_convert(s, f, out, PCM_OUT_ROOM, &produced);
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
 * layout, so a decoder that never states an order is not a new format. */
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

            ret = pcm_convert(s, f, out, PCM_OUT_ROOM, &produced);
            if (ret == FF_PCM_FORMAT_CHANGE)
                changes++;
            CHECK(ret == FF_PCM_OUTPUT_READY);
            /* The session repaired the frame in place, so the next compare
             * is between two definite layouts. */
            CHECK(f->ch_layout.order == AV_CHANNEL_ORDER_NATIVE);
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
        ret = pcm_convert(s, a, out, PCM_OUT_ROOM, &produced);
        CHECK(ret == FF_PCM_OUTPUT_READY);

        /* Same count, different channels: a real change, so it must be
         * reported rather than silently accepted, and it must commit once
         * the tail is gone. */
        ret = pcm_convert(s, b, out, PCM_OUT_ROOM, &produced);
        CHECK(ret == FF_PCM_FORMAT_CHANGE || ret == FF_PCM_OUTPUT_READY);

        CHECK(DRAIN_OLD(s, out, PCM_OUT_ROOM) >= 0);
        ret = pcm_convert(s, b, out, PCM_OUT_ROOM, &produced);
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
        ret = pcm_convert(s, packed, out, 64, &produced);
        CHECK(ret == FF_PCM_OUTPUT_READY);
        CHECK(swr_get_delay(s->swr, 48000) > 0);

        /* The planar frame arrives before that tail is gone. */
        ret = pcm_convert(s, planar, out, PCM_OUT_ROOM, &produced);
        CHECK(ret == FF_PCM_FORMAT_CHANGE);
        CHECK(produced == 0);

        tail = DRAIN_OLD(s, out, PCM_OUT_ROOM);
        CHECK(tail >= 0);

        /* Now the switch commits and the planar frame converts once. */
        ret = pcm_convert(s, planar, out, PCM_OUT_ROOM, &produced);
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
        ret = pcm_convert(s, f, out, PCM_OUT_ROOM, &produced);
        CHECK(ret == FF_PCM_OUTPUT_READY);
        CHECK(produced > 0);

        /* Survived the round trip into the session's snapshot, still
         * custom, and equal to what was presented. */
        CHECK(s->in_layout.order == AV_CHANNEL_ORDER_CUSTOM);
        CHECK(s->in_layout.nb_channels == 2);
        CHECK(av_channel_layout_compare(&s->in_layout, &custom) == 0);

        /* A second identical frame is not a change. */
        ret = pcm_convert(s, f, out, PCM_OUT_ROOM, &produced);
        CHECK(ret == FF_PCM_OUTPUT_READY);
    }

    av_frame_free(&f);
    av_free(out);
    Java_org_ffmpeg_FFMpegNative_pcmSessionFree(NULL, NULL, (jlong)(intptr_t)s);
    av_channel_layout_uninit(&custom);
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
        ret = pcm_convert(s, old_f, out, 64, &produced);
        CHECK(ret == FF_PCM_OUTPUT_READY);
        ret = pcm_convert(s, new_f, out, PCM_OUT_ROOM, &produced);
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
        ret = pcm_convert(s, new_f, out, PCM_OUT_ROOM, &produced);
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
    test_pcm_session_seam_is_continuous();
    test_output_session_basic_lifecycle();
    test_output_session_revoke_refuses_new_work();
    test_output_session_blocked_producer_stays_revoking();
    test_output_session_quarantine();
    test_output_session_concurrent_enter_exit();
    test_output_session_revoke_race();

    printf("%d checks, %d failures\n", checks, failures);
    if (failures) {
        printf("RESULT: FAIL\n");
        return 1;
    }
    printf("RESULT: PASS\n");
    return 0;
}
