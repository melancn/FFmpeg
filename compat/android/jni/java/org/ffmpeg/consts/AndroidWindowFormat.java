package org.ffmpeg.consts;

/**
 * {@code ANativeWindow} buffer formats (mirrors {@code AHardwareBuffer_Format}).
 */
public final class AndroidWindowFormat {
    private AndroidWindowFormat() {
    }

    public static final int WINDOW_FORMAT_RGBA_8888 = 1;
    public static final int WINDOW_FORMAT_RGBX_8888 = 2;
    public static final int WINDOW_FORMAT_RGB_565 = 4;
}
