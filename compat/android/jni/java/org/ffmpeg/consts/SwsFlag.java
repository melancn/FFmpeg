package org.ffmpeg.consts;

/** SWS algorithm flags mirroring {@code libswscale}. */
public final class SwsFlag {
    private SwsFlag() {
    }

    public static final int SWS_FAST_BILINEAR = 1;
    public static final int SWS_BILINEAR = 2;
    public static final int SWS_BICUBIC = 4;
    public static final int SWS_X = 8;
    public static final int SWS_POINT = 0x10;
    public static final int SWS_AREA = 0x20;
    public static final int SWS_BICUBLIN = 0x40;
    public static final int SWS_GAUSS = 0x80;
    public static final int SWS_SINC = 0x100;
    public static final int SWS_LANCZOS = 0x200;
    public static final int SWS_SPLINE = 0x400;
    public static final int SWS_STRICT = 0x800;
    public static final int SWS_PRINT_INFO = 0x1000;
    public static final int SWS_FULL_CHR_H_INT = 0x2000;
    public static final int SWS_FULL_CHR_H_INP = 0x4000;
    public static final int SWS_DIRECT_BGR = 0x8000;
    public static final int SWS_ACCURATE_RND = 0x40000;
    public static final int SWS_BITEXACT = 0x80000;
    public static final int SWS_ERROR_DIFFUSION = 0x800000;
    public static final int SWS_PARAM_DEFAULT = 123456;
}
