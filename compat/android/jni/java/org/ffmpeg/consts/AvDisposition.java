package org.ffmpeg.consts;

/** Stream disposition flags, {@code AV_DISPOSITION_*}. */
public final class AvDisposition {
    private AvDisposition() {
    }

    public static final int AV_DISPOSITION_DEFAULT = 1;
    public static final int AV_DISPOSITION_DUB = 2;
    public static final int AV_DISPOSITION_ORIGINAL = 4;
    public static final int AV_DISPOSITION_COMMENT = 8;
    public static final int AV_DISPOSITION_LYRICS = 16;
    public static final int AV_DISPOSITION_KARAOKE = 32;
    public static final int AV_DISPOSITION_FORCED = 64;
    public static final int AV_DISPOSITION_HEARING_IMPAIRED = 128;
    public static final int AV_DISPOSITION_VISUAL_IMPAIRED = 256;
    public static final int AV_DISPOSITION_CLEAN_EFFECTS = 512;
    public static final int AV_DISPOSITION_ATTACHED_PIC = 1024;
    public static final int AV_DISPOSITION_TIMED_THUMBNAILS = 2048;
    public static final int AV_DISPOSITION_NON_DIEGETIC = 4096;
    public static final int AV_DISPOSITION_CAPTIONS = 65536;
    public static final int AV_DISPOSITION_DESCRIPTIONS = 131072;
    public static final int AV_DISPOSITION_METADATA = 262144;
    public static final int AV_DISPOSITION_DEPENDENT = 524288;
    public static final int AV_DISPOSITION_STILL_IMAGE = 1048576;
    public static final int AV_DISPOSITION_MULTILAYER = 2097152;
}
