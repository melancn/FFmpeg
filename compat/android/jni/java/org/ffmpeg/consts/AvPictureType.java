package org.ffmpeg.consts;

/** Picture-type constants, {@code enum AVPictureType}. */
public final class AvPictureType {
    private AvPictureType() {
    }

    public static final int AV_PICTURE_TYPE_NONE = 0;
    public static final int AV_PICTURE_TYPE_I = 1;
    public static final int AV_PICTURE_TYPE_P = 2;
    public static final int AV_PICTURE_TYPE_B = 3;
    public static final int AV_PICTURE_TYPE_S = 4;
    public static final int AV_PICTURE_TYPE_SI = 5;
    public static final int AV_PICTURE_TYPE_SP = 6;
    public static final int AV_PICTURE_TYPE_BI = 7;
}
