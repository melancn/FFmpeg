package org.ffmpeg.consts;

/** AVOption flag bits, {@code AV_OPT_FLAG_*}. */
public final class AvOptionFlag {
    private AvOptionFlag() {
    }

    public static final int AV_OPT_FLAG_ENCODING_PARAM = 1 << 0;
    public static final int AV_OPT_FLAG_DECODING_PARAM = 1 << 1;
    public static final int AV_OPT_FLAG_AUDIO_PARAM = 1 << 3;
    public static final int AV_OPT_FLAG_VIDEO_PARAM = 1 << 4;
    public static final int AV_OPT_FLAG_SUBTITLE_PARAM = 1 << 5;
    public static final int AV_OPT_FLAG_EXPORT = 1 << 6;
    public static final int AV_OPT_FLAG_READONLY = 1 << 7;
    public static final int AV_OPT_FLAG_BSF_PARAM = 1 << 8;
    public static final int AV_OPT_FLAG_RUNTIME_PARAM = 1 << 15;
    public static final int AV_OPT_FLAG_FILTERING_PARAM = 1 << 16;
    public static final int AV_OPT_FLAG_DEPRECATED = 1 << 17;
}
