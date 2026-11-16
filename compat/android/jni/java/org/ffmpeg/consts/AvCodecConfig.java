package org.ffmpeg.consts;

/** Codec-config enum values mirroring {@code enum AVCodecConfig}. */
public final class AvCodecConfig {
    private AvCodecConfig() {
    }

    public static final int AV_CODEC_CONFIG_PIX_FORMAT = 0;
    public static final int AV_CODEC_CONFIG_FRAME_RATE = 1;
    public static final int AV_CODEC_CONFIG_SAMPLE_RATE = 2;
    public static final int AV_CODEC_CONFIG_SAMPLE_FORMAT = 3;
    public static final int AV_CODEC_CONFIG_CHANNEL_LAYOUT = 4;
    public static final int AV_CODEC_CONFIG_COLOR_RANGE = 5;
    public static final int AV_CODEC_CONFIG_COLOR_SPACE = 6;
    public static final int AV_CODEC_CONFIG_ALPHA_MODE = 7;
}
