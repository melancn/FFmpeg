package org.ffmpeg.consts;

/** Pixel-format constants mirroring {@code enum AVPixelFormat}. */
public final class AvPixelFormat {
    private AvPixelFormat() {
    }

    public static final int AV_PIX_FMT_NONE = -1;
    public static final int AV_PIX_FMT_YUV420P = 0;
    public static final int AV_PIX_FMT_YUYV422 = 1;
    public static final int AV_PIX_FMT_RGB24 = 2;
    public static final int AV_PIX_FMT_BGR24 = 3;
    public static final int AV_PIX_FMT_YUV422P = 4;
    public static final int AV_PIX_FMT_YUV444P = 5;
    public static final int AV_PIX_FMT_NV21 = 24;
    public static final int AV_PIX_FMT_RGBA = 26;
    public static final int AV_PIX_FMT_BGRA = 28;
    public static final int AV_PIX_FMT_YUV420P10LE = 62;
    public static final int AV_PIX_FMT_YUV422P10LE = 64;
    public static final int AV_PIX_FMT_YUV444P10LE = 68;
    public static final int AV_PIX_FMT_P010LE = 158;

    /**
     * Software NV12 (Y + interleaved UV). The mediacodec decoder falls back
     * to this format (mapped from the platform {@code COLOR_FormatYUV420*}
     * variants) when no Surface is attached via {@code hw_device_ctx}; decoded
     * frames live in ordinary host buffers, not in {@code AVMediaCodecBuffer}.
     * Asserting against this value after open2 helps detect the fallback.
     */
    public static final int AV_PIX_FMT_NV12 = 23;

    /**
     * Android MediaCodec hardware pixel format. Only frames in this format
     * carry a non-null {@code AVMediaCodecBuffer*} in {@code frame->data[3]}.
     * Selected by the mediacodec decoder iff {@code avctx->hw_device_ctx} is
     * an {@link AvHwDeviceType#AV_HWDEVICE_TYPE_MEDIACODEC} device whose
     * {@code hwctx->surface} is the App Surface.
     */
    public static final int AV_PIX_FMT_MEDIACODEC = 164;
}
