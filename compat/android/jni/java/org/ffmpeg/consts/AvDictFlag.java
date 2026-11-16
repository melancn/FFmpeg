package org.ffmpeg.consts;

/** Dictionary flags mirroring {@code AV_DICT_*}. */
public final class AvDictFlag {
    private AvDictFlag() {
    }

    public static final int AV_DICT_MATCH_CASE = 1;
    public static final int AV_DICT_IGNORE_SUFFIX = 2;
    public static final int AV_DICT_DONT_STRDUP_KEY = 4;
    public static final int AV_DICT_DONT_STRDUP_VAL = 8;
    public static final int AV_DICT_DONT_STRDUP = AV_DICT_DONT_STRDUP_KEY;
    public static final int AV_DICT_DONT_OVERWRITE = 16;
    public static final int AV_DICT_APPEND = 32;
    public static final int AV_DICT_MULTIKEY = 64;
    public static final int AV_DICT_DEDUP = 128;
}
