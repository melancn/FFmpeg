package org.ffmpeg.consts;

/** Video metadata snapshot schema v1 and its fixed array layouts. */
public final class VideoMetadata {
    private VideoMetadata() {
    }

    public static final int VIDEO_METADATA_SCHEMA_VERSION = 1;

    public static final int DOVI_CONFIG_SLOTS = 9;
    public static final int DOVI_CONFIG_VERSION_MAJOR = 0;
    public static final int DOVI_CONFIG_VERSION_MINOR = 1;
    public static final int DOVI_CONFIG_PROFILE = 2;
    public static final int DOVI_CONFIG_LEVEL = 3;
    public static final int DOVI_CONFIG_RPU_PRESENT = 4;
    public static final int DOVI_CONFIG_EL_PRESENT = 5;
    public static final int DOVI_CONFIG_BL_PRESENT = 6;
    public static final int DOVI_CONFIG_BL_SIGNAL_COMPATIBILITY_ID = 7;
    public static final int DOVI_CONFIG_MD_COMPRESSION = 8;

    public static final int MASTERING_DISPLAY_SLOTS = 22;
    public static final int MASTERING_DISPLAY_HAS_PRIMARIES = 0;
    public static final int MASTERING_DISPLAY_HAS_LUMINANCE = 1;
    public static final int MASTERING_DISPLAY_RED_X_NUM = 2;
    public static final int MASTERING_DISPLAY_RED_X_DEN = 3;
    public static final int MASTERING_DISPLAY_RED_Y_NUM = 4;
    public static final int MASTERING_DISPLAY_RED_Y_DEN = 5;
    public static final int MASTERING_DISPLAY_GREEN_X_NUM = 6;
    public static final int MASTERING_DISPLAY_GREEN_X_DEN = 7;
    public static final int MASTERING_DISPLAY_GREEN_Y_NUM = 8;
    public static final int MASTERING_DISPLAY_GREEN_Y_DEN = 9;
    public static final int MASTERING_DISPLAY_BLUE_X_NUM = 10;
    public static final int MASTERING_DISPLAY_BLUE_X_DEN = 11;
    public static final int MASTERING_DISPLAY_BLUE_Y_NUM = 12;
    public static final int MASTERING_DISPLAY_BLUE_Y_DEN = 13;
    public static final int MASTERING_DISPLAY_WHITE_X_NUM = 14;
    public static final int MASTERING_DISPLAY_WHITE_X_DEN = 15;
    public static final int MASTERING_DISPLAY_WHITE_Y_NUM = 16;
    public static final int MASTERING_DISPLAY_WHITE_Y_DEN = 17;
    public static final int MASTERING_DISPLAY_MIN_LUMINANCE_NUM = 18;
    public static final int MASTERING_DISPLAY_MIN_LUMINANCE_DEN = 19;
    public static final int MASTERING_DISPLAY_MAX_LUMINANCE_NUM = 20;
    public static final int MASTERING_DISPLAY_MAX_LUMINANCE_DEN = 21;

    public static final int CONTENT_LIGHT_SLOTS = 2;
    public static final int CONTENT_LIGHT_MAX_CLL = 0;
    public static final int CONTENT_LIGHT_MAX_FALL = 1;

    public static final int DISPLAY_MATRIX_SLOTS = 9;
}
