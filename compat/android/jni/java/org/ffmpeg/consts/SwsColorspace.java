package org.ffmpeg.consts;

/** SWS colorspace constants, {@code SWS_CS_*}. */
public final class SwsColorspace {
    private SwsColorspace() {
    }

    public static final int SWS_CS_ITU709 = 1;
    public static final int SWS_CS_FCC = 4;
    public static final int SWS_CS_ITU601 = 5;
    public static final int SWS_CS_ITU624 = 5;
    public static final int SWS_CS_SMPTE170M = 5;
    public static final int SWS_CS_SMPTE240M = 7;
    public static final int SWS_CS_BT2020 = 9;
    public static final int SWS_CS_DEFAULT = 5;
}
