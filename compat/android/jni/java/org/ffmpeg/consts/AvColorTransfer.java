package org.ffmpeg.consts;

/** Transfer characteristics, {@code AVCOL_TRC_*}. */
public final class AvColorTransfer {
    private AvColorTransfer() {
    }

    public static final int AVCOL_TRC_RESERVED0 = 0;
    public static final int AVCOL_TRC_BT709 = 1;
    public static final int AVCOL_TRC_UNSPECIFIED = 2;
    public static final int AVCOL_TRC_RESERVED = 3;
    public static final int AVCOL_TRC_GAMMA22 = 4;
    public static final int AVCOL_TRC_GAMMA28 = 5;
    public static final int AVCOL_TRC_SMPTE170M = 6;
    public static final int AVCOL_TRC_SMPTE240M = 7;
    public static final int AVCOL_TRC_LINEAR = 8;
    public static final int AVCOL_TRC_LOG = 9;
    public static final int AVCOL_TRC_LOG_SQRT = 10;
    public static final int AVCOL_TRC_IEC61966_2_4 = 11;
    public static final int AVCOL_TRC_BT1361_ECG = 12;
    public static final int AVCOL_TRC_IEC61966_2_1 = 13;
    public static final int AVCOL_TRC_BT2020_10 = 14;
    public static final int AVCOL_TRC_BT2020_12 = 15;
    public static final int AVCOL_TRC_SMPTEST2084 = 16;
    public static final int AVCOL_TRC_SMPTE2084 = 16;
    public static final int AVCOL_TRC_SMPTEST428_1 = 17;
    public static final int AVCOL_TRC_SMPTE428 = 17;
    public static final int AVCOL_TRC_ARIB_STD_B67 = 18;
    public static final int AVCOL_TRC_V_LOG = 256;
    public static final int AVCOL_TRC_EXT_BASE = 256;
    public static final int AVCOL_TRC_EXT_NB = 257;
    public static final int AVCOL_TRC_NB = 19;
}
