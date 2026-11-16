package org.ffmpeg.consts;

/**
 * Bridge contract version and capability bits exposed by
 * {@code FFMpegNative.getBridgeCapabilities()}.
 *
 * <p>These describe the wrapper ABI, not FFmpeg. Gate every
 * bridge-version-dependent feature on the matching {@code CAP_*} bit rather
 * than comparing versions.
 */
public final class BridgeApi {
    private BridgeApi() {
    }

    /** Bridge contract major version implemented by this JAR. */
    public static final int BRIDGE_API_MAJOR = 2;

    /**
     * Bridge contract minor version implemented by this JAR.
     *
     * <p>1.2 fixes error paths in the PCM session and the window renderer and
     * adds {@link #CAP_PCM_SESSION_SAFE_V2} / {@link #CAP_RENDERER_SAFE_CLOSE}.
     * 1.3 appends the audio format snapshot entry points. 1.4 appends the
     * video diagnostics. 1.5 appends the libass message channel. 2.0 makes
     * five handle kinds opaque tokens and changes the close protocol.
     */
    public static final int BRIDGE_API_MINOR = 0;

    /** {@code major << 16 | minor}; 2.0 is {@code 0x00020000}. */
    public static final int BRIDGE_API_VERSION = (BRIDGE_API_MAJOR << 16) | BRIDGE_API_MINOR;

    /** {@code packetGetBudgetInfo} is implemented and returns the fixed 6-slot layout. */
    public static final long CAP_PACKET_BUDGET_INFO = 1L << 0;

    /** {@code packetGetSideDataSize} and {@code packetCopySideDataExact} are implemented. */
    public static final long CAP_SIDE_DATA_EXACT_COPY = 1L << 1;

    /** {@code getBridgeBuildInfo} returns a non-empty description. */
    public static final long CAP_BRIDGE_BUILD_INFO = 1L << 2;

    /** {@code frameNormalizeChannelLayout} and {@code channelLayoutNormalizeMask} are implemented. */
    public static final long CAP_FRAME_LAYOUT_NORMALIZE = 1L << 3;

    /** {@code pcmSessionCreate/Convert/Drain/Reset/Free} are implemented. */
    public static final long CAP_BOUNDED_PCM_RESAMPLE = 1L << 4;

    /** The PCM session obeys its 1.2 contract. */
    public static final long CAP_PCM_SESSION_SAFE_V2 = 1L << 5;

    /** The window renderer obeys its 1.2 close contract. */
    public static final long CAP_RENDERER_SAFE_CLOSE = 1L << 6;

    /**
     * Reserved for per-output-instance hardware buffer accounting. Not
     * implemented by this build and deliberately never returned.
     */
    public static final long CAP_CODEC_SCOPED_BUFFER_ACCOUNTING = 1L << 7;

    /** {@code streamGetAudioFormatInfo} is implemented (schema v1). */
    public static final long CAP_AUDIO_STREAM_FORMAT_INFO = 1L << 8;

    /** {@code frameGetAudioFormatInfo} is implemented (schema v1). */
    public static final long CAP_AUDIO_FRAME_FORMAT_INFO = 1L << 9;

    /** Reserved; this build never returns it. */
    public static final long CAP_AUDIO_PROBE_POLICY = 1L << 10;

    /** Reserved; this build never returns it. */
    public static final long CAP_AUDIO_PROBE_STATS = 1L << 11;

    /** {@code videoCodecComponentName} is implemented. */
    public static final long CAP_VIDEO_CODEC_COMPONENT_NAME = 1L << 12;

    /** {@code videoDecodeStats} is implemented (schema v1). */
    public static final long CAP_VIDEO_DECODE_STATS = 1L << 13;

    /** libass messages are forwarded to logcat under {@code MediaHub/Libass}. */
    public static final long CAP_LIBASS_FONT_LOG = 1L << 14;

    public static final long CAP_STREAM_SIDE_DATA = 1L << 15;
    public static final long CAP_FRAME_SIDE_DATA = 1L << 16;
    public static final long CAP_SIDE_DATA_TYPES = 1L << 17;
    public static final long CAP_VIDEO_METADATA_SNAPSHOT = 1L << 18;

    /** 2.0: an {@code AVFormatContext} handle is a controlled token. */
    public static final long CAP_FORMAT_HANDLE_GUARD = 1L << 19;

    /** 2.0: dictionary handles carry a real slot generation. */
    public static final long CAP_DICT_HANDLE_GENERATION = 1L << 20;

    /** 2.0: {@code dictIterate} returns an iteration cursor. */
    public static final long CAP_DICT_ITERATION_CURSOR = 1L << 21;

    /** 2.0: a format context holds its own reference to the interrupt flag. */
    public static final long CAP_INTERRUPT_LIFETIME = 1L << 22;

    /** 2.0: the window renderer releases its creator reference and recycles slots. */
    public static final long CAP_RENDERER_SLOT_RECYCLE = 1L << 23;
}
