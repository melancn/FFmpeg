package org.ffmpeg.consts;

/** Codec property flags, {@code AV_CODEC_PROP_*}. */
public final class AvCodecProp {
    private AvCodecProp() {
    }

    public static final int AV_CODEC_PROP_INTRA_ONLY = 1;
    public static final int AV_CODEC_PROP_LOSSY = 2;
    public static final int AV_CODEC_PROP_LOSSLESS = 4;
    public static final int AV_CODEC_PROP_REORDER = 8;
    public static final int AV_CODEC_PROP_FIELDS = 16;
    public static final int AV_CODEC_PROP_ENHANCEMENT = 32;
    public static final int AV_CODEC_PROP_BITMAP_SUB = 65536;
    public static final int AV_CODEC_PROP_TEXT_SUB = 131072;
}
