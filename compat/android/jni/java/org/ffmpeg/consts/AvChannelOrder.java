package org.ffmpeg.consts;

/** Channel orders, {@code enum AVChannelOrder}. */
public final class AvChannelOrder {
    private AvChannelOrder() {
    }

    public static final int AV_CHANNEL_ORDER_UNSPEC = 0;
    public static final int AV_CHANNEL_ORDER_NATIVE = 1;
    public static final int AV_CHANNEL_ORDER_CUSTOM = 2;
    public static final int AV_CHANNEL_ORDER_AMBISONIC = 3;
    public static final int FF_CHANNEL_ORDER_NB = 4;
}
