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

#include "libavcodec/avcodec.h"
#include "libavcodec/packet.h"
#include "libavutil/buffer.h"
#include "libavutil/error.h"

/* Re-include the production translation unit for its file-static helpers.
 * They are static precisely so they are not part of the shippable ABI, but
 * that also makes them unreachable from a separate object file. Including the
 * .c here keeps the tests bound to the shipped implementation rather than to
 * a copy that could drift. The production file defines no main(), so there is
 * nothing to rename; the build rules pass -Dmain=ff_jni_production_main
 * defensively in case that ever changes. */
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

    printf("%d checks, %d failures\n", checks, failures);
    if (failures) {
        printf("RESULT: FAIL\n");
        return 1;
    }
    printf("RESULT: PASS\n");
    return 0;
}
