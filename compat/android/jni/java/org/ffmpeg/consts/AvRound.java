package org.ffmpeg.consts;

/** Rounding modes mirroring {@code enum AVRounding}. */
public final class AvRound {
    private AvRound() {
    }

    public static final int AV_ROUND_ZERO = 0;
    public static final int AV_ROUND_INF = 1;
    public static final int AV_ROUND_DOWN = 2;
    public static final int AV_ROUND_UP = 3;
    public static final int AV_ROUND_NEAR_INF = 5;
    public static final int AV_ROUND_PASS_MINMAX = 8192;
}
