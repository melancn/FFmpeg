package org.ffmpeg.consts;

/**
 * Channel indices, {@code enum AVChannel}, from
 * {@code libavutil/channel_layout.h}.
 */
public final class AvChannel {
    private AvChannel() {
    }

    public static final int AV_CHAN_NONE = -1;
    public static final int AV_CHAN_FRONT_LEFT = 0;
    public static final int AV_CHAN_FRONT_RIGHT = 1;
    public static final int AV_CHAN_FRONT_CENTER = 2;
    public static final int AV_CHAN_LOW_FREQUENCY = 3;
    public static final int AV_CHAN_BACK_LEFT = 4;
    public static final int AV_CHAN_BACK_RIGHT = 5;
    public static final int AV_CHAN_FRONT_LEFT_OF_CENTER = 6;
    public static final int AV_CHAN_FRONT_RIGHT_OF_CENTER = 7;
    public static final int AV_CHAN_BACK_CENTER = 8;
    public static final int AV_CHAN_SIDE_LEFT = 9;
    public static final int AV_CHAN_SIDE_RIGHT = 10;
    public static final int AV_CHAN_TOP_CENTER = 11;
    public static final int AV_CHAN_TOP_FRONT_LEFT = 12;
    public static final int AV_CHAN_TOP_FRONT_CENTER = 13;
    public static final int AV_CHAN_TOP_FRONT_RIGHT = 14;
    public static final int AV_CHAN_TOP_BACK_LEFT = 15;
    public static final int AV_CHAN_TOP_BACK_CENTER = 16;
    public static final int AV_CHAN_TOP_BACK_RIGHT = 17;
    public static final int AV_CHAN_STEREO_LEFT = 29;
    public static final int AV_CHAN_STEREO_RIGHT = 30;
    public static final int AV_CHAN_WIDE_LEFT = 31;
    public static final int AV_CHAN_WIDE_RIGHT = 32;
    public static final int AV_CHAN_SURROUND_DIRECT_LEFT = 33;
    public static final int AV_CHAN_SURROUND_DIRECT_RIGHT = 34;
    public static final int AV_CHAN_LOW_FREQUENCY_2 = 35;
    public static final int AV_CHAN_TOP_SIDE_LEFT = 36;
    public static final int AV_CHAN_TOP_SIDE_RIGHT = 37;
    public static final int AV_CHAN_BOTTOM_FRONT_CENTER = 38;
    public static final int AV_CHAN_BOTTOM_FRONT_LEFT = 39;
    public static final int AV_CHAN_BOTTOM_FRONT_RIGHT = 40;
    public static final int AV_CHAN_SIDE_SURROUND_LEFT = 41;
    public static final int AV_CHAN_SIDE_SURROUND_RIGHT = 42;
    public static final int AV_CHAN_TOP_SURROUND_LEFT = 43;
    public static final int AV_CHAN_TOP_SURROUND_RIGHT = 44;
    public static final int AV_CHAN_BINAURAL_LEFT = 61;
    public static final int AV_CHAN_BINAURAL_RIGHT = 62;
    public static final int AV_CHAN_UNUSED = 512;
    public static final int AV_CHAN_UNKNOWN = 768;
    public static final int AV_CHAN_AMBISONIC_BASE = 1024;
    public static final int AV_CHAN_AMBISONIC_END = 2047;
}
