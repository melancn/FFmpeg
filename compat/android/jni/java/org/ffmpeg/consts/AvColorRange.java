package org.ffmpeg.consts;

/** Color range, {@code AVCOL_RANGE_*}. */
public final class AvColorRange {
    private AvColorRange() {
    }

    public static final int AVCOL_RANGE_UNSPECIFIED = 0;
    public static final int AVCOL_RANGE_MPEG = 1;
    public static final int AVCOL_RANGE_JPEG = 2;
    public static final int AVCOL_RANGE_NB = 3;
}
