package org.ffmpeg.consts;

/** Hardware configuration methods, {@code AV_CODEC_HW_CONFIG_METHOD_*}. */
public final class AvCodecHwConfigMethod {
    private AvCodecHwConfigMethod() {
    }

    public static final int AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX = 1;
    public static final int AV_CODEC_HW_CONFIG_METHOD_HW_FRAMES_CTX = 2;
    public static final int AV_CODEC_HW_CONFIG_METHOD_INTERNAL = 4;
    public static final int AV_CODEC_HW_CONFIG_METHOD_AD_HOC = 8;
}
