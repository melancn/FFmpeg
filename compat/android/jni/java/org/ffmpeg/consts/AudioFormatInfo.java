package org.ffmpeg.consts;

/**
 * Audio format snapshot schema v1, shared by
 * {@code streamGetAudioFormatInfo} and {@code frameGetAudioFormatInfo}.
 */
public final class AudioFormatInfo {
    private AudioFormatInfo() {
    }

    /** Value of {@link #AUDIO_INFO_SCHEMA_VERSION} that this JAR understands. */
    public static final int AUDIO_FORMAT_SCHEMA_VERSION = 1;

    /** Elements required in {@code outInfo}. */
    public static final int AUDIO_FORMAT_INFO_SLOTS = 13;

    /**
     * Ceiling of this bridge's bounded policy -- not a statement about
     * FFmpeg. A layout with more channels is refused with {@code E2BIG}.
     */
    public static final int AUDIO_FORMAT_MAX_CHANNELS = 64;

    /** Placeholder written into a slot whose known bit is clear. */
    public static final long AUDIO_FORMAT_UNKNOWN_VALUE = 0L;

    /** Always 1 for this schema. */
    public static final int AUDIO_INFO_SCHEMA_VERSION = 0;
    /** Bit N set iff slot N is defined (see {@code AUDIO_KL_*}). */
    public static final int AUDIO_INFO_KNOWN_FIELDS = 1;
    /** {@code AVSampleFormat}; NONE/unknown is not filled in as S16. */
    public static final int AUDIO_INFO_SAMPLE_FORMAT = 2;
    /** Hz; never filled in as 48000. */
    public static final int AUDIO_INFO_SAMPLE_RATE = 3;
    /** Declared/frame channel count; never filled in as stereo. */
    public static final int AUDIO_INFO_CHANNEL_COUNT = 4;
    /** One of the {@code AUDIO_ORDER_*} codes, reported verbatim. */
    public static final int AUDIO_INFO_CHANNEL_LAYOUT_ORDER = 5;
    /** FFmpeg {@code AVChannelLayout} mask, not an Android channel mask. */
    public static final int AUDIO_INFO_NATIVE_CHANNEL_MASK = 6;
    /** {@code AVCodecParameters.codec_id}. Stream only. */
    public static final int AUDIO_INFO_CODEC_ID = 7;
    /** {@code AVCodecParameters.profile}. Stream only. */
    public static final int AUDIO_INFO_PROFILE = 8;
    /** {@code bits_per_coded_sample} as declared. Stream only. */
    public static final int AUDIO_INFO_BITS_PER_CODED_SAMPLE = 9;
    /** {@code bits_per_raw_sample} as declared. Stream only. */
    public static final int AUDIO_INFO_BITS_PER_RAW_SAMPLE = 10;
    /** {@code AVFrame.nb_samples}. Frame only. */
    public static final int AUDIO_INFO_SAMPLES_PER_CHANNEL = 11;
    /** Number of valid elements written to {@code outChannelIds}. */
    public static final int AUDIO_INFO_CHANNEL_IDS_COUNT = 12;

    /** Only a channel count is specified; no order, no ids. */
    public static final int AUDIO_ORDER_UNSPEC = 0;
    /** Channels in ascending {@code AVChannel} order; a mask is defined. */
    public static final int AUDIO_ORDER_NATIVE = 1;
    /** An explicit per-channel map; ids are in stream order, never sorted. */
    public static final int AUDIO_ORDER_CUSTOM = 2;
    /** ACN components, ordered by index. */
    public static final int AUDIO_ORDER_AMBISONIC = 3;

    public static final long AUDIO_KL_SCHEMA_VERSION = 1L << 0;
    public static final long AUDIO_KL_KNOWN_FIELDS = 1L << 1;
    public static final long AUDIO_KL_SAMPLE_FORMAT = 1L << 2;
    public static final long AUDIO_KL_SAMPLE_RATE = 1L << 3;
    public static final long AUDIO_KL_CHANNEL_COUNT = 1L << 4;
    public static final long AUDIO_KL_CHANNEL_LAYOUT_ORDER = 1L << 5;
    public static final long AUDIO_KL_NATIVE_CHANNEL_MASK = 1L << 6;
    public static final long AUDIO_KL_CODEC_ID = 1L << 7;
    public static final long AUDIO_KL_PROFILE = 1L << 8;
    public static final long AUDIO_KL_BITS_PER_CODED_SAMPLE = 1L << 9;
    public static final long AUDIO_KL_BITS_PER_RAW_SAMPLE = 1L << 10;
    public static final long AUDIO_KL_SAMPLES_PER_CHANNEL = 1L << 11;
    public static final long AUDIO_KL_CHANNEL_IDS_COUNT = 1L << 12;
}
