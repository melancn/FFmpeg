package org.ffmpeg.consts;

/** Timestamp and time-base constants mirrored from libavutil. */
public final class AvTimeBase {
    private AvTimeBase() {
    }

    /** Undefined timestamp, mirrors FFmpeg's {@code AV_NOPTS_VALUE}. */
    public static final long AV_NOPTS_VALUE = 0x8000000000000000L;

    /** Time base: FFmpeg timestamps are in {@code AV_TIME_BASE}ths of a second (1 000 000). */
    public static final long AV_TIME_BASE = 1_000_000L;

    /**
     * Time base as a rational, identical to
     * {@code AV_TIME_BASE_Q = (AVRational){1, AV_TIME_BASE}} = 1/1 000 000.
     */
    public static final int AV_TIME_BASE_Q_NUM = 1;
    public static final int AV_TIME_BASE_Q_DEN = 1_000_000;
}
