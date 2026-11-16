package org.ffmpeg.consts;

/** Misc constants mirrored from {@code libavcodec/defs.h}. */
public final class AvCodecDefs {
    private AvCodecDefs() {
    }

    public static final int AV_INPUT_BUFFER_PADDING_SIZE = 64;

    /** Error-detection flags, {@code AV_EF_*}. */
    public static final int AV_EF_CRCCHECK = 1;
    public static final int AV_EF_BITSTREAM = 2;
    public static final int AV_EF_BUFFER = 4;
    public static final int AV_EF_EXPLODE = 8;
    public static final int AV_EF_IGNORE_ERR = 0x8000;
    public static final int AV_EF_CAREFUL = 0x10000;
    public static final int AV_EF_COMPLIANT = 0x20000;
    public static final int AV_EF_AGGRESSIVE = 0x40000;

    /** Compliance levels, {@code FF_COMPLIANCE_*}. */
    public static final int FF_COMPLIANCE_VERY_STRICT = 2;
    public static final int FF_COMPLIANCE_STRICT = 1;
    public static final int FF_COMPLIANCE_NORMAL = 0;
    public static final int FF_COMPLIANCE_UNOFFICIAL = -1;
    public static final int FF_COMPLIANCE_EXPERIMENTAL = -2;
}
