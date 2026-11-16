package org.ffmpeg.consts;

/** AVOption types, {@code AV_OPT_TYPE_*}. */
public final class AvOptionType {
    private AvOptionType() {
    }

    public static final int AV_OPT_TYPE_FLAGS = 1;
    public static final int AV_OPT_TYPE_INT = 2;
    public static final int AV_OPT_TYPE_INT64 = 3;
    public static final int AV_OPT_TYPE_DOUBLE = 4;
    public static final int AV_OPT_TYPE_FLOAT = 5;
    public static final int AV_OPT_TYPE_STRING = 6;
    public static final int AV_OPT_TYPE_RATIONAL = 7;
    public static final int AV_OPT_TYPE_BINARY = 8;
    public static final int AV_OPT_TYPE_DICT = 9;
    public static final int AV_OPT_TYPE_UINT64 = 10;
    public static final int AV_OPT_TYPE_CONST = 11;
    public static final int AV_OPT_TYPE_IMAGE_SIZE = 12;
    public static final int AV_OPT_TYPE_PIXEL_FMT = 13;
    public static final int AV_OPT_TYPE_SAMPLE_FMT = 14;
    public static final int AV_OPT_TYPE_VIDEO_RATE = 15;
    public static final int AV_OPT_TYPE_DURATION = 16;
    public static final int AV_OPT_TYPE_COLOR = 17;
    public static final int AV_OPT_TYPE_BOOL = 18;
    public static final int AV_OPT_TYPE_CHLAYOUT = 19;
    public static final int AV_OPT_TYPE_UINT = 20;
    public static final int AV_OPT_TYPE_FLAG_ARRAY = 65536;
}
