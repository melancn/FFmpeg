package org.ffmpeg.consts;

/**
 * Channel masks, {@code AV_CH_*}, and channel-layout masks,
 * {@code AV_CH_LAYOUT_*}, from {@code libavutil/channel_layout.h}.
 * All values are 64-bit bitfields.
 */
public final class AvChannelLayout {
    private AvChannelLayout() {
    }

    public static final long AV_CH_FRONT_LEFT = 1L;
    public static final long AV_CH_FRONT_RIGHT = 2L;
    public static final long AV_CH_FRONT_CENTER = 4L;
    public static final long AV_CH_LOW_FREQUENCY = 8L;
    public static final long AV_CH_BACK_LEFT = 16L;
    public static final long AV_CH_BACK_RIGHT = 32L;
    public static final long AV_CH_FRONT_LEFT_OF_CENTER = 64L;
    public static final long AV_CH_FRONT_RIGHT_OF_CENTER = 128L;
    public static final long AV_CH_BACK_CENTER = 256L;
    public static final long AV_CH_SIDE_LEFT = 512L;
    public static final long AV_CH_SIDE_RIGHT = 1024L;
    public static final long AV_CH_TOP_CENTER = 2048L;
    public static final long AV_CH_TOP_FRONT_LEFT = 4096L;
    public static final long AV_CH_TOP_FRONT_CENTER = 8192L;
    public static final long AV_CH_TOP_FRONT_RIGHT = 16384L;
    public static final long AV_CH_TOP_BACK_LEFT = 32768L;
    public static final long AV_CH_TOP_BACK_CENTER = 65536L;
    public static final long AV_CH_TOP_BACK_RIGHT = 131072L;
    public static final long AV_CH_STEREO_LEFT = 536870912L;
    public static final long AV_CH_STEREO_RIGHT = 1073741824L;
    public static final long AV_CH_WIDE_LEFT = 2147483648L;
    public static final long AV_CH_WIDE_RIGHT = 4294967296L;
    public static final long AV_CH_SURROUND_DIRECT_LEFT = 8589934592L;
    public static final long AV_CH_SURROUND_DIRECT_RIGHT = 17179869184L;
    public static final long AV_CH_LOW_FREQUENCY_2 = 34359738368L;
    public static final long AV_CH_TOP_SIDE_LEFT = 68719476736L;
    public static final long AV_CH_TOP_SIDE_RIGHT = 137438953472L;
    public static final long AV_CH_BOTTOM_FRONT_CENTER = 274877906944L;
    public static final long AV_CH_BOTTOM_FRONT_LEFT = 549755813888L;
    public static final long AV_CH_BOTTOM_FRONT_RIGHT = 1099511627776L;
    public static final long AV_CH_SIDE_SURROUND_LEFT = 2199023255552L;
    public static final long AV_CH_SIDE_SURROUND_RIGHT = 4398046511104L;
    public static final long AV_CH_TOP_SURROUND_LEFT = 8796093022208L;
    public static final long AV_CH_TOP_SURROUND_RIGHT = 17592186044416L;
    public static final long AV_CH_BINAURAL_LEFT = 2305843009213693952L;
    public static final long AV_CH_BINAURAL_RIGHT = 4611686018427387904L;

    public static final long AV_CH_LAYOUT_MONO = 4L;
    public static final long AV_CH_LAYOUT_STEREO = 3L;
    public static final long AV_CH_LAYOUT_2POINT1 = 11L;
    public static final long AV_CH_LAYOUT_2_1 = 259L;
    public static final long AV_CH_LAYOUT_SURROUND = 7L;
    public static final long AV_CH_LAYOUT_3POINT1 = 15L;
    public static final long AV_CH_LAYOUT_4POINT0 = 263L;
    public static final long AV_CH_LAYOUT_4POINT1 = 271L;
    public static final long AV_CH_LAYOUT_2_2 = 1539L;
    public static final long AV_CH_LAYOUT_QUAD = 51L;
    public static final long AV_CH_LAYOUT_5POINT0 = 1543L;
    public static final long AV_CH_LAYOUT_5POINT1 = 1551L;
    public static final long AV_CH_LAYOUT_5POINT0_BACK = 55L;
    public static final long AV_CH_LAYOUT_5POINT1_BACK = 63L;
    public static final long AV_CH_LAYOUT_6POINT0 = 1799L;
    public static final long AV_CH_LAYOUT_6POINT0_FRONT = 1731L;
    public static final long AV_CH_LAYOUT_HEXAGONAL = 311L;
    public static final long AV_CH_LAYOUT_3POINT1POINT2 = 20495L;
    public static final long AV_CH_LAYOUT_6POINT1 = 1807L;
    public static final long AV_CH_LAYOUT_6POINT1_BACK = 319L;
    public static final long AV_CH_LAYOUT_6POINT1_FRONT = 1739L;
    public static final long AV_CH_LAYOUT_7POINT0 = 1591L;
    public static final long AV_CH_LAYOUT_7POINT0_FRONT = 1735L;
    public static final long AV_CH_LAYOUT_7POINT1 = 1599L;
    public static final long AV_CH_LAYOUT_7POINT1_WIDE = 1743L;
    public static final long AV_CH_LAYOUT_7POINT1_WIDE_BACK = 255L;
    public static final long AV_CH_LAYOUT_5POINT1POINT2 = 22031L;
    public static final long AV_CH_LAYOUT_5POINT1POINT2_BACK = 20543L;
    public static final long AV_CH_LAYOUT_OCTAGONAL = 1847L;
    public static final long AV_CH_LAYOUT_CUBE = 184371L;
    public static final long AV_CH_LAYOUT_5POINT1POINT4 = 185871L;
    public static final long AV_CH_LAYOUT_5POINT1POINT6 = 196111L;
    public static final long AV_CH_LAYOUT_7POINT1POINT2 = 22079L;
    public static final long AV_CH_LAYOUT_7POINT1POINT4 = 185919L;
    public static final long AV_CH_LAYOUT_7POINT1POINT6 = 196159L;
    public static final long AV_CH_LAYOUT_7POINT2POINT3 = 34359825983L;
    public static final long AV_CH_LAYOUT_9POINT1POINT4 = 186111L;
    public static final long AV_CH_LAYOUT_9POINT1POINT6 = 206158616319L;
    public static final long AV_CH_LAYOUT_HEXADECAGONAL = 6442710839L;
    public static final long AV_CH_LAYOUT_BINAURAL = 6917529027641081856L;
    public static final long AV_CH_LAYOUT_STEREO_DOWNMIX = 1610612736L;
    public static final long AV_CH_LAYOUT_22POINT2 = 2164663779327L;

    /** @deprecated aliases kept for ABI compatibility (FF_API_CHANNEL_LAYOUT_BACK). */
    @Deprecated
    public static final long AV_CH_LAYOUT_5POINT1POINT4_BACK = 185871L;
    @Deprecated
    public static final long AV_CH_LAYOUT_7POINT1POINT4_BACK = 185919L;
    @Deprecated
    public static final long AV_CH_LAYOUT_9POINT1POINT4_BACK = 186111L;
    @Deprecated
    public static final long AV_CH_LAYOUT_7POINT1_TOP_BACK = 20543L;
}
