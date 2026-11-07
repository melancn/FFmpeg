/*
 * Pins the AVCodecID constants that the JAR repeats, against the real enum.
 *
 * Why this suite exists: FFMpegNative.java mirrors a handful of
 * enum AVCodecID values because Java code needs them for routing decisions
 * (which bitstream filter to insert, which ENCODING_* sink accepts the
 * stream). Those Java literals cannot include libavcodec/codec_id.h, so
 * nothing used to notice when one of them was transcribed wrongly. That is
 * exactly what happened to AV_CODEC_ID_EAC3 and AV_CODEC_ID_OPUS: the audio
 * block is a dense sequence of implied values starting at
 * AV_CODEC_ID_MP2 = 0x15000, and counting header *lines* instead of enum
 * *enumerators* produced 0x15038/0x1504c instead of 0x15028/0x1503c. The
 * numbers stayed plausible and the error was invisible at both ends.
 *
 * The only honest check is against the compiler's own answer, so this file
 * asserts the enum values directly and then repeats every Java literal next
 * to them. A translation unit here sees the real enum; the JAR cannot.
 *
 * Adding a mirrored constant to FFMpegNative.java means adding it here too:
 * a literal nobody pins is a literal that can drift again.
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
#include <stdint.h>

#include "libavcodec/codec_id.h"

/* The literals below are copies of FFMpegNative.java, not of codec_id.h.
 * They are written in the Java source's own hex form so that a transcription
 * slip in either direction shows up as a failed comparison rather than as
 * two copies of one shared mistake. */

/* Java: AV_CODEC_ID_PCM_S16LE = 0x10000 */
#define JAVA_PCM_S16LE 0x10000
/* Java: AV_CODEC_ID_AMR_NB = 0x12000 */
#define JAVA_AMR_NB    0x12000
/* Java: AV_CODEC_ID_AMR_WB = 73729 */
#define JAVA_AMR_WB    73729
/* Java: AV_CODEC_ID_MP3 = 0x15001 */
#define JAVA_MP3       0x15001
/* Java: AV_CODEC_ID_AAC = 0x15002 */
#define JAVA_AAC       0x15002
/* Java: AV_CODEC_ID_AC3 = 0x15003 */
#define JAVA_AC3       0x15003
/* Java: AV_CODEC_ID_DTS = 0x15004 */
#define JAVA_DTS       0x15004
/* Java: AV_CODEC_ID_VORBIS = 0x15005 */
#define JAVA_VORBIS    0x15005
/* Java: AV_CODEC_ID_FLAC = 0x1500c */
#define JAVA_FLAC      0x1500c
/* Java: AV_CODEC_ID_ALAC = 0x15010 */
#define JAVA_ALAC      0x15010
/* Java: AV_CODEC_ID_EAC3 = 0x15028 (86056) */
#define JAVA_EAC3      0x15028
/* Java: AV_CODEC_ID_OPUS = 0x1503C (86076) */
#define JAVA_OPUS      0x1503C

static int g_checks;
static int g_failures;

#define CHECK(cond) do {                                                      \
    g_checks++;                                                               \
    if (!(cond)) {                                                            \
        g_failures++;                                                         \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
    }                                                                         \
} while (0)

/* The values the JAR must carry, as decimal. Spelled out separately from the
 * hex above: a wrong hex/decimal pair inside one comment is invisible, and a
 * consumer that logs a codec id as a decimal is a normal thing to do. */
#define DEC_EAC3 86056
#define DEC_OPUS 86076

/* The two constants this suite was written for, asserted at compile time as
 * well as at run time: a drift in codec_id.h then cannot reach a device, and
 * the failure names the constant. */
_Static_assert(AV_CODEC_ID_EAC3 == 0x15028,
               "AV_CODEC_ID_EAC3 moved: update FFMpegNative.AV_CODEC_ID_EAC3 "
               "and the Java literals pinned in this file");
_Static_assert(AV_CODEC_ID_OPUS == 0x1503C,
               "AV_CODEC_ID_OPUS moved: update FFMpegNative.AV_CODEC_ID_OPUS "
               "and the Java literals pinned in this file");

static void test_audio_block_base(void)
{
    /* The base of the audio block, and the one implied value that is not
     * 0x1500n: AMR_NB is an explicit alias target inside the block. */
    CHECK(AV_CODEC_ID_MP2 == 0x15000);
    CHECK(AV_CODEC_ID_MP2 == 86016);

    /* Enumerators that do state their own value, so this pins the anchors the
     * implied sequence is counted from. */
    CHECK(AV_CODEC_ID_PCM_S16LE == JAVA_PCM_S16LE);
    CHECK(AV_CODEC_ID_AMR_NB     == JAVA_AMR_NB);
    CHECK(AV_CODEC_ID_AMR_WB     == JAVA_AMR_WB);
}

static void test_java_literals_match_enum(void)
{
    /* Dense part of the audio block: MP3 onward is implied, so every one of
     * these would shift together if an enumerator were inserted above it.
     * Each Java copy is compared to the enum, never to another copy. */
    CHECK(JAVA_MP3    == AV_CODEC_ID_MP3);
    CHECK(JAVA_AAC    == AV_CODEC_ID_AAC);
    CHECK(JAVA_AC3    == AV_CODEC_ID_AC3);
    CHECK(JAVA_DTS    == AV_CODEC_ID_DTS);
    CHECK(JAVA_VORBIS == AV_CODEC_ID_VORBIS);
    CHECK(JAVA_FLAC   == AV_CODEC_ID_FLAC);
    CHECK(JAVA_ALAC   == AV_CODEC_ID_ALAC);
    CHECK(JAVA_EAC3   == AV_CODEC_ID_EAC3);
    CHECK(JAVA_OPUS   == AV_CODEC_ID_OPUS);

    /* The envelopes FFMpegNative documents: the whole audio block lies in
     * [0x15000, 0x16000), and no mirrored constant collides with another.
     * A literal that landed in the video block would satisfy nothing above
     * and mis-route every stream that carries it. */
    CHECK(JAVA_MP3    >= 0x15000 && JAVA_MP3    < 0x16000);
    CHECK(JAVA_AAC    >= 0x15000 && JAVA_AAC    < 0x16000);
    CHECK(JAVA_AC3    >= 0x15000 && JAVA_AC3    < 0x16000);
    CHECK(JAVA_DTS    >= 0x15000 && JAVA_DTS    < 0x16000);
    CHECK(JAVA_VORBIS >= 0x15000 && JAVA_VORBIS < 0x16000);
    CHECK(JAVA_FLAC   >= 0x15000 && JAVA_FLAC   < 0x16000);
    CHECK(JAVA_ALAC   >= 0x15000 && JAVA_ALAC   < 0x16000);
    CHECK(JAVA_EAC3   >= 0x15000 && JAVA_EAC3   < 0x16000);
    CHECK(JAVA_OPUS   >= 0x15000 && JAVA_OPUS   < 0x16000);

    /* Strictly ascending in declaration order, which is the order codec_id.h
     * declares them in. Equal or reversed neighbours mean a copy of one
     * constant was pasted where another belongs. */
    CHECK(JAVA_MP3    < JAVA_AAC);
    CHECK(JAVA_AAC    < JAVA_AC3);
    CHECK(JAVA_AC3    < JAVA_DTS);
    CHECK(JAVA_DTS    < JAVA_VORBIS);
    CHECK(JAVA_VORBIS < JAVA_FLAC);
    CHECK(JAVA_FLAC   < JAVA_ALAC);
    CHECK(JAVA_ALAC   < JAVA_EAC3);
    CHECK(JAVA_EAC3   < JAVA_OPUS);
}

static void test_decimal_comment_agrees(void)
{
    /* What FFMpegNative.java writes in the trailing comment, and what a
     * caller sees when it prints the id in base 10. */
    CHECK(AV_CODEC_ID_EAC3 == DEC_EAC3);
    CHECK(AV_CODEC_ID_OPUS == DEC_OPUS);
    CHECK(JAVA_EAC3 == DEC_EAC3);
    CHECK(JAVA_OPUS == DEC_OPUS);

    /* And the off-by-0x10 pair the JAR used to carry is not what the enum
     * says. Stated as an inequality so the regression is recognisable by
     * its symptom, not only by the corrected number. */
    CHECK(JAVA_EAC3 != 0x15038);
    CHECK(JAVA_OPUS != 0x1504c);
}

void codec_id_bridge_run(void)
{
    test_audio_block_base();
    test_java_literals_match_enum();
    test_decimal_comment_agrees();

    printf("codec id bridge: %d checks, %d failures\n",
           g_checks, g_failures);
    /* Reported into packet_contract_test.c's counters, like the audio format
     * suite: one binary, one pass/fail line. */
    {
        extern void fftest_add_checks(int n, int f);

        fftest_add_checks(g_checks, g_failures);
    }
}
