package org.ffmpeg.consts;

/**
 * Hardware device type constants mirroring {@code enum AVHWDeviceType}.
 *
 * <p>MediaCodec Surface zero-copy requires
 * {@link #AV_HWDEVICE_TYPE_MEDIACODEC}.
 */
public final class AvHwDeviceType {
    private AvHwDeviceType() {
    }

    public static final int AV_HWDEVICE_TYPE_MEDIACODEC = 10;
}
