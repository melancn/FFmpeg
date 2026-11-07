/*
 * Audio format snapshot schema (v1), shared by the bridge and its tests.
 *
 * Why this is a header rather than definitions inside ffmpeg_jni.c:
 * packet_contract_test.c reaches the bridge's file-static helpers by
 * including ffmpeg_jni.c directly, so every JNI entry point already has
 * exactly one definition in that translation unit. The audio snapshot suite
 * needs the same schema constants but must NOT pull in a second copy of the
 * bridge -- that is what produced "duplicate symbol
 * Java_org_ffmpeg_FFMpegNative_getVersion". Including only this header gives
 * the suite the contract without the implementation.
 *
 * Anything that changes here changes the wire format between the .so and the
 * JAR, so FFMpegNative.java repeats every value and
 * tests/audio_format_snapshot_test.c pins them.
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

#ifndef FFMPEG_JNI_AUDIO_FORMAT_H
#define FFMPEG_JNI_AUDIO_FORMAT_H

#include "libavutil/channel_layout.h"

#define FF_JNI_AUDIO_FORMAT_SCHEMA_VERSION  1
#define FF_JNI_AUDIO_FORMAT_INFO_SLOTS     13
/* Ceiling of this bridge's bounded policy, not a claim about FFmpeg: a
 * layout with more channels is refused with E2BIG rather than clamped,
 * because a clamped layout silently mis-maps every channel past the cut. */
#define FF_JNI_AUDIO_FORMAT_MAX_CHANNELS   64

/* Placeholder written into a slot whose known bit is clear. 0 is a legal
 * value for several slots (mask, bit depths, id count), so it cannot mark
 * an unknown on its own; the known bit is the only authority. */
#define FF_JNI_AUDIO_FORMAT_UNKNOWN_VALUE  0

/* Indices into outInfo. */
#define FF_JNI_AUDIO_INFO_SCHEMA_VERSION          0
#define FF_JNI_AUDIO_INFO_KNOWN_FIELDS            1
#define FF_JNI_AUDIO_INFO_SAMPLE_FORMAT           2
#define FF_JNI_AUDIO_INFO_SAMPLE_RATE             3
#define FF_JNI_AUDIO_INFO_CHANNEL_COUNT           4
#define FF_JNI_AUDIO_INFO_CHANNEL_LAYOUT_ORDER    5
#define FF_JNI_AUDIO_INFO_NATIVE_CHANNEL_MASK     6
#define FF_JNI_AUDIO_INFO_CODEC_ID                7
#define FF_JNI_AUDIO_INFO_PROFILE                 8
#define FF_JNI_AUDIO_INFO_BITS_PER_CODED_SAMPLE   9
#define FF_JNI_AUDIO_INFO_BITS_PER_RAW_SAMPLE    10
#define FF_JNI_AUDIO_INFO_SAMPLES_PER_CHANNEL    11
#define FF_JNI_AUDIO_INFO_CHANNEL_IDS_COUNT      12

/* Bits of KNOWN_FIELDS, one per slot. Slots 0 and 1 describe the snapshot
 * itself and are always defined. */
#define FF_JNI_AUDIO_KL_SCHEMA_VERSION        (1LL << FF_JNI_AUDIO_INFO_SCHEMA_VERSION)
#define FF_JNI_AUDIO_KL_KNOWN_FIELDS          (1LL << FF_JNI_AUDIO_INFO_KNOWN_FIELDS)
#define FF_JNI_AUDIO_KL_SAMPLE_FORMAT         (1LL << FF_JNI_AUDIO_INFO_SAMPLE_FORMAT)
#define FF_JNI_AUDIO_KL_SAMPLE_RATE           (1LL << FF_JNI_AUDIO_INFO_SAMPLE_RATE)
#define FF_JNI_AUDIO_KL_CHANNEL_COUNT         (1LL << FF_JNI_AUDIO_INFO_CHANNEL_COUNT)
#define FF_JNI_AUDIO_KL_CHANNEL_LAYOUT_ORDER  (1LL << FF_JNI_AUDIO_INFO_CHANNEL_LAYOUT_ORDER)
#define FF_JNI_AUDIO_KL_NATIVE_CHANNEL_MASK   (1LL << FF_JNI_AUDIO_INFO_NATIVE_CHANNEL_MASK)
#define FF_JNI_AUDIO_KL_CODEC_ID              (1LL << FF_JNI_AUDIO_INFO_CODEC_ID)
#define FF_JNI_AUDIO_KL_PROFILE               (1LL << FF_JNI_AUDIO_INFO_PROFILE)
#define FF_JNI_AUDIO_KL_BITS_PER_CODED_SAMPLE (1LL << FF_JNI_AUDIO_INFO_BITS_PER_CODED_SAMPLE)
#define FF_JNI_AUDIO_KL_BITS_PER_RAW_SAMPLE   (1LL << FF_JNI_AUDIO_INFO_BITS_PER_RAW_SAMPLE)
#define FF_JNI_AUDIO_KL_SAMPLES_PER_CHANNEL   (1LL << FF_JNI_AUDIO_INFO_SAMPLES_PER_CHANNEL)
#define FF_JNI_AUDIO_KL_CHANNEL_IDS_COUNT     (1LL << FF_JNI_AUDIO_INFO_CHANNEL_IDS_COUNT)

/* The slots defined for every successful snapshot, regardless of carrier.
 * Everything else is added only when the value was actually read, which is
 * what makes an over-eager set bit a test failure. */
#define FF_JNI_AUDIO_KL_COMMON                                            \
    (FF_JNI_AUDIO_KL_SCHEMA_VERSION | FF_JNI_AUDIO_KL_KNOWN_FIELDS |      \
     FF_JNI_AUDIO_KL_CHANNEL_COUNT | FF_JNI_AUDIO_KL_CHANNEL_LAYOUT_ORDER | \
     FF_JNI_AUDIO_KL_CHANNEL_IDS_COUNT)

/* Channel order codes. These are FFmpeg's AVChannelOrder values reported
 * unchanged: mapping to an Android channel mask is the caller's job, and
 * doing it here would erase the difference between NATIVE and CUSTOM.
 *
 * UNSPEC is itself a defined answer -- "the layout type is not specified" --
 * not a synonym for stereo and not an unknown. They are also pinned to the
 * real enum below so a future AVChannelOrder change cannot silently shift
 * the wire format. */
#define FF_JNI_AUDIO_ORDER_UNSPEC    AV_CHANNEL_ORDER_UNSPEC
#define FF_JNI_AUDIO_ORDER_NATIVE    AV_CHANNEL_ORDER_NATIVE
#define FF_JNI_AUDIO_ORDER_CUSTOM    AV_CHANNEL_ORDER_CUSTOM
#define FF_JNI_AUDIO_ORDER_AMBISONIC AV_CHANNEL_ORDER_AMBISONIC

#endif /* FFMPEG_JNI_AUDIO_FORMAT_H */
