package org.ffmpeg.consts;

/** Schema v1 layout of the {@code videoDecodeStats} output array. */
public final class VideoDecodeStats {
    private VideoDecodeStats() {
    }

    /** Value of the schema-version slot. */
    public static final int VIDEO_DECODE_STATS_SCHEMA_VERSION = 1;

    /** Elements required in {@code out}. */
    public static final int VIDEO_DECODE_STATS_SLOTS = 6;

    /** Always {@link #VIDEO_DECODE_STATS_SCHEMA_VERSION} for this schema. */
    public static final int VIDEO_DECODE_STATS_SCHEMA_VERSION_INDEX = 0;
    /** Frames returned by {@code codecReceiveFrame} with status 0. */
    public static final int VIDEO_DECODE_STATS_DECODED_FRAMES = 1;
    /** Successful Surface submissions through this bridge. */
    public static final int VIDEO_DECODE_STATS_RENDERED_FRAMES = 2;
    /** Frames discarded without being shown, as observed by this bridge. */
    public static final int VIDEO_DECODE_STATS_DROPPED_FRAMES = 3;
    /** Hard decode failures; EAGAIN and EOF are not counted. */
    public static final int VIDEO_DECODE_STATS_DECODE_ERRORS = 4;
    /** {@code AVCodecContext.bit_rate} in bit/s; {@code <= 0} is unknown. */
    public static final int VIDEO_DECODE_STATS_CURRENT_BITRATE = 5;
}
