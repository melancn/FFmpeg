package org.ffmpeg.consts;

/** Matrix encoding values, {@code enum AVMatrixEncoding}. */
public final class AvMatrixEncoding {
    private AvMatrixEncoding() {
    }

    public static final int AV_MATRIX_ENCODING_NONE = 0;
    public static final int AV_MATRIX_ENCODING_DOLBY = 1;
    public static final int AV_MATRIX_ENCODING_DPLII = 2;
    public static final int AV_MATRIX_ENCODING_DPLIIX = 3;
    public static final int AV_MATRIX_ENCODING_DPLIIZ = 4;
    public static final int AV_MATRIX_ENCODING_DOLBYEX = 5;
    public static final int AV_MATRIX_ENCODING_DOLBYHEADPHONE = 6;
    public static final int AV_MATRIX_ENCODING_NB = 7;
}
