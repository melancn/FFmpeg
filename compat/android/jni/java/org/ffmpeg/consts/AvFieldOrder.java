package org.ffmpeg.consts;

/** Field-order values, {@code enum AVFieldOrder}. */
public final class AvFieldOrder {
    private AvFieldOrder() {
    }

    public static final int AV_FIELD_UNKNOWN = 0;
    public static final int AV_FIELD_PROGRESSIVE = 1;
    public static final int AV_FIELD_TT = 2;
    public static final int AV_FIELD_BB = 3;
    public static final int AV_FIELD_TB = 4;
    public static final int AV_FIELD_BT = 5;
}
