package org.ffmpeg;

/**
 * JNI bindings to a statically-linked FFmpeg built for Android.
 *
 * <p>The matching native implementation lives in
 * {@code compat/android/jni/ffmpeg_jni.c} and is compiled into
 * {@code libffmpeg_jni.so} for each Android ABI.
 *
 * <p><b>Handle model.</b> Opaque FFmpeg objects (AVFormatContext,
 * AVCodecContext, AVCodec, AVPacket, AVFrame, SwsContext, SwrContext and the
 * AVDictionary "box") are exchanged as {@code long} handles. A handle of
 * {@code 0} means null/invalid. The caller owns the lifetime of every handle
 * it allocates and MUST free it with the matching {@code *Free} method to
 * avoid leaking native memory.
 *
 * <p><b>Buffers.</b> Raw native pointers are never exposed to Java; decoded
 * data is copied into Java {@code byte[]} through the {@code *Copy} methods.
 * Size those arrays with the provided helpers
 * ({@link #bytesPerSample}, the frame/sws getters, or as {@code width*height*4}
 * when scaling to {@code AV_PIX_FMT_RGBA}).
 *
 * <p><b>Error codes.</b> Most methods return an FFmpeg error code (negative on
 * failure). Translate it with {@link #strerror}.
 */
public final class FFMpegNative {

    /**
     * Register a native log callback that forwards FFmpeg logs to Android Logcat.
     * Forwarding honours the level set by {@link #logSetLevel} (default
     * {@code AV_LOG_INFO}); raise it to {@link #AV_LOG_TRACE} to see the
     * per-buffer codec chatter.
     */
    public native void logSetCallback();

    /**
     * Attach a hardware frames context to a codec context, replacing any
     * previously-set {@code hw_frames_ctx} (the old reference is released).
     * Note: the mediacodec hardware decoders' {@code hw_config} does not
     * declare {@code HW_FRAMES_CTX}, so this setter is unrelated to the
     * Surface zero-copy path; use {@link #setContextHwDeviceCtx} for Surface.
     */
    public native void setContextHwFramesCtx(long codecCtx, long hwFramesCtx);

    /** Map a hardware frame to a destination frame (or vice‑versa). */
    public native int hwframeMap(long dstFrame, long srcFrame, int flags);

    /** Parse a channel layout string (e.g. "stereo", "5.1") into a layout value. */
    public native int channelLayoutFromString(String layoutStr);

    /** Manually link two filter contexts inside a filter graph. */
    public native int filterLink(long srcCtx, long dstCtx, int srcPadIdx, int dstPadIdx);

    /** Get a filter context from a graph by name. */
    public native long filterGraphGetFilter(long graph, String name);

    /** Convert a channel layout mask to a human‑readable description. */
    public native String channelLayoutDescribe(long mask);

    /** Convert a channel layout mask to a string using av_channel_layout_to_string. */
    public native String channelLayoutToString(long mask);



    static {
        System.loadLibrary("ffmpeg_jni");
    }

    public FFMpegNative() {
    }

    /* ================================================================== */
    /* Versions / info                                                     */
    /* ================================================================== */

    /** @return FFmpeg build version string (e.g. {@code "n7.1-..."}). */
    public native String getVersion();

    /** @return libavutil version, {@code "MAJOR.MINOR.MICRO"}. */
    public native String getAvutilVersion();

    /** @return libavcodec version, {@code "MAJOR.MINOR.MICRO"}. */
    public native String getAvcodecVersion();

    /** @return libavformat version, {@code "MAJOR.MINOR.MICRO"}. */
    public native String getAvformatVersion();

    /** @return libavfilter version, {@code "MAJOR.MINOR.MICRO"}. */
    public native String getAvfilterVersion();

    /** @return libswscale version, {@code "MAJOR.MINOR.MICRO"}. */
    public native String getSwscaleVersion();

    /** @return libswresample version, {@code "MAJOR.MINOR.MICRO"}. */
    public native String getSwresampleVersion();

    /** @return human-readable description of an FFmpeg error code. */
    public native String strerror(int errnum);

    /* ================================================================== */
    /* JNI bridge contract                                                 */
    /*                                                                     */
    /* These describe the *wrapper* ABI, not FFmpeg. {@link #getVersion}   */
    /* reports the FFmpeg build and says nothing about which native entry  */
    /* points this .so implements, so a JAR and a .so can disagree while   */
    /* both look healthy. Callers must gate every bridge-version-          */
    /* dependent feature on these three queries.                           */
    /* ================================================================== */

    /** Bridge contract major version implemented by this JAR. */
    public static final int BRIDGE_API_MAJOR = 1;

    /**
     * Bridge contract minor version implemented by this JAR.
     *
     * <p>1.2 changes no signature, parameter width, return type or AVERROR
     * convention. It fixes error paths in the PCM session and the window
     * renderer and adds {@link #CAP_PCM_SESSION_SAFE_V2} /
     * {@link #CAP_RENDERER_SAFE_CLOSE}. One safety narrowing is worth
     * repeating here: {@link #pcmSessionCreate} now refuses a planar output
     * sample format instead of writing it as if it were interleaved.
     *
     * <p>1.3 only appends the two read-only audio format snapshot entry
     * points and their schema. A 1.2 caller is unaffected; a 1.3 caller
     * should detect {@link #CAP_AUDIO_STREAM_FORMAT_INFO} /
     * {@link #CAP_AUDIO_FRAME_FORMAT_INFO} rather than comparing versions.
     *
     * <p>1.4 appends two read-only video diagnostics,
     * {@link #videoCodecComponentName} and {@link #videoDecodeStats}. A 1.3
     * caller is unaffected; a 1.4 caller must gate each feature on
     * {@link #CAP_VIDEO_CODEC_COMPONENT_NAME} /
     * {@link #CAP_VIDEO_DECODE_STATS} rather than comparing versions.
     *
     * <p>1.5 appends the libass message channel,
     * {@link #assSetLogLevel} / {@link #assGetLogLevel}. libass already
     * computes the font decision (requested family/bold/italic, selected
     * file, TTC face index, every missing-glyph fallback); the bridge now
     * forwards its messages to logcat under {@code MediaHub/Libass}. A 1.4
     * caller is unaffected; gate on {@link #CAP_LIBASS_FONT_LOG} because a
     * non-libass build does not contain the entry points at all.
     */
    public static final int BRIDGE_API_MINOR = 5;

    /** {@code major << 16 | minor}; first release is {@code 0x00010000}. */
    public static final int BRIDGE_API_VERSION = (BRIDGE_API_MAJOR << 16) | BRIDGE_API_MINOR;

    /** {@link #packetGetBudgetInfo} is implemented and returns the fixed 6-slot layout. */
    public static final long CAP_PACKET_BUDGET_INFO = 1L << 0;

    /** {@link #packetGetSideDataSize} and {@link #packetCopySideDataExact} are implemented. */
    public static final long CAP_SIDE_DATA_EXACT_COPY = 1L << 1;

    /** {@link #getBridgeBuildInfo} returns a non-empty description. */
    public static final long CAP_BRIDGE_BUILD_INFO = 1L << 2;

    /**
     * {@link #frameNormalizeChannelLayout} and {@link #channelLayoutNormalizeMask}
     * are implemented. Gate every layout-normalisation call on this bit: a
     * library built before them answers 0 here, and calling the entry point
     * anyway would throw {@link UnsatisfiedLinkError} instead of degrading.
     */
    public static final long CAP_FRAME_LAYOUT_NORMALIZE = 1L << 3;

    /**
     * {@code pcmSessionCreate/Convert/Drain/Reset/Free} are implemented, with
     * all limits enforced before any allocation or input consumption.
     *
     * <p>Gate every bounded-resampling call on this bit. A library built
     * before 1.1 answers 0 here; calling the session methods anyway throws
     * {@link UnsatisfiedLinkError} rather than degrading.
     *
     * @see #pcmSessionCreate
     */
    public static final long CAP_BOUNDED_PCM_RESAMPLE = 1L << 4;

    /**
     * The PCM session obeys its 1.2 contract: input planes are read through
     * the frame's full plane list, {@link #PCM_DRAINED} is only reported after
     * a completed flush (never derived from a non-zero internal delay), a
     * format change is refused until the old tail is drained and then commits
     * exactly once, and bytes are copied into {@code out} only after the
     * conversion has succeeded.
     *
     * <p>Check this bit before relying on any of that; a 1.1 library still
     * answers {@link #CAP_BOUNDED_PCM_RESAMPLE} but can answer
     * {@link #PCM_NEED_DRAIN} forever on an undersized block.
     */
    public static final long CAP_PCM_SESSION_SAFE_V2 = 1L << 5;

    /**
     * The window renderer obeys its 1.2 close contract: {@link
     * #windowRendererFree} retains the whole renderer (scaler, window and
     * locks) while an operation is admitted instead of freeing part of it,
     * handles are registry-checked and become unusable after Free, and
     * {@link #windowRendererFinish} reports {@link #OUTPUT_SESSION_DETACHED}
     * only after the platform window has been released exactly once.
     */
    public static final long CAP_RENDERER_SAFE_CLOSE = 1L << 6;

    /**
     * Reserved for per-output-instance hardware buffer accounting. Not
     * implemented by this build and deliberately never returned:
     * {@link #mediasurfaceGetOutstandingBuffers} is a process-wide counter
     * and is not evidence about a single output session.
     *
     * <p>Deferred by decision, not by omission. Closing it needs two things
     * this bridge cannot provide on its own: a way to read the decoder's
     * own hardware-frame count (private to libavcodec, with no exported
     * accessor) and sole ownership of the codec's lifetime (which
     * {@link #codecFreeContext} on a raw pointer cannot enforce). Until both
     * exist, the parallel hardware-decode restriction stands: do not run two
     * FFmpeg MediaCodec outputs at once, and do not read a zero outstanding
     * count as proof that every underlying reference is gone.
     */
    public static final long CAP_CODEC_SCOPED_BUFFER_ACCOUNTING = 1L << 7;

    /**
     * {@link #streamGetAudioFormatInfo} is implemented and fills the schema v1
     * layout described by {@code AUDIO_INFO_*}.
     */
    public static final long CAP_AUDIO_STREAM_FORMAT_INFO = 1L << 8;

    /**
     * {@link #frameGetAudioFormatInfo} is implemented and fills the same
     * schema v1 layout, with {@code AUDIO_SOURCE_FRAME} as the source kind.
     */
    public static final long CAP_AUDIO_FRAME_FORMAT_INFO = 1L << 9;

    /**
     * Reserved for a probe that refuses to open an audio decoder. The bit
     * position is allocated so it can never be reused for something else, but
     * this build never returns it: there is no entry point behind it yet.
     *
     * <p>Do not branch on it. A library that answers 0 must be treated as
     * having no such capability at all, never as "fall back to ordinary
     * probing".
     */
    public static final long CAP_AUDIO_PROBE_POLICY = 1L << 10;

    /**
     * Reserved for per-session probe statistics. Allocated, never returned by
     * this build, for the same reason as {@link #CAP_AUDIO_PROBE_POLICY}.
     */
    public static final long CAP_AUDIO_PROBE_STATS = 1L << 11;

    /**
     * {@link #videoCodecComponentName} is implemented and returns the
     * MediaCodec component the platform actually selected.
     *
     * <p>Gate every call on this bit: a library built before 1.4 has no such
     * entry point, and calling it anyway throws
     * {@link UnsatisfiedLinkError} instead of returning {@code null}.
     */
    public static final long CAP_VIDEO_CODEC_COMPONENT_NAME = 1L << 12;

    /**
     * {@link #videoDecodeStats} is implemented and fills the schema v1
     * counter layout described by {@code VIDEO_DECODE_STATS_*}.
     *
     * <p>Gate on this bit for the same reason as
     * {@link #CAP_VIDEO_CODEC_COMPONENT_NAME}. The counters describe what
     * this bridge observed; output the platform codec drops internally is not
     * visible and is not counted.
     */
    public static final long CAP_VIDEO_DECODE_STATS = 1L << 13;

    /**
     * libass messages are forwarded to logcat under the {@code MediaHub/Libass}
     * tag, and {@link #assSetLogLevel} / {@link #assGetLogLevel} control that
     * channel.
     *
     * <p>This is the only observable record of the font libass actually
     * chose: the requested family/bold/italic, the selected file, the TTC
     * face index, each missing-glyph fallback and the final "not found"
     * verdict. None of it is exposed by the libass public API, so Kotlin-side
     * logging cannot substitute.
     *
     * <p>Gate on this bit: a non-libass build has no such entry points, and
     * calling them anyway throws {@link UnsatisfiedLinkError}.
     */
    public static final long CAP_LIBASS_FONT_LOG = 1L << 14;

    /* ================================================================== */
    /* Audio format snapshot (schema v1)                                   */
    /*                                                                     */
    /* streamGetAudioFormatInfo() and frameGetAudioFormatInfo() fill the    */
    /* same 13-slot long[] so one decoder serves both. A slot either holds  */
    /* a value read verbatim from FFmpeg or has its knownFields bit clear.  */
    /* knownFields means "this slot is defined" -- never "the value is 0"   */
    /* and never "a decoder or sink can handle it".                         */
    /*                                                                     */
    /* Slots are long so a full 64-bit AVChannelLayout mask survives        */
    /* intact: the sign bit must not turn it into a negative or a 0.       */
    /* ================================================================== */

    /** Value of {@link #AUDIO_INFO_SCHEMA_VERSION} that this JAR understands. */
    public static final int AUDIO_FORMAT_SCHEMA_VERSION = 1;

    /** Elements required in {@code outInfo}. */
    public static final int AUDIO_FORMAT_INFO_SLOTS = 13;

    /**
     * Ceiling of this bridge's bounded policy -- not a statement about
     * FFmpeg. A layout with more channels is refused with {@code E2BIG},
     * never clamped to 64 or to stereo.
     */
    public static final int AUDIO_FORMAT_MAX_CHANNELS = 64;

    /**
     * Value written into a slot whose known bit is clear. It is a
     * placeholder, not a signal: 0 is a legal value for the mask, both bit
     * depths and the id count, so only the known bit may be used to decide
     * whether a slot is defined.
     */
    public static final long AUDIO_FORMAT_UNKNOWN_VALUE = 0L;

    /* Fixed indices into the snapshot array. */
    /** Always 1 for this schema. */
    public static final int AUDIO_INFO_SCHEMA_VERSION        = 0;
    /** Bit N set iff slot N is defined (see {@code AUDIO_KL_*}). */
    public static final int AUDIO_INFO_KNOWN_FIELDS          = 1;
    /** {@code AVSampleFormat}; NONE/unknown is not filled in as S16. */
    public static final int AUDIO_INFO_SAMPLE_FORMAT         = 2;
    /** Hz; never filled in as 48000. */
    public static final int AUDIO_INFO_SAMPLE_RATE           = 3;
    /** Declared/frame channel count; never filled in as stereo. */
    public static final int AUDIO_INFO_CHANNEL_COUNT         = 4;
    /**
     * One of the {@code AUDIO_ORDER_*} codes, reported verbatim. UNSPEC is
     * a defined answer ("layout type not specified") -- not stereo, and not
     * an unknown.
     */
    public static final int AUDIO_INFO_CHANNEL_LAYOUT_ORDER  = 5;
    /**
     * FFmpeg {@code AVChannelLayout} mask, NOT an Android channel mask.
     * Defined only for a NATIVE order (or AMBISONIC with a non-diegetic
     * mask); for UNSPEC and CUSTOM the union holds something else, so the
     * known bit is clear. Never map this to {@code AudioFormat} directly.
     */
    public static final int AUDIO_INFO_NATIVE_CHANNEL_MASK   = 6;
    /** {@code AVCodecParameters.codec_id}. Stream only. */
    public static final int AUDIO_INFO_CODEC_ID              = 7;
    /** {@code AVCodecParameters.profile}; unknown stays unknown. Stream only. */
    public static final int AUDIO_INFO_PROFILE               = 8;
    /**
     * {@code bits_per_coded_sample} as declared. Not a generic PCM storage
     * width. Stream only.
     */
    public static final int AUDIO_INFO_BITS_PER_CODED_SAMPLE = 9;
    /**
     * {@code bits_per_raw_sample} as declared: source metadata evidence,
     * not proof that the decoder's output keeps that precision. Stream only.
     * S32 with 24 raw bits is a real combination.
     */
    public static final int AUDIO_INFO_BITS_PER_RAW_SAMPLE   = 10;
    /**
     * {@code AVFrame.nb_samples}. Frame only. A 0-sample frame is a defined
     * answer, distinguished from unknown by its known bit.
     */
    public static final int AUDIO_INFO_SAMPLES_PER_CHANNEL   = 11;
    /**
     * Number of valid elements written to {@code outChannelIds}; elements
     * past this count are not part of the snapshot. 0 for UNSPEC.
     */
    public static final int AUDIO_INFO_CHANNEL_IDS_COUNT     = 12;

    /** Only a channel count is specified; no order, no ids. */
    public static final int AUDIO_ORDER_UNSPEC    = 0;
    /** Channels in ascending {@code AVChannel} order; a mask is defined. */
    public static final int AUDIO_ORDER_NATIVE    = 1;
    /** An explicit per-channel map; ids are in stream order, never sorted. */
    public static final int AUDIO_ORDER_CUSTOM    = 2;
    /** ACN components, ordered by index. */
    public static final int AUDIO_ORDER_AMBISONIC = 3;

    /* knownFields bits, one per slot. */
    public static final long AUDIO_KL_SCHEMA_VERSION        = 1L << 0;
    public static final long AUDIO_KL_KNOWN_FIELDS          = 1L << 1;
    public static final long AUDIO_KL_SAMPLE_FORMAT         = 1L << 2;
    public static final long AUDIO_KL_SAMPLE_RATE           = 1L << 3;
    public static final long AUDIO_KL_CHANNEL_COUNT         = 1L << 4;
    public static final long AUDIO_KL_CHANNEL_LAYOUT_ORDER  = 1L << 5;
    public static final long AUDIO_KL_NATIVE_CHANNEL_MASK   = 1L << 6;
    public static final long AUDIO_KL_CODEC_ID              = 1L << 7;
    public static final long AUDIO_KL_PROFILE               = 1L << 8;
    public static final long AUDIO_KL_BITS_PER_CODED_SAMPLE = 1L << 9;
    public static final long AUDIO_KL_BITS_PER_RAW_SAMPLE   = 1L << 10;
    public static final long AUDIO_KL_SAMPLES_PER_CHANNEL   = 1L << 11;
    public static final long AUDIO_KL_CHANNEL_IDS_COUNT     = 1L << 12;

    /**
     * Read the complete audio format of a stream's declared parameters.
     *
     * <p>Reads {@code codecpar} only: no decoder is opened, no packet is
     * read, nothing is resampled and no layout normaliser is called. An
     * UNSPEC stream therefore reports UNSPEC, not a plausible-looking stereo
     * default.
     *
     * <p>Gate on {@link #CAP_AUDIO_STREAM_FORMAT_INFO}. A library that
     * answers 0 there has no such capability: the feature must be refused,
     * never re-implemented from {@link #streamGetParChannelLayout} (whose 0
     * for non-NATIVE layouts cannot be told apart from "no layout").
     *
     * @param formatCtx   format context handle
     * @param streamIndex audio stream to describe
     * @param outInfo     at least {@link #AUDIO_FORMAT_INFO_SLOTS} elements;
     *                    unknown slots are written as
     *                    {@link #AUDIO_FORMAT_UNKNOWN_VALUE}
     * @param outChannelIds at least {@link #AUDIO_FORMAT_MAX_CHANNELS}
     *                    elements; the first
     *                    {@link #AUDIO_INFO_CHANNEL_IDS_COUNT} are part of
     *                    the snapshot
     * @return 0 on success, or a negative {@code AVERROR}:
     *         {@code EINVAL} (null handle, bad index, non-audio stream, short
     *         {@code outInfo}), {@code ENOSPC} (short {@code outChannelIds}),
     *         {@code E2BIG} (channel count above the bridge ceiling). On
     *         failure neither array is written.
     */
    public native int streamGetAudioFormatInfo(long formatCtx, int streamIndex,
                                               long[] outInfo, int[] outChannelIds);

    /**
     * Read the audio format of a decoded frame.
     *
     * <p>Reports what was actually decoded, which may differ from what the
     * stream declared. {@code CODEC_ID}, {@code PROFILE} and both bit-depth
     * slots are always Unknown here: {@code AVFrame} does not carry them,
     * and copying the stream's values would present a belief as an
     * observation. An S32 frame is not read as "32 valid bits" either.
     * Associate a frame with its source stream using the app's own config
     * epoch.
     *
     * <p>The frame is not modified. Gate on
     * {@link #CAP_AUDIO_FRAME_FORMAT_INFO}.
     *
     * @param frame     decoded frame handle
     * @param outInfo   at least {@link #AUDIO_FORMAT_INFO_SLOTS} elements
     * @param outChannelIds at least {@link #AUDIO_FORMAT_MAX_CHANNELS}
     * @return 0 on success, or a negative {@code AVERROR} as for
     *         {@link #streamGetAudioFormatInfo}; nothing is written on
     *         failure
     */
    public native int frameGetAudioFormatInfo(long frame,
                                              long[] outInfo, int[] outChannelIds);

    /**
     * Number of elements {@link #packetGetBudgetInfo} requires in its output
     * array. Part of the contract: the layout below is frozen for major 1.
     */
    public static final int PACKET_BUDGET_INFO_SLOTS = 6;

    /* Fixed indices into the packetGetBudgetInfo() output array. */
    /** {@code AVPacket.size}. */
    public static final int PACKET_BUDGET_PAYLOAD_BYTES         = 0;
    /** Capacity of the referenced payload buffer (conservative estimate if none). */
    public static final int PACKET_BUDGET_PAYLOAD_CAPACITY     = 1;
    /** Sum of all side data content lengths, excluding padding. */
    public static final int PACKET_BUDGET_SIDE_DATA_BYTES      = 2;
    /** Number of side data entries, of any type. */
    public static final int PACKET_BUDGET_SIDE_DATA_COUNT      = 3;
    /** Capacity of the buffer referenced by {@code opaque_ref}. */
    public static final int PACKET_BUDGET_OPAQUE_REF_CAPACITY  = 4;
    /** Conservative accounting charge for this packet (see method docs). */
    public static final int PACKET_BUDGET_CHARGE_BYTES         = 5;

    /**
     * @return the bridge contract version implemented by the loaded library,
     *         {@code major << 16 | minor}, or 0 if the library predates this
     *         query. Compare with {@link #BRIDGE_API_VERSION}.
     */
    public native int getBridgeApiVersion();

    /**
     * @return a bitmask of {@code CAP_*} flags this library actually implements
     *         and has been validated for. Unknown bits must be ignored.
     */
    public native long getBridgeCapabilities();

    /**
     * @return a single-line JSON object identifying the built library
     *         ({@code bridgeApi}, {@code packetBudgetPolicyVersion},
     *         {@code ffmpegVersion}, {@code abi}, {@code buildType}), or an
     *         empty string if unavailable. Contains no URLs, paths or user data.
     */
    public native String getBridgeBuildInfo();

    /**
     * Take a consistent snapshot of a packet's memory accounting.
     *
     * <p>This is a <em>budget charge</em>: a deliberately conservative number
     * suitable for admission control. It is not the exact {@code malloc} size
     * and not process RSS. Shared buffers are charged once per holding packet,
     * so the total may over-count but never under-counts. It also does not
     * cover demuxer-internal buffering, decoders, swresample or libass.
     *
     * <p>Returns all values in one call so the caller never reads fields from
     * two different states.
     *
     * @param pkt   packet handle
     * @param out   at least {@link #PACKET_BUDGET_INFO_SLOTS} elements; written
     *              only on success. Indices are the
     *              {@code PACKET_BUDGET_*} constants.
     * @return 0 on success, negative {@code AVERROR} otherwise. On failure the
     *         contents of {@code out} are unspecified and must not be used.
     */
    public native int packetGetBudgetInfo(long pkt, long[] out);

    /**
     * Exact byte length of a side data entry.
     *
     * <p>Distinguishes the two cases {@link #packetGetSideData} cannot:
     * a missing entry returns a negative {@code AVERROR} ({@code ENOENT}),
     * while an entry that exists but is empty returns 0.
     *
     * @return the entry length (>= 0), or negative {@code AVERROR}
     */
    public native int packetGetSideDataSize(long pkt, int type);

    /**
     * Copy a side data entry in full, without silent truncation.
     *
     * @param pkt       packet handle
     * @param type      {@code AV_PKT_DATA_*}
     * @param out       destination array
     * @param offset    first index to write
     * @param capacity  number of bytes available from {@code offset}
     * @return the full entry length on success ({@code 0} for an empty entry);
     *         {@code AVERROR(ENOENT)} if the entry does not exist,
     *         {@code AVERROR(ENOSPC)} if it does not fit, in which case
     *         {@code out} is left untouched; {@code AVERROR(EINVAL)} for bad
     *         arguments or out-of-range {@code offset}/{@code capacity}
     */
    public native int packetCopySideDataExact(long pkt, int type,
                                              byte[] out, int offset, int capacity);

    /* ================================================================== */
    /* AVDictionary (key/value options)                                    */
    /* ================================================================== */

    /**
     * Allocate an empty options dictionary box. The returned handle backs a
     * pointer-to-pointer and must be freed with {@link #dictFree}.
     *
     * @return a dictionary-box handle (0 on failure)
     */
    public native long dictCreate();

    /**
     * Set/replace/add an entry.
     *
     * @param box   dictionary-box handle from {@link #dictCreate}
     * @param key   entry key (UTF-8)
     * @param val   entry value (UTF-8), or {@code null} to delete the key
     * @param flags bitmask of {@code AV_DICT_*} constants (e.g. {@code AV_DICT_DONT_STRDUP})
     * @return 0 on success, negative AVERROR on failure
     */
    public native int dictSet(long box, String key, String val, int flags);

    /** @return the value for {@code key}, or {@code null} if not present. */
    public native String dictGet(long box, String key, int flags);

    /** @return number of entries in the dictionary. */
    public native int dictCount(long box);

    /** Free the dictionary box and its contents. */
    public native void dictFree(long box);

    /** Parse a "key=val,key2=val2" string into the dictionary. */
    public native int dictParseString(long box, String str, String keyValSep, String pairsSep, int flags);

    /** Serialize the dictionary to a {@code "key=val,key2=val2"} string. */
    public native String dictGetString(long box, String keyValSep, String pairsSep);

    /**
     * Iterate dictionary entries. Pass 0 for the first call to retrieve the
     * first entry handle; pass that handle to retrieve the next, and so on.
     * @return the next entry handle, or 0 at end-of-dictionary.
     */
    public native long dictIterate(long box, long prevHandle);

    /** @return the key of a dictionary entry handle (from {@link #dictIterate}). */
    public native String dictEntryGetKey(long entryHandle);

    /** @return the value of a dictionary entry handle (from {@link #dictIterate}). */
    public native String dictEntryGetValue(long entryHandle);

    /* ================================================================== */
    /* libavformat (demuxing)                                              */
    /* ================================================================== */

    /**
     * Open a media input and read its header. Optionally pass a
     * {@link #dictCreate} box as format-level options (may be 0).
     *
     * @param url          path or URI
     * @param optionsBox   dictionary-box handle, or 0
     * @return AVFormatContext handle, or 0 on failure
     */
    public native long formatOpenInput(String url, long optionsBox);

    /**
     * Cancellable variant of {@link #formatOpenInput}: the context is
     * allocated first, {@code flag} is attached as its
     * {@code interrupt_callback}, and only then is it opened. Raising the
     * flag with {@link #interruptFlagAbort} therefore aborts the in-flight
     * open (DNS, TLS handshake, HLS playlist reload) with
     * {@code AVERROR_EXIT} instead of waiting for the protocol timeout.
     *
     * @param url          path or URI
     * @param optionsBox   dictionary-box handle, or 0
     * @param flag         interrupt flag from {@link #interruptFlagCreate};
     *                     0 opens without cancellation
     * @return AVFormatContext handle, or 0 on failure (same contract as
     *         {@link #formatOpenInput})
     */
    public native long formatOpenInputWithInterrupt(String url, long optionsBox, long flag);

    /**
     * Probe the streams. Call once before accessing stream metadata.
     * @return 0 on success, negative AVERROR on failure
     */
    public native int formatFindStreamInfo(long ctx);

    /** Close and free the format context (handle becomes invalid). */
    public native void formatCloseInput(long ctx);

    /* -------------------------------------------------------------- */
    /* Network / format probing / manual context                      */
    /* -------------------------------------------------------------- */

    /** Initialise global network state; call before opening http/rtmp/etc. */
    public native int formatNetworkInit();

    /** Tear down global network state (pair with {@link #formatNetworkInit}). */
    public native int formatNetworkDeinit();

    /** Manually allocate an (uninitialized) format context. */
    public native long formatAllocContext();

    /** Find an input demuxer format by short name (e.g. "matroska"). */
    public native long findInputFormat(String name);

    /** Find an output muxer format by short name/filename/mime (use with alloc_output_context2). */
    public native long guessFormat(String shortName, String filename, String mime);

    /** Query whether a muxer supports a given codec (codecId). */
    public native int formatQueryCodec(long ctx, int codecId, int stdCompliance);

    /**
     * Read the next packet from the container into {@code pkt}.
     * @return 0 on success, AVERROR_EOF at end-of-file, or negative error
     */
    public native int readFrame(long ctx, long pkt);

    /**
     * Seek to a timestamp.
     * @param timestamp in {@link #streamGetTimeBaseDen}/{@code Num} units, or AV_TIME_BASE for -1
     * @param flags {@code AVSEEK_FLAG_*}
     */
    public native int seekFrame(long ctx, int streamIndex, long timestamp, int flags);

    /**
     * Seek within a timestamp range (more flexible than {@link #seekFrame}).
     * @param minTs minimum acceptable timestamp
     * @param ts     target timestamp
     * @param maxTs maximum acceptable timestamp
     * @param flags {@code AVSEEK_FLAG_*}
     * @return 0 on success, negative AVERROR
     */
    public native int formatSeekFile(long ctx, int streamIndex,
                                     long minTs, long ts, long maxTs, int flags);

    /** Flush the format context (resets internal demux buffers). */
    public native int formatFlush(long ctx);

    /** @return "num/den" of {@code AV_TIME_BASE_Q} (typically "1/1000000"). */
    public native String getFormatTimeBaseQ();

    /** @return input demuxer format short name (e.g. "mov,mp4,m4a..."), or null. */
    public native String formatGetInputFormatName(long ctx);

    /** @return output muxer format short name, or null. */
    public native String formatGetOutputFormatName(long ctx);

    /** Print container/stream info to the log (debugging aid). */
    public native void dumpFormat(long ctx, int index, String url, int isOutput);

    /** Free a format context created for muxing (after writeTrailer). */
    public native void formatFreeContext(long ctx);

    /** @return numerator of stream {@code index}'s time base. */
    public native long streamGetTimeBaseNum(long ctx, int index);

    /** @return denominator of stream {@code index}'s time base. */
    public native long streamGetTimeBaseDen(long ctx, int index);

    /* ================================================================== */
    /* libavformat: muxing / output                                        */
    /* ================================================================== */

    /**
     * Allocate an output format context from a format short name and/or filename.
     * @return AVFormatContext handle, 0 on failure
     */
    public native long formatAllocOutputContext2(String formatName, String filename);

    /**
     * Add a new stream to the output context.
     * @return the new stream index, or negative AVERROR
     */
    public native int formatNewStream(long ctx, long codec);

    /**
     * Open an AVIO resource (file/socket) for writing/reading.
     * @param flags AVIO_FLAG_READ / AVIO_FLAG_WRITE
     */
    public native int ioOpen(long ctx, String url, int flags);

    /** Close the AVIO resource bound to the context. */
    public native int ioClose(long ctx);

    /** Write the container header after streams/codecs are configured. */
    public native int formatWriteHeader(long ctx, long optionsBox);

    /** Write a packet without interleaving. */
    public native int writeFrame(long ctx, long pkt);

    /** Write a packet with interleaving (preferred for muxing). */
    public native int interleavedWriteFrame(long ctx, long pkt);

    /** Write the container trailer (finalize). */
    public native int writeTrailer(long ctx);


    /** @return number of streams reported by the container. */
    public native int getNbStreams(long ctx);

    /** @return container duration in microseconds, or negative if unknown. */
    public native long getDuration(long ctx);

    /**
     * Find the best stream of a given type.
     *
     * @param type    {@code AVMEDIA_TYPE_*} (e.g. {@code AVMEDIA_TYPE_VIDEO})
     * @param want    requested stream index, or -1 for any
     * @param related index of a related stream (e.g. matching audio), or -1
     * @return best stream index, or negative AVERROR
     */
    public native int findBestStream(long ctx, int type, int want, int related);

    /** @return start time (in stream timebase units) of stream {@code index}. */
    public native long streamGetStartTime(long ctx, int index);

    /** @return codec id ({@code AV_CODEC_ID_*}) of stream {@code index}. */
    public native int streamGetCodecId(long ctx, int index);

    /** @return codec type ({@code AVMEDIA_TYPE_*}) of stream {@code index}. */
    public native int streamGetCodecType(long ctx, int index);

    /** @return coded width (pixels) of stream {@code index}, 0 if N/A. */
    public native int streamGetWidth(long ctx, int index);

    /** @return coded height (pixels) of stream {@code index}, 0 if N/A. */
    public native int streamGetHeight(long ctx, int index);

    /** @return pixel/sample format of stream {@code index}, -1 if unknown. */
    public native int streamGetFormat(long ctx, int index);

    /** @return numerator of stream {@code index}'s average frame rate. */
    public native int streamGetAvgFrameRateNum(long ctx, int index);
    /** @return denominator of stream {@code index}'s average frame rate. */
    public native int streamGetAvgFrameRateDen(long ctx, int index);
    /** @return numerator of stream {@code index}'s base frame rate (r_frame_rate). */
    public native int streamGetRFrameRateNum(long ctx, int index);
    /** @return denominator of stream {@code index}'s base frame rate. */
    public native int streamGetRFrameRateDen(long ctx, int index);
    /** @return stream {@code index}'s bitrate (bits/sec) from codecpar, 0 if unknown. */
    public native int streamGetParBitRate(long ctx, int index);
    /** @return number of frames in stream {@code index}, 0 if unknown. */
    public native int streamGetParNbFrames(long ctx, int index);

    /** @return stream {@code index} duration (in time base units), or {@link #AV_NOPTS_VALUE}. */
    public native long formatGetStreamDuration(long ctx, int index);
    /** @return stream {@code index} {@code AV_DISPOSITION_*} flags. */
    public native int streamGetDisposition(long ctx, int index);
    /** Set stream {@code index} disposition flags. */
    public native void streamSetDisposition(long ctx, int index, int disposition);
    /**
     * Disable codec-level parsing for a single stream: closes any parser
     * that {@link #formatFindStreamInfo} may have created and prevents
     * {@link #readFrame} from creating a new one. Use this when the demuxer
     * misidentified the stream's codec (e.g. AV3A audio tagged as MP3 in
     * MPEG-TS), so that raw demuxed payload is returned without
     * parser-induced splitting.
     *
     * <p>Must be called after {@link #formatFindStreamInfo} and before the
     * first {@link #readFrame}.
     */
    public native void streamDisableParsing(long ctx, int index);
    /** @return fourcc codec tag of stream {@code index}. */
    public native int streamGetParCodecTag(long ctx, int index);
    /** @return audio sample rate (Hz) of stream {@code index}. */
    public native int streamGetParSampleRate(long ctx, int index);
    /** @return channel count of stream {@code index}. */
    public native int streamGetParChannels(long ctx, int index);
    /**
     * Copy up to {@code len} bytes of stream {@code index}'s codecpar
     * extradata (SPS/PPS/VPS for H.264/HEVC, etc.) into {@code out}.
     * @return bytes copied, or -1 if no extradata / bad args.
     */
    public native int streamGetExtradata(long ctx, int index, byte[] out, int off, int len);

    /**
     * @return byte length of stream {@code index}'s codecpar extradata, 0 when
     *         absent. Size your buffer with this before calling
     *         {@link #streamGetExtradata}; for MKV
     *         {@code AVMEDIA_TYPE_ATTACHMENT} streams this is the font size.
     */
    public native int streamGetParExtradataSize(long ctx, int index);


    /* ================================================================== */
    /* libavcodec (decoding / encoding)                                    */
    /* ================================================================== */

    /** Find a decoder by id. @return AVCodec handle, or 0 if not found. */
    public native long findDecoder(int codecId);

    /** Find an encoder by id. @return AVCodec handle, or 0 if not found. */
    public native long findEncoder(int codecId);

    /** Find a decoder by short name (e.g. "h264"). @return AVCodec handle, or 0. */
    public native long findDecoderByName(String name);

    /** Find an encoder by short name (e.g. "libx264"). @return AVCodec handle, or 0. */
    public native long findEncoderByName(String name);

    /**
     * @return the long descriptive name of an AVCodec handle (from
     * {@link #findDecoder}/{@link #findDecoderByName}/etc.), or {@code null}.
     */
    public native String codecGetLongName(long codec);

    /** @return media type ({@code AVMEDIA_TYPE_*}) of an AVCodec handle. */
    public native int codecGetType(long codec);

    /** @return the short codec name for an AVCodec handle. */
    public native String codecGetPtrName(long codec);

    /**
     * @return duration of an audio frame (in codec time base units) given the
     * codec context and the frame payload size in bytes.
     */
    public native int getAudioFrameDuration(long codecCtx, int frameBytes);

    /** Variant of {@link #getAudioFrameDuration} based on stream codecpar. */
    public native int getAudioFrameDuration2(long fmtCtx, int streamIndex, int frameBytes);

    /* -------------------------------------------------------------- */
    /* Hardware acceleration (basic apparatus)                        */
    /* -------------------------------------------------------------- */

    /**
     * Create a hardware device context (also initialises global network state
     * for some types). The returned AVBufferRef must be freed with
     * {@link #bufferRefFree}.
     * @param hwType enum value from {@link #hwDeviceFindTypeByName}
     * @param device device string, or {@code null} for default
     * @param opts device-creation options, typically 0
     * @return AVBufferRef handle, 0 on failure
     */
    public native long hwdeviceCreate(int hwType, String device, int opts);

    /** Free an AVBufferRef (device or frames context reference). */
    public native void bufferRefFree(long ref);

    /** Allocate a hardware frames context from a device reference. */
    public native long hwframeCtxAlloc(long deviceRef);

    /** Initialise a hardware frames context (configure format/width/height). */
    public native int hwframeCtxInit(long framesRef);

    /** Transfer a hardware frame to/from system memory between {@code dst}/{@code src}. */
    public native int hwframeTransferData(long dstFrame, long srcFrame, int flags);

    /** @return a new AVBufferRef to a frame's {@code hw_frames_ctx} (free with {@link #bufferRefFree}). */
    public native long hwframeGetHwFramesCtx(long frame);

    /** @return a hardware frames context for a given codec context + device (0 on failure). */
    public native long codecGetHwFramesParameters(long codecCtx, long deviceRef);

    /** Find an AVHWDeviceType enum value from its name (e.g. "mediacodec"). */
    public native int hwDeviceFindTypeByName(String name);

    /** @return the canonical name for an AVHWDeviceType enum value, or null. */
    public native String hwDeviceGetTypeName(int hwType);

    /** Iterate hardware device types; pass 0 to start, returns 0 at end. */
    public native int hwDeviceIterateTypes(int prev);


    /** @return short human-readable codec name (e.g. {@code "h264"}). */
    public native String codecGetName(int codecId);

    /**
     * Allocate a codec context.
     * @param codec AVCodec handle from {@link #findDecoder}/{@link #findEncoder}, or 0
     * @return AVCodecContext handle, 0 on failure
     */
    public native long allocCodecContext(long codec);

    /**
     * Copy stream parameters (codec id, width/height/format, ...) into a
     * freshly allocated codec context, before opening it. Also sets
     * {@code pkt_timebase} from the stream time base, which decoders need to
     * rescale packet timestamps correctly (the mediacodec decoders feed the
     * codec in {@code AV_TIME_BASE} units).
     */
    public native int codecParametersToContext(long codecCtx, long fmtCtx, int index);

    /**
     * Open the codec context.
     * @return 0 on success, negative AVERROR on failure
     */
    public native int codecOpen2(long codecCtx, long codec);

    /** Free the codec context (handle becomes invalid). */
    public native void codecFreeContext(long codecCtx);

    /** @return coded width (pixels) configured on the context. */
    public native int getContextWidth(long codecCtx);

    /** @return coded height (pixels) configured on the context. */
    public native int getContextHeight(long codecCtx);

    /** @return pixel format configured on the context, -1 if N/A. */
    public native int getContextPixFmt(long codecCtx);

    /** @return codec id configured on the context. */
    public native int getContextCodecId(long codecCtx);

    /* -------------------------------------------------------------- */
    /* Video decoder diagnostics (1.4)                                 */
    /* -------------------------------------------------------------- */

    /**
     * Return the MediaCodec component the hardware decoder actually selected,
     * for example {@code "c2.qti.avc.decoder"} or
     * {@code "OMX.NVIDIA.h264.decode"}.
     *
     * <p>FFmpeg chooses this component internally when a
     * {@code *_mediacodec} decoder is opened. {@link #codecGetName} reports
     * only the FFmpeg codec name (for example {@code "h264"}) and
     * {@link #findDecoderByName} the requested decoder name (for example
     * {@code "h264_mediacodec"}); neither is the platform component. This
     * method is the only source of the actual component.
     *
     * <p>Returns {@code null} for a software decoder, an AVS2/platform
     * decoder that is not backed by this FFmpeg codec context, a context that
     * has not been opened, and when the platform name could not be read. A
     * MIME type and the requested FFmpeg decoder name are never returned.
     * {@code null} means "not known", never "software decode is in use".
     *
     * <p>Gate on {@link #CAP_VIDEO_CODEC_COMPONENT_NAME}. The read does not
     * modify decoder state and may run on any thread, but the context must
     * still be valid: do not call this after {@link #codecFreeContext}, and
     * do not free the context concurrently. Every call returns a new String.
     *
     * @param codecCtx an opened codec context handle
     * @return the actual MediaCodec component name, or {@code null}
     */
    public native String videoCodecComponentName(long codecCtx);

    /** Value of the schema-version slot in a {@link #videoDecodeStats} snapshot. */
    public static final int VIDEO_DECODE_STATS_SCHEMA_VERSION = 1;

    /** Elements required in {@code out}. */
    public static final int VIDEO_DECODE_STATS_SLOTS = 6;

    /* Fixed indices into the videoDecodeStats() output array. */
    /** Always {@link #VIDEO_DECODE_STATS_SCHEMA_VERSION} for this schema. */
    public static final int VIDEO_DECODE_STATS_SCHEMA_VERSION_INDEX = 0;
    /** Frames returned by {@link #codecReceiveFrame} with status 0. */
    public static final int VIDEO_DECODE_STATS_DECODED_FRAMES      = 1;
    /** Successful Surface submissions through this bridge. */
    public static final int VIDEO_DECODE_STATS_RENDERED_FRAMES     = 2;
    /** Frames discarded without being shown, as observed by this bridge. */
    public static final int VIDEO_DECODE_STATS_DROPPED_FRAMES      = 3;
    /** Hard decode failures; EAGAIN and EOF are not counted. */
    public static final int VIDEO_DECODE_STATS_DECODE_ERRORS       = 4;
    /** {@code AVCodecContext.bit_rate} in bit/s; {@code <= 0} is unknown. */
    public static final int VIDEO_DECODE_STATS_CURRENT_BITRATE     = 5;

    /**
     * Read a per-context decoder counter snapshot (schema v1).
     *
     * <p>Schema v1 fills {@link #VIDEO_DECODE_STATS_SLOTS} slots, indexed by
     * the {@code VIDEO_DECODE_STATS_*} constants:
     * <pre>
     * 0  schemaVersion   always 1
     * 1  decodedFrames   frames returned by codecReceiveFrame
     * 2  renderedFrames  successful Surface submissions through this bridge
     * 3  droppedFrames   frames observed as discarded without being shown
     * 4  decodeErrors    hard decode failures
     * 5  currentBitRate  AVCodecContext.bit_rate, bit/s; &lt;= 0 unknown
     * </pre>
     *
     * <p>Counters are monotonic and are not reset by flush or seek. They are
     * maintained at this bridge's entry points:
     * {@link #codecReceiveFrame} for decoded frames and hard errors, and
     * {@link #mediasurfaceRenderBufferAtTime} /
     * {@link #mediasurfaceReleaseBuffer} for rendered and dropped frames.
     * Output the platform MediaCodec drops inside itself is not observable
     * from FFmpeg and is not counted, so a zero dropped count does not prove
     * that the platform dropped nothing.
     *
     * <p>{@link #VIDEO_DECODE_STATS_CURRENT_BITRATE} is the codec context's
     * declared bit rate; it is not derived from payload size or elapsed time
     * and is {@code <= 0} when the stream did not declare one.
     *
     * <p>Only a hardware MediaCodec video decoder context created by
     * {@link #allocCodecContext} is accepted. Software decoders, audio
     * contexts and encoders answer {@code AVERROR(EINVAL)}; a context with no
     * counter entry answers {@code AVERROR(ENOENT)}. Nothing is written on
     * failure. The context must still be valid: do not call this after
     * {@link #codecFreeContext}, and do not free the context concurrently.
     *
     * <p>Gate on {@link #CAP_VIDEO_DECODE_STATS}.
     *
     * @param codecCtx codec context handle
     * @param out      destination array, at least
     *                 {@link #VIDEO_DECODE_STATS_SLOTS} elements
     * @param len      number of slots the caller offers; must be at least
     *                 {@link #VIDEO_DECODE_STATS_SLOTS}
     * @return 0 on success, or a negative AVERROR as described above
     */
    public native int videoDecodeStats(long codecCtx, long[] out, int len);

    /**
     * Send a packet to the decoder (drain by passing {@code pkt==0}).
     * @return 0 on success or EAGAIN/EOF (negative AVERROR)
     */
    public native int codecSendPacket(long codecCtx, long pkt);

    /**
     * Receive a decoded frame.
     * @return 0 on success or EAGAIN/EOF (negative AVERROR)
     */
    public native int codecReceiveFrame(long codecCtx, long frame);

    /**
     * Send a frame to the encoder (flush by passing {@code frame==0}).
     * @return 0 on success or EAGAIN/EOF (negative AVERROR)
     */
    public native int codecSendFrame(long codecCtx, long frame);

    /**
     * Receive an encoded packet.
     * @return 0 on success or EAGAIN/EOF (negative AVERROR)
     */
    public native int codecReceivePacket(long codecCtx, long pkt);

    /** Flush the codec decoder/encoder buffers (resets internal state). */
    public native void codecFlushBuffers(long codecCtx);

    /** Allocate a codec parameters object. @return AVCodecParameters handle, 0 on failure. */
    public native long codecparAlloc();

    /** Free a codec parameters object (handle becomes invalid). */
    public native void codecparFree(long par);

    /** Copy parameters from a codec context to an AVCodecParameters (for muxing). */
    public native int codecparFromContext(long par, long codecCtx);

    /**
     * Set (copy in) codecpar extradata for stream {@code index} from the given
     * Java {@code byte[]} (e.g. SPS/PPS for H.264). Pass null/0 to clear.
     * @return bytes copied, or negative AVERROR
     */
    public native int codecparSetExtradata(long fmtCtx, int streamIndex, byte[] in, int off, int len);

    /**
     * Set (copy in) the codec context's {@code extradata} directly from a Java
     * {@code byte[]} (e.g. ExoPlayer {@code Format.initializationData}: AAC
     * AudioSpecificConfig, OpusHead, Vorbis ident/comment/setup headers, ALAC
     * magic cookie, SPS/PPS for H.264). Call this after
     * {@link #allocCodecContext} / {@link #codecParametersToContext} but before
     * {@link #codecOpen2}. Pass null/0 to clear.
     *
     * @return bytes copied, or negative AVERROR
     */
    public native int codecContextSetExtradata(long codecCtx, byte[] in, int off, int len);


    /* -------------------------------------------------------------- */
    /* MediaCodec hardware decode: bind an Android Surface to the      */
    /* decoder so h264/hevc/av1_mediacodec output renders directly to  */
    /* the Surface.                                                    */
    /* -------------------------------------------------------------- */

    /**
     * Allocate a {@code AVMediaCodecContext} (for binding to a Surface).
     * Must be paired with {@link #mediasurfaceDefaultInit} before
     * {@link #codecOpen2}, and {@link #mediasurfaceDefaultFree} before
     * {@link #codecFreeContext}.
     *
     * @return a non-zero handle, or 0 on allocation failure
     */
    public native long mediasurfaceAllocContext();

    /**
     * Bind an Android {@code Surface} to the codec context's
     * {@code hwaccel_context}. This NewGlobalRefs the Surface internally and
     * must run before {@link #codecOpen2}; the Surface takes effect during
     * MediaCodec's configure phase.
     *
     * <p><b>Caveat (mediacodec decoder):</b> on the mediacodec hardware
     * decoders ({@code h264_mediacodec}, {@code hevc_mediacodec}, ...) this
     * ad-hoc {@code hwaccel_context} path is <b>not</b> what drives Surface
     * zero-copy. Under the default {@code get_format} the decoder selects
     * its pixel format by inspecting {@code hw_device_ctx}; since this path
     * never sets {@code hw_device_ctx}, {@code get_format} returns
     * {@link #AV_PIX_FMT_MEDIACODEC} never, the decoder's Surface branch is
     * skipped, and frames come back as {@link #AV_PIX_FMT_NV12} (23) —
     * decoded in software buffers, not rendered to the Surface. New code that
     * wants the zero-copy Surface path on mediacodec decoders should use
     * {@link #mediasurfaceHwdeviceCreate} + {@link #setContextHwDeviceCtx}
     * instead. This method is retained for compatibility with older callers
     * and still works for the non-mediacodec Surface-binding it historically
     * documented.
     *
     * <p>Typical order:
     * <pre>
     * long mc = mediasurfaceAllocContext();
     * if (mediasurfaceDefaultInit(codecCtx, mc, surface) < 0) {
     *     mediasurfaceFree(mc);      // rollback
     *     return error;
     * }
     * codecOpen2(codecCtx, codec);   // surface effective at configure
     * ...decode...
     * mediasurfaceDefaultFree(codecCtx);   // release surface global ref + mc first
     * codecFreeContext(codecCtx);          // then free the codec context
     * </pre>
     *
     * <p>Changing the Surface at runtime is NOT supported; to change it you
     * must close and reopen (mediasurfaceDefaultFree + re-allocContext /
     * setSurface / defaultInit + re-codecOpen2).
     *
     * @return 0 or a negative AVERROR (e.g. {@code ENOSYS} when built without
     *         {@code --enable-mediacodec})
     */
    public native int mediasurfaceDefaultInit(long codecCtx, long mcCtx, android.view.Surface surface);

    /**
     * Free the {@code AVMediaCodecContext} bound by
     * {@link #mediasurfaceDefaultInit} plus the Surface global ref it holds.
     * <b>Must be called before {@link #codecFreeContext}</b>. Calling it twice
     * is safe (the second call is a no-op).
     */
    public native void mediasurfaceDefaultFree(long codecCtx);

    /**
     * Free an {@code AVMediaCodecContext} that was never successfully
     * initialized (rollback only, after a failed
     * {@link #mediasurfaceDefaultInit}).
     *
     * <p><b>Use-after-free / double-free caveat.</b> This releases the
     * {@code AVMediaCodecContext} handle directly. After a successful
     * {@link #mediasurfaceDefaultInit} the codec context owns that same
     * pointer via {@code avctx->hwaccel_context}, and
     * {@link #mediasurfaceDefaultFree} will later both {@code DeleteGlobalRef}
     * the Surface and {@code av_freep} the same memory. Therefore once init
     * has succeeded <b>this method MUST NOT be called</b> on the returned
     * handle: doing so leaves {@code hwaccel_context} dangling and the
     * subsequent {@code mediasurfaceDefaultFree} will trigger a
     * use-after-free / double-free (and a JVM abort on the dangling
     * {@code DeleteGlobalRef}).
     *
     * <p>Use this only as the rollback of a <i>failed</i>
     * {@code mediasurfaceDefaultInit} (return value {@code < 0}), which has
     * already unwound the {@code hwaccel_context} install and is safe to
     * clean up. Once init returns {@code 0} the Surface lifecycle is owned
     * exclusively by {@link #mediasurfaceDefaultFree}.
     */
    public native void mediasurfaceFree(long mcCtx);

    /**
     * Allocate an {@code AV_HWDEVICE_TYPE_MEDIACODEC} device whose
     * {@code hwctx->surface} holds the given Android {@code Surface}, ready
     * to be attached to a codec context with
     * {@link #setContextHwDeviceCtx}. This is the path that actually drives
     * Surface zero-copy on the mediacodec hardware decoders (unlike
     * {@link #mediasurfaceDefaultInit}, see its caveat): once the device is
     * set as {@code hw_device_ctx}, the decoder's default {@code get_format}
     * returns {@link #AV_PIX_FMT_MEDIACODEC} and decoded frames carry an
     * {@code AVMediaCodecBuffer*} in {@code frame->data[3]} for
     * {@link #mediasurfaceRenderBufferAtTime}.
     *
     * <p>The handle is an opaque {@code AVBufferRef*} (0 on failure). Release
     * it with {@link #bufferRefFree} after the codec context has been freed;
     * dropping the last reference invokes a free callback that deletes the
     * Surface global ref this method created.
     *
     * <p>Typical order:
     * <pre>
     * long dev = mediasurfaceHwdeviceCreate(surface);   // 0 == failure
     * // ... codecAllocContext3 + parametersToContext ...
     * setContextHwDeviceCtx(codecCtx, dev);             // BEFORE codecOpen2
     * codecOpen2(codecCtx, codec);
     * // assert getContextPixFmt(codecCtx) == AV_PIX_FMT_MEDIACODEC
     * ...decode; mediasurfaceGetBuffer / mediasurfaceRenderBufferAtTime...
     * codecFreeContext(codecCtx);                       // unrefs its copy of dev
     * bufferRefFree(dev);                               // last ref -> free callback
     * </pre>
     *
     * @return a non-zero {@code AVBufferRef*} handle, or 0 on allocation or
     *         init failure
     */
    public native long mediasurfaceHwdeviceCreate(android.view.Surface surface);

    /**
     * Attach a hardware device context (e.g. the one returned by
     * {@link #mediasurfaceHwdeviceCreate}) to a codec context as
     * {@code avctx->hw_device_ctx}, transferring a new reference to the
     * codec. Any previously-attached {@code hw_device_ctx} is released first.
     * Must be called before {@link #codecOpen2} so the decoder consults it
     * during {@code get_format}. {@link #codecFreeContext} drops the codec's
     * own reference; the caller still owns the original handle and must
     * release it with {@link #bufferRefFree}.
     *
     * @return 0 on success, {@code AVERROR(EINVAL)} if either handle is null,
     *         {@code AVERROR(ENOMEM)} if the new reference could not be
     *         allocated
     */
    public native int setContextHwDeviceCtx(long codecCtx, long hwDeviceCtx);


    /* -------------------------------------------------------------- */
    /* Codec context configuration setters (encoding setup)           */
    /* -------------------------------------------------------------- */

    /** Set the codec target bitrate (bits/sec). */
    public native void setContextBitRate(long codecCtx, long bitRate);
    /** Set the GOP size (I-frame interval, in frames). */
    public native void setContextGopSize(long codecCtx, int gop);
    /** Set the maximum number of B-frames between non-B-frames. */
    public native void setContextMaxBFrames(long codecCtx, int maxBFrames);
    /** Set the global quality (used with {@code AV_OPT_FLAG_QSCALE}). */
    public native void setContextGlobalQuality(long codecCtx, int quality);
    /** Set the minimum quantizer. */
    public native void setContextQmin(long codecCtx, int qmin);
    /** Set the maximum quantizer. */
    public native void setContextQmax(long codecCtx, int qmax);
    /** Set the number of encoding/decoding threads. */
    public native void setContextThreadCount(long codecCtx, int count);
    /** Set the pixel format (video encoding). */
    public native void setContextPixFmt(long codecCtx, int pixFmt);
    /** Set the coded width (video encoding). */
    public native void setContextWidth(long codecCtx, int width);
    /** Set the coded height (video encoding). */
    public native void setContextHeight(long codecCtx, int height);
    /** Set the codec time base (num/den). Important for encoding. */
    public native void setContextTimeBase(long codecCtx, int num, int den);
    /**
     * Set the time base of incoming packets (decoding). Decoders use it to
     * rescale packet timestamps; {@link #codecParametersToContext} already
     * sets it, so this is only needed when building a context by hand.
     */
    public native void setContextPktTimebase(long codecCtx, int num, int den);
    /** Set the frame rate (num/den) for video encoding. */
    public native void setContextFramerate(long codecCtx, int num, int den);
    /** Set the audio sample rate (Hz). */
    public native void setContextSampleRate(long codecCtx, int rate);
    /** Set the audio sample format ({@code AV_SAMPLE_FMT_*}). */
    public native void setContextSampleFmt(long codecCtx, int sampleFmt);
    /** Set the audio channel count (applies a default channel layout). */
    public native void setContextChannels(long codecCtx, int channels);

    /* -------------------------------------------------------------- */
    /* Codec context getters (parity)                                  */
    /* -------------------------------------------------------------- */

    /** @return codec target bitrate, 0 if unset. */
    public native long getContextBitRate(long codecCtx);
    /** @return GOP size. */
    public native int getContextGopSize(long codecCtx);
    /** @return audio sample rate (Hz). */
    public native int getContextSampleRate(long codecCtx);
    /** @return audio sample format, -1 if N/A. */
    public native int getContextSampleFmt(long codecCtx);
    /** @return audio channel count. */
    public native int getContextChannels(long codecCtx);
    /** @return numerator of the context time base. */
    public native int getContextTimeBaseNum(long codecCtx);
    /** @return denominator of the context time base. */
    public native int getContextTimeBaseDen(long codecCtx);

    /* -------------------------------------------------------------- */
    /* Generic option API (av_opt_*)                                  */
    /* -------------------------------------------------------------- */

    /**
     * Set a string option on any option-enabled object (codec context,
     * format context, etc.). Use this for string options like "preset".
     * @return 0 on success, negative AVERROR
     */
    public native int optSetString(long obj, String name, String val);
    /** Set an integer option. @return 0 on success, negative AVERROR */
    public native int optSetInt(long obj, String name, long val);
    /** Set a double/float option. @return 0 on success, negative AVERROR */
    public native int optSetDouble(long obj, String name, double val);
    /**
     * Get an integer option value.
     * @return the option value, or a negative AVERROR on failure
     */
    public native long optGetInt(long obj, String name);

    /** Set a rational option (e.g. {@code "framerate"}). @return 0 or AVERROR. */
    public native int optSetQ(long obj, String name, int num, int den);

    /** Set an image-size option (e.g. {@code "video_size"} w x h). */
    public native int optSetImageSize(long obj, String name, int w, int h);

    /** @return a double/float option value, or AVERROR as double on failure. */
    public native double optGetDouble(long obj, String name);

    /** @return a string option value, or null on failure. */
    public native String optGetString(long obj, String name);

    /* -------------------------------------------------------------- */
    /* Logging                                                        */
    /* -------------------------------------------------------------- */

    /** Set the global FFmpeg log level ({@code AV_LOG_*}). */
    public native void logSetLevel(int level);
    /** @return the current global log level. */
    public native int logGetLevel();


    /* ================================================================== */
    /* AVPacket                                                            */
    /* ================================================================== */

    /** Allocate a packet. @return AVPacket handle, 0 on failure. */
    public native long packetAlloc();

    /** Free the packet (handle becomes invalid). */
    public native void packetFree(long pkt);

    /**
     * Deep-copy a packet (av_packet_clone) into a new independent handle.
     * @return new AVPacket handle (caller must {@link #packetFree}), 0 on failure.
     */
    public native long packetClone(long pkt);

    /** Reset the packet for reuse without freeing). */
    public native void packetUnref(long pkt);

    /** @return payload size in bytes. */
    public native int packetGetSize(long pkt);

    /** @return presentation timestamp, or {@link #AV_NOPTS_VALUE}. */
    public native long packetGetPts(long pkt);

    /** @return decoding timestamp, or {@link #AV_NOPTS_VALUE}. */
    public native long packetGetDts(long pkt);

    /** @return index of the stream this packet belongs to. */
    public native int packetGetStreamIndex(long pkt);

    /** @return {@code AV_PKT_FLAG_*} bitmask. */
    public native int packetGetFlags(long pkt);

    /**
     * Copy up to {@code len} bytes of packet payload into {@code out}.
     *
     * <p>Partial copies are allowed by contract: the return value may be less
     * than {@code len} when the payload is shorter. A caller that needs the
     * whole payload must compare the return value against
     * {@link #packetGetSize} rather than assuming success.
     *
     * @param off first index of {@code out} to write
     * @param len maximum number of bytes to copy
     * @return bytes copied (>= 0), {@code 0} when the packet has no payload,
     *         or a negative AVERROR on bad arguments (null handle/array,
     *         negative {@code off}/{@code len}, or a range that does not fit
     *         in {@code out})
     */
    public native int packetCopyData(long pkt, byte[] out, int off, int len);

    /**
     * Allocate a new buffer of {@code size} bytes for the packet.
     * @return 0 on success, negative AVERROR
     */
    public native int newPacket(long pkt, int size);

    /** Ensure the packet's buffer is writable (copies on write). */
    public native int packetMakeWritable(long pkt);

    /**
     * Rescale the packet's pts/dts/duration between time bases.
     * @param srcNum/srcDen source time base, dstNum/dstDen target time base
     */
    public native void packetRescaleTs(long pkt,
                                       int srcNum, int srcDen, int dstNum, int dstDen);

    /**
     * Allocate the packet buffer and copy {@code len} bytes from {@code in}.
     * @return bytes copied, or negative AVERROR
     */
    public native int packetCopyFrom(long pkt, byte[] in, int off, int len);

    /* -------------------------------------------------------------- */
    /* AVPacket setters/getters                                      */
    /* -------------------------------------------------------------- */

    /** @return packet duration (in stream time base units), 0 if unknown. */
    public native long packetGetDuration(long pkt);
    /** @return byte position in stream, -1 if unknown. */
    public native long packetGetPos(long pkt);
    /** Set the packet pts. */
    public native void packetSetPts(long pkt, long pts);
    /** Set the packet dts. */
    public native void packetSetDts(long pkt, long dts);
    /** Set the packet duration. */
    public native void packetSetDuration(long pkt, long duration);
    /** Set the packet's owning stream index. */
    public native void packetSetStreamIndex(long pkt, int index);
    /** Set the packet flags ({@code AV_PKT_FLAG_*}). */
    public native void packetSetFlags(long pkt, int flags);
    /**
     * Copy a side-data element of {@code type} into {@code out}. Pass type =
     * {@code AV_PKT_DATA_*} (use the integer constants at the end of this
     * class).
     *
     * <p>Legacy entry point retained unchanged for existing callers. It
     * truncates to {@code len} and reports -1 both for "no such side data"
     * and for a rejected call, so it cannot tell an empty entry from a missing
     * one. New code should use {@link #packetGetSideDataSize} and
     * {@link #packetCopySideDataExact} instead.
     *
     * @return bytes copied, or -1 if no such side data or on bad arguments
     */
    public native int packetGetSideData(long pkt, int type, byte[] out, int off, int len);


    /* ================================================================== */
    /* AVFrame                                                             */
    /* ================================================================== */

    /** Allocate a frame. @return AVFrame handle, 0 on failure. */
    public native long frameAlloc();

    /** Free the frame (handle becomes invalid). */
    public native void frameFree(long frame);

    /** Reset the frame for reuse (keep allocation). */
    public native void frameUnref(long frame);

    /** @return coded width (pixels). */
    public native int frameGetWidth(long frame);

    /** @return coded height (pixels). */
    public native int frameGetHeight(long frame);

    /** @return pixel/sample format of the frame. */
    public native int frameGetFormat(long frame);

    /** @return presentation timestamp, or {@link #AV_NOPTS_VALUE}. */
    public native long frameGetPts(long frame);

    /** @return best-effort timestamp, or {@link #AV_NOPTS_VALUE}. */
    public native long frameGetBestEffortTimestamp(long frame);

    /** @return packet DTS propagated to the frame, or {@link #AV_NOPTS_VALUE}. */
    public native long frameGetPktDts(long frame);

    /** @return 1 if the frame is a key frame, 0 otherwise. */
    public native int frameIsKeyFrame(long frame);

    /** @return linesize (stride in bytes) for the given plane/data pointer. */
    public native int frameGetLineSize(long frame, int plane);

    /** @return number of audio samples per channel (video returns 0). */
    public native int frameGetNbSamples(long frame);

    /** @return channel count from the frame's channel layout (video returns 0). */
    public native int frameGetChannels(long frame);

    /**
     * Replace an {@code AV_CHANNEL_ORDER_UNSPEC} frame layout with a definite
     * one, in place, and leave an already-definite layout untouched (idempotent).
     *
     * <p>Call this on a freshly decoded audio frame <em>before</em> the first
     * {@link #swrConvertFrame}. libswresample compares each frame's layout with
     * the one it derived at {@code swr_init}; an UNSPEC frame can never match
     * that derived layout, so every frame after the first fails with
     * {@code AVERROR_INPUT_CHANGED}. Normalising once keeps the layout stable
     * for the whole stream without rebuilding the resampler (which would drop
     * the tail samples it still buffers).
     *
     * <p>The frame must be owned exclusively by the caller: the layout is
     * modified in place and must not be shared with another thread or frame.
     *
     * @param frame  the frame handle
     * @param declaredMask a channel-layout <em>bitmask value</em> from
     *        {@link #streamGetParChannelLayout} (not a pointer), or 0 when the
     *        container declares nothing usable. Read only; never freed. It is
     *        used only when its channel count equals the frame's.
     * @return {@code 0} when the layout was already definite (nothing changed);
     *         the channel count ({@code > 0}) when the layout was normalised;
     *         or a negative AVERROR ({@code EINVAL}) when the frame is null, the
     *         layout is invalid, or an UNSPEC layout has more than two channels
     *         that no declaration covers. Multi-channel layouts are never
     *         guessed: 5.1, 5.1(side) and hexa all have six channels, and a
     *         wrong pick silently mis-maps every sample.
     *
     * <p>The single/double-channel case uses {@code av_channel_layout_default},
     * which maps 1 to FRONT_CENTER and 2 to FL|FR. That is a convention that
     * matches real mono/stereo PCM, not something read from the stream; for a
     * single channel the position carries no information.
     *
     * @see #CAP_FRAME_LAYOUT_NORMALIZE
     */
    public native int frameNormalizeChannelLayout(long frame, long declaredMask);

    /**
     * Same policy as {@link #frameNormalizeChannelLayout}, for callers holding a
     * mask and channel count instead of a frame: use it during stream setup to
     * find out whether a layout can be normalised before any frame is decoded.
     *
     * <p>This answers "is this combination workable?", not "did anything
     * change?", so a mask that already matches the channel count reports that
     * count ({@code > 0}) rather than 0.
     *
     * @param currentMask the mask value describing the layout, or 0 when there
     *        is none
     * @param nbChannels  the channel count
     * @return the channel count ({@code > 0}) when the combination is usable, or
     *         a negative AVERROR ({@code EINVAL}) when the mask is invalid, the
     *         two disagree, or there are more than two channels and no mask to
     *         take the order from
     */
    public native int channelLayoutNormalizeMask(long currentMask, int nbChannels);

    /** @return sample rate, 0 if N/A. */
    public native int frameGetSampleRate(long frame);

    /** @return sample format, -1 if N/A. */
    public native int frameGetSampleFormat(long frame);

    /**
     * Copy all planes of a decoded video frame into a flat buffer (rows are
     * linesize-aligned). The buffer must be at least
     * {@code width*height*bytesPerPixel} bytes for packed formats; for planar
     * formats use the per-plane linesize. Returns bytes written, or negative
     * AVERROR (e.g. {@code ENOMEM} if {@code out} is too small).
     */
    public native int frameCopyVideo(long frame, byte[] out, int off, int len);

    /**
     * Copy decoded audio samples into a flat buffer. For interleaved
     * (non-planar) formats the whole buffer is copied; for planar formats
     * only plane 0 (channel 0) is copied. Size {@code out} as
     * {@code nbSamples * channels * bytesPerSample} for interleaved, or
     * {@code nbSamples * bytesPerSample} for one planar channel.
     * @return bytes copied, or negative AVERROR
     */
    public native int frameCopyAudio(long frame, byte[] out, int off, int len);

    /* -------------------------------------------------------------- */
    /* MediaCodec output buffer rendering. Only valid for frames whose  */
    /* {@link #frameGetFormat} == {@link #AV_PIX_FMT_MEDIACODEC}.       */
    /* -------------------------------------------------------------- */

    /**
     * Returns a non-zero {@code AVMediaCodecBuffer} handle only when
     * {@code frameGetFormat(frame) == AV_PIX_FMT_MEDIACODEC}; ordinary
     * (software) frames return 0. After acquiring the handle you must call
     * exactly one of {@link #mediasurfaceReleaseBuffer} or
     * {@link #mediasurfaceRenderBufferAtTime}, then {@link #frameUnref} to
     * recycle the {@code AVFrame} and release the underlying MediaCodec
     * buffer. The handle is only valid while {@code frame} is alive; it is
     * invalidated by {@link #frameUnref}.
     */
    public native long mediasurfaceGetBuffer(long frame);

    /**
     * Release an {@code AVMediaCodecBuffer}; {@code render == 1} renders it to
     * the bound Surface, {@code render == 0} discards it. Repeated calls are
     * safe (only the first caller truly renders the underlying MediaCodec
     * output buffer).
     *
     * @return 0 or a negative AVERROR ({@code ENOSYS} means the build was
     *         compiled without {@code --enable-mediacodec})
     */
    public native int mediasurfaceReleaseBuffer(long buf, int render);

    /**
     * Release an {@code AVMediaCodecBuffer} and render it at the given system
     * time (CLOCK_MONOTONIC ns, aligned with {@code System.nanoTime()}). The
     * release semantics match {@link #mediasurfaceReleaseBuffer}; only the
     * timing differs.
     *
     * @return 0 or a negative AVERROR
     */
    public native int mediasurfaceRenderBufferAtTime(long buf, long nanoTime);

    /**
     * Hardware buffers handed out by {@link #mediasurfaceGetBuffer} that have
     * not yet been released.
     *
     * <p>This is the bridge's own count, not the decoder's: the codec's
     * internal buffer count is private to libavcodec. A non-zero value means
     * the MediaCodec producer cannot be considered detached -- those buffers
     * still reference the Surface, and
     * {@code ff_mediacodec_dec_close()} will skip stopping the codec while
     * any are outstanding.
     *
     * <p>Close order: release every outstanding buffer (or unref every frame
     * holding one), confirm this returns 0, and only then free the codec.
     *
     * @return the outstanding count (>= 0)
     */
    public native int mediasurfaceGetOutstandingBuffers();

    /* -------------------------------------------------------------- */
    /* Software video rendering: sws_scale directly into an            */
    /* ANativeWindow buffer. Use this on the software-decode fallback  */
    /* path, where frames come back in an ordinary pixel format        */
    /* instead of {@link #AV_PIX_FMT_MEDIACODEC}.                       */
    /* -------------------------------------------------------------- */

    /**
     * Wrap a {@code Surface} in a native renderer that scales decoded frames
     * straight into the window's buffer. This connects to the Surface as a
     * buffer producer, so it is mutually exclusive with the MediaCodec
     * zero-copy path ({@link #mediasurfaceHwdeviceCreate}) on the same
     * Surface.
     *
     * @return an opaque renderer handle, or 0 on failure. Free with
     *         {@link #windowRendererFree}.
     */
    public native long windowRendererCreate(android.view.Surface surface);

    /**
     * Set the window's buffer geometry (RGBA_8888). Pass the video's native
     * resolution and let the Android view hierarchy scale the result.
     * @return 0 on success, negative AVERROR
     */
    public native int windowRendererSetSize(long handle, int width, int height);

    /**
     * Scale and post one decoded frame to the Surface. The internal
     * {@code SwsContext} is cached and only rebuilt when the frame geometry,
     * pixel format, or window buffer size changes.
     * @return 0 on success, negative AVERROR
     */
    public native int windowRendererRender(long handle, long frame);

    /* ------------------------------------------------------------------ */
    /* Output-session unbinding.                                           */
    /*                                                                     */
    /* A renderer or a MediaCodec binding is a buffer producer attached to */
    /* a Surface. Detaching is asynchronous on the driver side, so a       */
    /* function returning proves nothing: ff_mediacodec_dec_close() only   */
    /* calls FF_AMediaCodec_stop() when no hardware buffer is outstanding, */
    /* and otherwise closes the decoder while the codec may still be       */
    /* live (mediacodecdec_common.c). The session therefore tracks its own */
    /* admitted operations and reports DETACHED only when that count has   */
    /* reached zero.                                                       */
    /*                                                                     */
    /* Protocol: revoke -> poll finish until DETACHED -> free.             */
    /* If your deadline expires first, call quarantine, then free. Never   */
    /* treat a timeout as DETACHED, and never rebind a quarantined handle. */
    /*                                                                     */
    /* What DETACHED does NOT mean: the bridge never blocks inside         */
    /* ANativeWindow_lock()/post or a MediaCodec call on your behalf, so   */
    /* no poll count, sleep interval or watchdog in this layer can turn an  */
    /* uncancellable platform call into a bounded exit. A timeout is        */
    /* QUARANTINED, and a quarantined session keeps its resources.          */
    /* ------------------------------------------------------------------ */

    /** Session accepts work. */
    public static final int OUTPUT_SESSION_ACTIVE = 0;
    /** Revoked: no new work is admitted; admitted work is draining. */
    public static final int OUTPUT_SESSION_REVOKING = 1;
    /**
     * Revoked, no operation is admitted any more, and the platform window
     * reference has been released exactly once. Nothing this renderer still
     * does can touch the old Surface.
     */
    public static final int OUTPUT_SESSION_DETACHED = 2;
    /** Teardown could not be proven; the handle is unusable and must be dropped. */
    public static final int OUTPUT_SESSION_QUARANTINED = 3;
    /** Freed. */
    public static final int OUTPUT_SESSION_CLOSED = 4;

    /**
     * Stop admitting work and begin unbinding.
     *
     * <p>Returns immediately: it never waits for admitted work. An operation
     * already inside the driver cannot be interrupted from this layer, so
     * waiting here would only mean waiting on a timeout.
     *
     * @return the number of operations still in flight (>= 0), or a negative
     *         AVERROR (including {@code EINVAL} for a handle that was never
     *         issued or is already freed). Poll {@link #windowRendererFinish}
     *         until it returns {@link #OUTPUT_SESSION_DETACHED}.
     */
    public native int windowRendererRevoke(long handle);

    /**
     * Attempt to complete teardown without waiting.
     *
     * @return an {@code OUTPUT_SESSION_*} state.
     *         {@link #OUTPUT_SESSION_DETACHED} means all three of: the session
     *         was revoked, no operation is admitted, and the window reference
     *         has been released. It is never returned because a poll budget
     *         ran out.
     */
    public native int windowRendererFinish(long handle);

    /**
     * Abandon teardown. The handle becomes unusable and must not be reused
     * or rebound. Quarantine refuses reuse; it does not authorise freeing
     * anything a producer may still be using.
     *
     * @return the number of operations still in flight, for diagnostics
     */
    public native int windowRendererQuarantine(long handle);

    /** @return the current {@code OUTPUT_SESSION_*} state. */
    public native int windowRendererGetState(long handle);

    /**
     * Release the renderer, its scaler, and the underlying window reference.
     *
     * <p>Retires the handle first, so no new operation can start. If an
     * operation is still admitted, it does <em>not</em> free the scaler, the
     * window, the locks or the object: the whole renderer is retained until
     * the last admitted call returns, then released. Calling Free twice, or
     * with a handle that was already freed, is a no-op.
     *
     * <p>Calling Free without revoking first is supported (it revokes
     * internally), but it gives you no chance to observe
     * {@link #OUTPUT_SESSION_DETACHED}; prefer the full protocol.
     */
    public native void windowRendererFree(long handle);

    /* -------------------------------------------------------------- */
    /* AAC raw -> ADTS packer (audio passthrough / offload).           */
    /*                                                                  */
    /* Containers such as Matroska carry AAC as raw access units with  */
    /* the AudioSpecificConfig in codecpar->extradata, while Android's */
    /* ENCODING_AAC_LC direct/offload sinks expect ADTS-framed input.  */
    /* Only AAC LC-family configs with chan_config 1..7 can be        */
    /* ADTS-packed (no PCE support); HE/xHE cannot -- create returns   */
    /* 0 for those and the caller falls back to software decoding.     */
    /* -------------------------------------------------------------- */

    /**
     * Create a packer from the AudioSpecificConfig in stream
     * {@code index}'s codecpar->extradata.
     *
     * @return an opaque packer handle, or 0 when the stream cannot be
     *         ADTS-packed (no extradata, AOT outside Main/LC/SSR/LTP,
     *         escape sampling index, chan_config 0 / > 7). Free with
     *         {@link #aacAdtsPackerFree}.
     */
    public native long aacAdtsPackerCreate(long formatCtx, int streamIndex);

    /**
     * Wrap one raw AAC access unit ({@code pkt}) in a 7-byte ADTS header and
     * copy [header][payload] into {@code out} at {@code off}. {@code out}
     * must have room for {@code 7 + packetSize} bytes.
     *
     * @return total bytes written ({@code 7 + packetSize}), or a negative
     *         AVERROR ({@code EINVAL} when the buffer is too small or the
     *         frame exceeds the 16383-byte ADTS limit).
     */
    public native int aacAdtsPackerWrap(long handle, long pkt, byte[] out, int off, int len);

    /** Release the packer. */
    public native void aacAdtsPackerFree(long handle);

    /* -------------------------------------------------------------- */
    /* Interrupt flag: abort blocking I/O from another thread.         */
    /*                                                                 */
    /* A demux thread parked in a network read can otherwise only be   */
    /* unblocked by the protocol's rw_timeout. Closing the             */
    /* AVFormatContext while that read is in flight frees the          */
    /* URLContext under the blocked thread, which then crashes on a    */
    /* dangling interrupt_callback. Raise the flag first, join the     */
    /* thread, then close the context.                                 */
    /*                                                                 */
    /* Typical order:                                                  */
    /*   long flag = interruptFlagCreate();                            */
    /*   ctx = formatOpenInput(url, opts);                             */
    /*   formatSetInterruptFlag(ctx, flag);                            */
    /*   ... demux on another thread ...                               */
    /*   interruptFlagAbort(flag);   // readFrame -> AVERROR_EXIT      */
    /*   demuxThread.join();                                           */
    /*   formatCloseInput(ctx);                                        */
    /*   interruptFlagFree(flag);    // only after every ctx is closed */
    /* -------------------------------------------------------------- */

    /**
     * Allocate a cleared interrupt flag.
     *
     * @return an opaque flag handle, or 0 on allocation failure. Free with
     *         {@link #interruptFlagFree}.
     */
    public native long interruptFlagCreate();

    /**
     * Install {@code flag} as the context's {@code interrupt_callback}, so
     * blocking operations on it abort with {@code AVERROR_EXIT} once the flag
     * is raised.
     *
     * @return 0 on success, or a negative AVERROR
     */
    public native int formatSetInterruptFlag(long formatCtx, long flag);

    /**
     * Raise the flag: in-flight and subsequent blocking I/O on any context
     * holding it fails with {@code AVERROR_EXIT}. Callable from any thread and
     * idempotent.
     */
    public native void interruptFlagAbort(long flag);

    /**
     * Free the flag. Every {@link #formatSetInterruptFlag} context must be
     * closed first, otherwise its callback would read freed memory.
     */
    public native void interruptFlagFree(long flag);

    /**
     * Allocate the frame's buffers for its current format/dimensions. Set
     * width/height (or sample rate/channels/sample format) first with the
     * setters below.
     * @param align alignment in bytes; pass 0 for default
     * @return 0 on success, negative AVERROR
     */
    public native int frameGetBuffer(long frame, int align);

    /**
     * Configure the frame for video encoding (call before
     * {@link #frameGetBuffer}).
     */
    public native void frameSetVideoFormat(long frame, int pixFmt, int width, int height);

    /**
     * Configure the frame for audio encoding (call before
     * {@link #frameGetBuffer}).
     */
    public native void frameSetAudioFormat(long frame, int sampleFmt, int sampleRate, int channels);

    /** Ensure the frame's buffers are writable (copies on write). @return 0 or AVERROR. */
    public native int frameMakeWritable(long frame);

    /* -------------------------------------------------------------- */
    /* AVFrame field getters                                        */
    /* -------------------------------------------------------------- */

    /** @return frame duration (in stream time base units), 0 if unknown. */
    public native long frameGetDuration(long frame);
    /** @return byte position of the packet this frame came from, -1 if unknown. */
    public native long frameGetPktPos(long frame);
    /** @return numerator of the sample aspect ratio. */
    public native int frameGetSarNum(long frame);
    /** @return denominator of the sample aspect ratio. */
    public native int frameGetSarDen(long frame);
    /** @return {@code AVCOL_RANGE_*} color range. */
    public native int frameGetColorRange(long frame);
    /** @return {@code AVCOL_SPC_*} color space (matrix). */
    public native int frameGetColorSpace(long frame);
    /** @return {@code AVCOL_PRI_*} color primaries. */
    public native int frameGetColorPrimaries(long frame);
    /** @return {@code AVCOL_TRC_*} transfer characteristics. */
    public native int frameGetColorTrc(long frame);
    /** @return {@code AV_PICTURE_TYPE_*} picture type (I/P/B/...). */
    public native int frameGetPictType(long frame);


    /* ================================================================== */
    /* libavfilter graph (transform pipelines)                            */
    /* ================================================================== */

    /** Allocate a filter graph. @return AVFilterGraph handle, 0 on failure. */
    public native long filterGraphAlloc();

    /** Free the filter graph. */
    public native void filterGraphFree(long graph);

    /** Find a filter by name (e.g. "buffer", "buffersink", "scale"). @return AVFilter handle. */
    public native long filterGetByName(String name);

    /**
     * Create a filter instance in the graph.
     * @param graph  AVFilterGraph handle from {@link #filterGraphAlloc}
     * @param filter AVFilter handle from {@link #filterGetByName}
     * @param name   instance name, or null
     * @param args   filter arguments string (e.g. width:height:pixfmt:timebase), or null
     * @return AVFilterContext handle, 0 on failure
     */
    public native long filterGraphCreateFilter(long graph, long filter, String name, String args);

    /**
     * Parse and link a filter chain string into the graph, connecting the
     * source filter to the {@code [in]} label and the sink to {@code [out]}.
     * Use the form {@code "[in] scale=640:480 [out]"}.
     * @return 0 on success, negative AVERROR
     */
    public native int filterGraphParsePtr(long graph, String filters, long srcCtx, long sinkCtx);

    /** Configure/validate the graph after all filters and links are set. */
    public native int filterGraphConfig(long graph);

    /** Push a frame into the buffer source. Pass 0 to flush. */
    public native int bufferSrcAddFrame(long srcCtx, long frame);

    /** Pull a filtered frame from the buffer sink. @return 0 or AVERROR_EOF/EAGAIN. */
    public native int bufferSinkGetFrame(long sinkCtx, long frame);

    /** Push a frame into the buffer source with flags ({@code AV_BUFFERSRC_FLAG_*}). */
    public native int bufferSrcAddFrameFlags(long srcCtx, long frame, int flags);

    /** Pull a filtered frame from the buffer sink with flags. */
    public native int bufferSinkGetFrameFlags(long sinkCtx, long frame, int flags);

    /** Set the buffer sink's frame size for a pull-based filter. */
    public native void bufferSinkSetFrameSize(long sinkCtx, int frameSize);

    /** @return numerator of the buffer sink's frame rate. */
    public native int bufferSinkGetFrameRateNum(long sinkCtx);
    /** @return denominator of the buffer sink's frame rate. */
    public native int bufferSinkGetFrameRateDen(long sinkCtx);
    /** @return numerator of the buffer sink's time base. */
    public native int bufferSinkGetTimeBaseNum(long sinkCtx);
    /** @return denominator of the buffer sink's time base. */
    public native int bufferSinkGetTimeBaseDen(long sinkCtx);
    /** @return an AVBufferRef to the sink's {@code hw_frames_ctx} (free with {@link #bufferRefFree}), or 0. */
    public native long bufferSinkGetHwFramesCtx(long sinkCtx);

    /** Allocate an AVBufferSrcParameters (fill and pass to {@link #buffersrcParametersSet}). */
    public native long buffersrcParametersAlloc(long srcCtx);
    /** Apply AVBufferSrcParameters to the buffer source. */
    public native int buffersrcParametersSet(long srcCtx, long params);

    /** Request the oldest buffered output frame (pull mode). */
    public native int filterGraphRequestOldest(long graph);

    /**
     * Send a runtime command (e.g. {"scale", "width"} = "100") to a filter.
     * @param res {@code byte[]} filled with response (may be null), up to {@code resLen}
     */
    public native int filterGraphSendCommand(long graph, String target, String cmd, String arg,
                                             byte[] res, int resLen, int flags);

    /** Queue a command (non-blocking variant). */
    public native int filterGraphQueueCommand(long graph, String target, String cmd, String arg,
                                              int flags, double ts);

    /** @return a string dump of the filter graph (debugging). */
    public native String filterGraphDump(long graph, String options);

    /** @return the name of a filter pad (pass result of {@link #filterGetInputPads}/{@code Output}). */
    public native String filterPadGetName(long pads, int idx);
    /** @return the media type ({@code AVMEDIA_TYPE_*}) of a filter pad. */
    public native int filterPadGetType(long pads, int idx);
    /** @return number of input pads on a filter context. */
    public native int filterGetNbInputs(long filterCtx);
    /** @return number of output pads on a filter context. */
    public native int filterGetNbOutputs(long filterCtx);
    /** @return handle to a filter's input {@code AVFilterPad} array (use with {@link #filterPadGetName}). */
    public native long filterGetInputPads(long filterCtx);
    /** @return handle to a filter's output {@code AVFilterPad} array. */
    public native long filterGetOutputPads(long filterCtx);


    /* ================================================================== */
    /* libswscale (pixel format conversion / scaling)                      */
    /* ================================================================== */

    /**
     * Allocate a scaler context.
     *
     * @param flags scaler algorithm flags (e.g. {@code SWS_BILINEAR})
     * @return SwsContext handle, 0 on failure
     */
    public native long swsGetContext(int srcW, int srcH, int srcFmt,
                                     int dstW, int dstH, int dstFmt, int flags);

    /**
     * Scale {@code srcFrame} into {@code out} at the target dimensions and
     * pixel format. For {@code AV_PIX_FMT_RGBA} size {@code out} as
     * {@code dstW*dstH*4}; for other formats use the matching buffer-size
     * helper.
     *
     * @return bytes written, or negative AVERROR
     */
    public native int swsScaleFrame(long sws, long srcFrame,
                                    int dstW, int dstH, int dstFmt,
                                    byte[] out, int off, int len);

    /** Free the scaler context. */
    public native void swsFreeContext(long sws);

    /** Cached-context variant of {@link #swsGetContext}: reuse/replace {@code prev}. */
    public native long swsGetCachedContext(long prev, int srcW, int srcH, int srcFmt,
                                           int dstW, int dstH, int dstFmt, int flags);

    /** (Re-)initialise an existing scaler context after changing parameters. */
    public native int swsInitContext(long sws);

    /**
     * Scale one {@link #frameAlloc}-style frame into another, allocating the
     * destination buffers automatically. {@code dst}/@{code src} are AVFrame
     * handles. Use this for cleaner frame->frame workflows than
     * {@link #swsScaleFrame}.
     */
    public native int swsScaleFrameCtx(long sws, long dstFrame, long srcFrame);

    /**
     * Set colorspace details (YUV range coefficients / BT.601/709). Both
     * arrays must each have 4 ints.
     */
    public native int swsSetColorspaceDetails(long sws, int[] invTable, int srcRange,
                                              int[] table, int dstRange,
                                              int brightness, int contrast, int saturation);


    /* ================================================================== */
    /* libswresample (sample format / rate / channel conversion)          */
    /* ================================================================== */

    /** Allocate a resampler. @return SwrContext handle, 0 on failure. */
    public native long swrAlloc();

    /**
     * Configure input / output conversion parameters (integers).
     *
     * @param outFmt    {@code AV_SAMPLE_FORMAT_*}
     * @param outRate   output sample rate (Hz)
     * @param outLayout output channel layout ({@code AV_CH_LAYOUT_*})
     * @param inFmt     input sample format
     * @param inRate    input sample rate (Hz)
     * @param inLayout  input channel layout
     * @param logLevel  log_level_offset (0 by default)
     * @return 0
     */
    public native int swrSetOpts(long ctx,
                                 int outFmt, int outRate, long outLayout,
                                 int inFmt,  int inRate, long inLayout,
                                 int logLevel);

    /** Initialise the resampler after options are set. @return 0 or AVERROR. */
    public native int swrInit(long ctx);

    /**
     * Convert {@code inCount} input samples (interleaved in {@code in}) to
     * {@code outCount} output samples (interleaved in {@code out}). The
     * {@code in} buffer must hold
     * {@code inCount * inChannels * bytesPerSample(inFmt)} bytes; {@code out}
     * must hold {@code outCount * outChannels * bytesPerSample(outFmt)}.
     *
     * <p>{@code outCount} is a capacity, not a promise: the array's real
     * length is checked against it before anything is converted. A request
     * that does not fit returns {@code AVERROR(ENOMEM)} without touching the
     * resampler. Pass {@code in == null} to drain buffered tail samples.
     *
     * <p>libswresample consumes every input sample it is handed in a single
     * call, so an accepted call always consumes all {@code inCount} samples
     * even when it produces fewer than {@code outCount} output samples.
     * There is no partial-consumption result; size the output from
     * {@link #swrGetOutSamples} when that distinction matters.
     *
     * @return number of output samples produced, or negative AVERROR
     */
    public native int swrConvert(long ctx, byte[] out, int outCount,
                                 byte[] in, int inCount);

    /** Free the resampler. */
    public native void swrFree(long ctx);

    /**
     * Convert a whole AVFrame to another AVFrame (allocates dst as needed).
     *
     * <p>Passing {@code inFrame == 0} is valid and means "drain": buffered
     * tail samples are emitted into {@code outFrame}. Keep calling until
     * {@code outFrame}'s {@code nbSamples} reaches 0; a return of 0 only means
     * the conversion call succeeded and says nothing about how many samples
     * were produced.
     *
     * <p>{@code ctx} and {@code outFrame} must be non-null. The bridge does not
     * invent timestamps, does not declare end-of-stream, and does not take
     * ownership of either frame.
     *
     * @return 0 on success, negative AVERROR otherwise
     */
    public native int swrConvertFrame(long ctx, long outFrame, long inFrame);

    /* ================================================================== */
    /* Bounded PCM resampling session                                      */
    /*                                                                     */
    /* The swr* entry points above pass straight through to              */
    /* libswresample: they cannot stop it from buffering whatever it      */
    /* could not emit, so a "keep blocks small" policy on the Java side   */
    /* only moves the backlog into native memory where nothing counts it. */
    /*                                                                    */
    /* A session owns the resampler together with explicit limits, and    */
    /* enforces them before anything is allocated or consumed. All of it  */
    /* is gated on CAP_BOUNDED_PCM_RESAMPLE.                              */
    /* ================================================================== */

    /** Output samples were written; the input frame has been consumed. */
    public static final int PCM_OUTPUT_READY = 0;

    /**
     * All input was taken but nothing could be emitted yet (a filter that
     * needs look-ahead). Advance your input cursor and feed the next frame.
     *
     * <p>A zero-sample frame is not EOF: converting one returns this status
     * with {@code status[0] == 0}, and it never flushes the session.
     */
    public static final int PCM_INPUT_CONSUMED = 1;

    /**
     * Retained for numeric compatibility only. A synchronous drain reports
     * progress as {@link #PCM_OUTPUT_READY} / {@link #PCM_DRAINED}, and
     * {@link #pcmSessionConvert} never returns this: a drain is no longer
     * driven by polling an internal delay figure.
     */
    public static final int PCM_NEED_DRAIN = 2;

    /**
     * The tail is complete. Only {@link #pcmSessionDrain} reports this, and
     * only after an empty-input flush actually returned no samples; an empty
     * or already-finished session reports it without touching the resampler.
     * {@code status[0]} is 0 in that case.
     */
    public static final int PCM_DRAINED = 3;

    /**
     * The frame's format differs from the configured one and the old
     * resampler still holds a tail.
     *
     * <p>Nothing was consumed and nothing was reconfigured. Drain the old
     * tail to {@link #PCM_DRAINED} first, then present the same frame again:
     * the format change is committed at that point and the frame is converted
     * exactly once. Switching immediately would silently drop the tail.
     *
     * @see #pcmSessionDrain
     */
    public static final int PCM_FORMAT_CHANGE = 4;

    /**
     * Create a bounded resampling session.
     *
     * <p>The output format is fixed for the life of the session (the player's
     * AudioTrack does not change mid-stream). Input format is not specified
     * here: it is taken from the first frame converted and re-committed only
     * when a frame actually carries a different one.
     *
     * <p>Limits are in samples per channel:
     * <ul>
     *   <li>{@code maxOutSamples} bounds the output of any single convert or
     *       drain call. A request whose {@code swr_get_out_samples()} upper
     *       bound exceeds it is refused before conversion.</li>
     *   <li>{@code maxInSamples} bounds the input of any single call.</li>
     *   <li>{@code maxDelaySamples} bounds what the session may carry between
     *       calls. This is the limit that stops a caller from feeding input
     *       without ever draining it.</li>
     * </ul>
     *
     * <p>These bound samples in flight and buffers handed out; they are
     * <em>not</em> a hard cap on process memory. libswresample allocates its
     * filter tables and delay line inside {@code swr_init()}, before any
     * sample-count check can run, and the bridge keeps its own staging buffer
     * (at most 4 MiB, reused across calls) to publish results only after a
     * conversion succeeds. Budget the Java-side copies and the owner's
     * copies separately.
     *
     * <p><b>Output must be interleaved.</b> {@code outFmt} may not be a
     * planar format: the single {@code byte[]} entry points have no way to
     * express a plane layout, and a planar format is refused (0 returned)
     * rather than written as if it were packed. This is a deliberate
     * narrowing in 1.2; convert to packed on the caller side or use the
     * frame-based {@code swrConvert} path if planar output is needed.
     *
     * @param outFmt           {@code AV_SAMPLE_FMT_*} of the output, packed
     * @param outRate          output sample rate in Hz
     * @param outLayoutMask    {@code AV_CH_LAYOUT_*} of the output
     * @param maxOutSamples    per-call output limit, per channel; also
     *                         bounded by the bridge staging ceiling
     * @param maxInSamples     per-call input limit, per channel
     * @param maxDelaySamples  internal backlog limit, in output samples
     * @return session handle, or 0 on invalid arguments
     */
    public native long pcmSessionCreate(int outFmt, int outRate,
                                        long outLayoutMask,
                                        int maxOutSamples,
                                        int maxInSamples,
                                        int maxDelaySamples);

    /**
     * Convert one frame into {@code out}.
     *
     * <p>{@code outCount} is the output capacity in samples per channel, and
     * a request, not a promise: the array's real length is what limits the
     * call. {@code out} must hold {@code outCount * outChannels *
     * bytesPerSample(outFmt)} bytes; a shorter array is refused before the
     * resampler runs.
     *
     * <p>All input sample count, rate, format and channel layout are read
     * from {@code frame} for this call. A planar frame is read through its
     * full plane list, so multi-channel planar input works beyond the eight
     * inline plane pointers.
     *
     * <p>{@code status[0]} receives the number of output samples written (0
     * on any failure, including a refusal). The return value says what
     * happened, see the {@code PCM_*} constants.
     *
     * <p><b>Retry contract.</b> Negative returns fall into two groups. A
     * refusal that happens before conversion ({@code EINVAL},
     * {@code ENOMEM} for an undersized array, {@code ENOSPC} for a session
     * limit, {@code EOVERFLOW}) leaves the input unconsumed, so the frame may
     * be presented again with corrected arguments. A failure after
     * conversion started, including a failure to publish into {@code out},
     * leaves the session unusable ({@code EIO}); call
     * {@link #pcmSessionReset} or {@link #pcmSessionFree} and do not replay
     * the frame.
     *
     * @return a {@code PCM_*} status, or a negative AVERROR
     */
    public native int pcmSessionConvert(long handle, long frame,
                                        byte[] out, int outCount,
                                        int[] status);

    /**
     * Emit the buffered tail into {@code out}.
     *
     * <p>This is the only way a stream ends. Each call flushes with empty
     * input; a non-empty result means more tail is left, and an empty result
     * means the tail is complete and {@link #PCM_DRAINED} is reported with
     * {@code status[0] == 0}.
     *
     * <p>Small blocks are the intended use: the flush is resumable, so
     * {@code outCount} does not have to hold the whole tail, and a large
     * backlog never prevents draining. An unconfigured or already-finished
     * session reports {@link #PCM_DRAINED} without touching the resampler.
     *
     * <p>Drain does not obey {@code maxDelaySamples} (it is the way the
     * backlog is removed), but it does obey the output block limits.
     */
    public native int pcmSessionDrain(long handle, byte[] out, int outCount,
                                      int[] status);

    /**
     * Drop everything buffered and start a new stream.
     *
     * <p>For a seek or a discontinuity: the old timeline must not bleed into
     * the new one. Do not use it to retry a format change (drain instead) and
     * do not use it to recover from a mid-conversion failure expecting the
     * old samples back. The output format never changes; a previously
     * configured input format is rebuilt from the snapshot, so the next
     * convert is not treated as a format change.
     *
     * <p>Also the only way to feed input again after {@link #PCM_DRAINED}.
     *
     * @return 0, or negative AVERROR
     */
    public native int pcmSessionReset(long handle);

    /** Release the session and its resampler. Safe on 0. */
    public native void pcmSessionFree(long handle);

    /** @return next pts after conversion (used for A/V sync). */
    public native long swrNextPts(long ctx, long pts);

    /** Set clock-drift compensation (sample delta / distance). */
    public native int swrSetCompensation(long ctx, int sampleDelta, int compensationDistance);

    /** Inject {@code count} samples of silence. */
    public native int swrInjectSilence(long ctx, int count);

    /** Channel layout helpers (legacy outlier API). */
    public native int getChannelLayoutNbChannels(long layout);
    public native long getDefaultChannelLayout(int nbChannels);


    /** @return delay (in {@code base} units) introduced by the resampler. */
    public native long swrGetDelay(long ctx, long base);

    /** @return number of output samples expected for {@code inSamples}. */
    public native int swrGetOutSamples(long ctx, int inSamples);

    /* ================================================================== */
    /* Bitstream filter API (AnnexB <-> AVCC etc.)                       */
    /* ================================================================== */

    /**
     * Allocate a bitstream filter context by filter name
     * (e.g. {@code "h264_mp4toannexb"}, {@code "h264_metadata"}).
     * @return AVBSFContext handle, 0 on failure
     */
    public native long bsfAllocByName(String name);

    /**
     * Copy stream parameters into the filter's input codecpar (call before
     * {@link #bsfInit}).
     */
    public native int bsfCopyInputPar(long bsfCtx, long fmtCtx, int streamIndex);

    /** Initialise the bitstream filter. @return 0 or AVERROR. */
    public native int bsfInit(long bsfCtx);

    /** Send a packet to the filter (pass 0 to flush). @return 0/EAGAIN/EOF. */
    public native int bsfSendPacket(long bsfCtx, long pkt);

    /** Receive a filtered packet. @return 0/EAGAIN/EOF. */
    public native int bsfReceivePacket(long bsfCtx, long pkt);

    /** Free the bitstream filter context. */
    public native void bsfFree(long bsfCtx);

    /* ================================================================== */
    /* helpers                                                             */
    /* ================================================================== */

    /** @return bytes per sample for an {@code AV_SAMPLE_FORMAT_*} value. */
    public native int bytesPerSample(int sampleFmt);

    /** @return bits per pixel for an {@code AV_PIX_FMT_*} value. */
    public native int bitsPerPixel(int pixFmt);

    /** Copy entries from {@code srcBox} into {@code dstBox} applying {@code flags}. */
    public native int dictCopy(long dstBox, long srcBox, int flags);

    /**
     * Rescale a 64-bit timestamp between two time bases.
     * @return the rescaled timestamp
     */
    public native long rescaleQ(long a,
                                int bqNum, int bqDen, int cqNum, int cqDen);

    /** @return name of an {@code AV_PIX_FMT_*} value, or {@code null}. */
    public native String getPixFmtName(int pixFmt);

    /** @return name of an {@code AV_SAMPLE_FORMAT_*} value, or {@code null}. */
    public native String getSampleFmtName(int sampleFmt);

    /** @return media-type name (e.g. "video"/"audio") for an {@code AVMEDIA_TYPE_*}, or null. */
    public native String getMediaTypeString(int mediaType);

    /** @return the {@code AV_PIX_FMT_*} for a name string, or {@code AV_PIX_FMT_NONE}. */
    public native int getPixFmt(String name);

    /** @return the {@code AV_SAMPLE_FORMAT_*} for a name string, or {@code AV_SAMPLE_FMT_NONE}. */
    public native int getSampleFmt(String name);

    /** @return the {@code AV_CODEC_ID_*} for a codec name, or {@code AV_CODEC_ID_NONE}. */
    public native int getCodecIdByName(String name);

    /** Rescale {@code a} by {@code b/c}. @return the rescaled value. */
    public native long rescale(long a, long b, long c);

    /** Rescale {@code a} by {@code b/c} with the given rounding mode ({@code AV_ROUND_*}). */
    public native long rescaleRnd(long a, long b, long c, int rnd);

    /** Rescale {@code a} between two time bases with a rounding mode. */
    public native long rescaleQRnd(long a,
                                   int bqNum, int bqDen, int cqNum, int cqDen, int rnd);

    /** @return bytes needed for a {@code width*height} image of {@code pixFmt} (aligned). */
    public native int imageGetBufferSize(int pixFmt, int width, int height, int align);

    /** @return bytes needed for {@code nbSamples} of {@code nbChannels}/{@code sampleFmt} (aligned). */
    public native int samplesGetBufferSize(int nbChannels, int nbSamples, int sampleFmt, int align);

    /** Compare two timestamps in different time bases; returns -1/0/1 (or AV_NOPTS_VALUE). */
    public native int compareTs(long tsA, int aNum, int aDen, long tsB, int bNum, int bDen);

    /** Set a default channel layout (nbChannels) on an AVFrame handle. */
    public native int channelLayoutDefault(long frameOrCtx, int nbChannels);


    /* ================================================================== */
    /* Round 4: gap-closure additions (see ffmpeg_jni.c "Round 4")        */
    /* ================================================================== */

    /* -------------------------------------------------------------- */
    /* libavformat: pause/play, options, index, sdp, tag tables       */
    /* -------------------------------------------------------------- */

    /** Pause an input stream (network streams that support it). @return 0 or AVERROR. */
    public native int readPause(long ctx);

    /** Resume a paused input stream. @return 0 or AVERROR. */
    public native int readPlay(long ctx);

    /**
     * Variant of {@link #formatFindStreamInfo} that accepts a {@link #dictCreate}
     * box of options.
     * @return 0 on success, negative AVERROR
     */
    public native int formatFindStreamInfoOpts(long ctx, long optionsBox);

    /**
     * Search a stream's index for the entry nearest {@code timestamp}.
     * @return index of the entry, or -1 if none.
     */
    public native int indexSearchTimestamp(long ctx, int streamIndex, long timestamp, int flags);

    /**
     * Generate an SDP description for the muxer bound to {@code ctx}
     * (used for RTSP/RTP streaming). Writes the SDP text into {@code out}.
     * @param len size of {@code out} in bytes
     * @return 0 on success, or an AVERROR if the buffer is too small
     */
    public native int sdpCreate(long ctx, byte[] out, int off, int len);

    /** @return handle to the RIFF video tag table (pass to {@link #codecGetIdFromTag}). */
    public native long getRiffVideoTags();
    /** @return handle to the RIFF audio tag table. */
    public native long getRiffAudioTags();
    /** @return handle to the MOV video tag table. */
    public native long getMovVideoTags();
    /** @return handle to the MOV audio tag table. */
    public native long getMovAudioTags();

    /** Map a fourcc tag to a codec id using a tag-table handle. @return {@code AV_CODEC_ID_*} or {@link #AV_CODEC_ID_NONE}. */
    public native int codecGetIdFromTag(long tagTable, int tag);
    /** Map a codec id to a fourcc tag using a tag-table handle. @return the tag, or 0 if none. */
    public native int codecGetTagFromId(long tagTable, int codecId);

    /* -------------------------------------------------------------- */
    /* libavcodec: iterate, supported-config, best pix fmt, bits, mem  */
    /* -------------------------------------------------------------- */

    /**
     * Iterate over all registered codecs.
     * @param state a 1-element long[]; set state[0]=0 for the first call, and
     *              reuse the same array on subsequent calls (the native side
     *              updates state[0] with the iterator position each time).
     * @return an {@code AVCodec} handle, or 0 at the end of the list.
     */
    public native long codecIterate(long[] state);

    /**
     * Enumerate the supported configurations of a codec/context.
     * @return a Java int[] where [0]={@code config}, [1]={@code count}, and
     *         [2..count+1] are the config values, or null if the config type
     *         is not expressible as int (FRAME_RATE / CHANNEL_LAYOUT) or on error.
     * @see #AV_CODEC_CONFIG_PIX_FORMAT
     */
    public native int[] codecGetSupportedConfigs(long codecCtx, long codec, int config, int flags);

    /**
     * Find the pixel format in {@code pixFmtList} that loses the least quality
     * converting from {@code srcPixFmt}.
     * @param pixFmtList list of candidate {@code AV_PIX_FMT_*} values (all elements used; do not add a terminator)
     * @param lossOut optional single-element int[] to receive the loss bitmask
     * @return the best {@code AV_PIX_FMT_*}, or {@link #AV_PIX_FMT_NONE}
     */
    public native int codecFindBestPixFmtOfList(int[] pixFmtList, int srcPixFmt, int hasAlpha, int[] lossOut);

    /** @return bits per sample for a {@code AV_CODEC_ID_*} (audio), or 0 if unknown. */
    public native int getBitsPerSample(int codecId);

    /**
     * Allocate a buffer of {@code size} bytes + FFmpeg's padding for codec
     * extradata. Zero-filled variant of {@link #fastPaddedMallocz}. Free with
     * {@link #free}.
     */
    public native long fastPaddedMalloc(long size);
    /** Allocate a zero-initialized padded buffer. Free with {@link #free}. */
    public native long fastPaddedMallocz(long size);

    /* -------------------------------------------------------------- */
    /* libavutil: samples_alloc, fifo, opt helpers, dict int, mem      */
    /* -------------------------------------------------------------- */

    /**
     * Allocate a sample buffer and copy the packed bytes back into {@code out}.
     * @return bytes written, or negative AVERROR
     */
    public native int samplesAlloc(int nbChannels, int nbSamples, int sampleFmt, int align,
                                   byte[] out, int off, int len);

    /**
     * Allocate sample buffers plus a data-pointer array.
     * @return handle to the pointer array (free with {@link #samplesFree})
     */
    public native long samplesAllocArray(int nbChannels, int nbSamples, int sampleFmt, int align);

    /** Free a data-pointer array from {@link #samplesAllocArray}. */
    public native void samplesFree(long dataArray);

    /** Allocate a byte-oriented auto-grow FIFO. @return AVFifo handle, 0 on failure. */
    public native long fifoAlloc2(long bytes);
    /** Free the FIFO. */
    public native void fifoFree(long fifo);
    /** @return number of elements (bytes) available for reading. */
    public native long fifoCanRead(long fifo);
    /** @return number of elements (bytes) available for writing. */
    public native long fifoCanWrite(long fifo);
    /** Write up to {@code len} bytes into the FIFO. @return 0 / AVERROR. */
    public native int fifoWrite(long fifo, byte[] in, int off, int len);
    /** Read up to {@code len} bytes from the FIFO (clamped by availability). @return bytes read. */
    public native int fifoRead(long fifo, byte[] out, int off, int len);
    /** Discard {@code n} bytes from the FIFO. */
    public native void fifoDrain(long fifo, long n);

    /* ---- sample-level audio FIFO (libavutil/audio_fifo.h) ------------- */

    /**
     * Allocate an audio FIFO operating at sample level.
     * @param sampleFmt {@code AV_SAMPLE_FMT_*} used throughout
     * @param channels  channel count (fixed for the FIFO lifetime)
     * @param nbSamples initial buffer size in samples
     * @return AVAudioFifo handle, or 0 on failure
     */
    public native long audioFifoAlloc(int sampleFmt, int channels, int nbSamples);

    /** Free the audio FIFO. */
    public native void audioFifoFree(long af);

    /**
     * Write {@code nbSamples} interleaved samples (in/off) into the FIFO.
     * The FIFO auto-grows; the Java buffer must hold
     * {@code nbSamples * channels * bytesPerSample(sampleFmt)} bytes.
     * @return samples written, or negative AVERROR
     */
    public native int audioFifoWrite(long af, int sampleFmt, int channels,
                                     byte[] in, int off, int nbSamples);

    /**
     * Read up to {@code maxSamples} interleaved samples out of the FIFO into
     * {@code out} (clamped by availability). @return samples read, or negative AVERROR
     */
    public native int audioFifoRead(long af, int sampleFmt, int channels,
                                    byte[] out, int off, int maxSamples);

    /** Peek (non-destructive) up to {@code maxSamples} interleaved samples. @return samples peeked. */
    public native int audioFifoPeek(long af, int sampleFmt, int channels,
                                    byte[] out, int off, int maxSamples);

    /** Discard (drain) {@code nbSamples} samples from the FIFO. @return 0 or AVERROR. */
    public native int audioFifoDrain(long af, int nbSamples);

    /** @return number of samples currently stored in the FIFO. */
    public native int audioFifoSize(long af);

    /** @return number of samples of free space currently available. */
    public native int audioFifoSpace(long af);

    /** {@code av_dict_set_int} convenience. @return 0 or AVERROR. */
    public native int dictSetInt(long box, String key, long value, int flags);

    /** @return the next {@code AVOption} of an object, or 0 at end (pass 0 to start). */
    public native long optNext(long obj, long prev);
    /** Find an option by name (and optional unit). @return AVOption handle, or 0. */
    public native long optFind(long obj, String name, String unit, int optFlags, int searchFlags);
    /** @return the option name. */
    public native String optGetName(long opt);
    /** @return the option help text. */
    public native String optGetHelp(long opt);
    /** @return the option unit string (for named constants). */
    public native String optGetUnit(long opt);
    /** @return the {@code AVOptionType} of the option, or -1. */
    public native int optGetType(long opt);
    /** Set a binary option from a {@code byte[]}. @return 0 or AVERROR. */
    public native int optSetBin(long obj, String name, byte[] in, int off, int len, int searchFlags);
    /** Apply a dictionary of options; the dict is consumed. @return 0 or AVERROR. */
    public native int optSetDict(long obj, long optionsBox);
    /** Copy all options from {@code src} to an allocated-but-uninitialized {@code dst}. @return 0 or AVERROR. */
    public native int optCopy(long dst, long src);

    /** @return the interleaved (packed) equivalent of a planar sample format. */
    public native int getPackedSampleFmt(int sampleFmt);
    /** @return the planar equivalent of an interleaved sample format. */
    public native int getPlanarSampleFmt(int sampleFmt);

    /** Allocate {@code size} bytes with av_malloc. Free with {@link #free}. @return handle, or 0. */
    public native long malloc(long size);
    /** Allocate zero-initialized memory. Free with {@link #free}. */
    public native long mallocz(long size);
    /** Free memory previously returned by {@link #malloc} / {@link #mallocz}. */
    public native void free(long ptr);

    /* -------------------------------------------------------------- */
    /* libavfilter: graph_alloc_filter, link channels, sink usage      */
    /* -------------------------------------------------------------- */

    /**
     * Allocate a filter instance in the graph without initializing it.
     * Configure options with {@link #optSetString} etc., then finalize with
     * {@link #filterInitDict}. @return AVFilterContext handle, 0 on failure.
     */
    public native long filterGraphAllocFilter(long graph, long filter, String name);

    /** Initialize a filter instance created by {@link #filterGraphAllocFilter}. @return 0 or AVERROR. */
    public native int filterInitDict(long ctx, long optionsBox);

    /** @return number of channels negotiated on a filter link handle. */
    public native int filterLinkGetChannels(long link);

    /** Pull an audio frame of (at least) {@code nbSamples} samples from the sink. @return 0/EAGAIN/EOF. */
    public native int bufferSinkGetSamples(long sinkCtx, long frame, int nbSamples);

    /** @return width of the buffer sink's output video. */
    public native int bufferSinkGetW(long sinkCtx);
    /** @return height of the buffer sink's output video. */
    public native int bufferSinkGetH(long sinkCtx);
    /** @return sample-aspect-ratio numerator of the sink output. */
    public native int bufferSinkGetSampleAspectNum(long sinkCtx);
    /** @return sample-aspect-ratio denominator of the sink output. */
    public native int bufferSinkGetSampleAspectDen(long sinkCtx);
    /** @return number of channels of the sink output audio. */
    public native int bufferSinkGetChannels(long sinkCtx);
    /** @return sample rate of the sink output audio. */
    public native int bufferSinkGetSampleRate(long sinkCtx);
    /** @return media type ({@code AVMEDIA_TYPE_*}) of the sink output. */
    public native int bufferSinkGetType(long sinkCtx);
    /** @return number of failed intra-reordering requests (push-mode loss). */
    public native int bufferSrcGetNbFailedRequests(long srcCtx);

    /* -------------------------------------------------------------- */
    /* libswscale: getColorspaceDetails, palette conversions          */
    /* -------------------------------------------------------------- */

    /**
     * Read the current colorspace details of a scaler context.
     * @param invOut optional 4-element int[] to receive the input YUV→RGB coefficients
     * @param srcRangeOut optional 1-element int[] to receive the input range flag
     * @param tableOut optional 4-element int[] to receive the output coefficients
     * @param dstRangeOut optional 1-element int[] to receive the output range flag
     * @param brightnessOut / contrastOut / saturationOut optional 1-element int[] outputs
     * @return 0 on success, negative AVERROR
     */
    public native int swsGetColorspaceDetails(long sws,
                                              int[] invOut, int[] srcRangeOut,
                                              int[] tableOut, int[] dstRangeOut,
                                              int[] brightnessOut, int[] contrastOut,
                                              int[] saturationOut);

    /**
     * Convert palette (indexed) pixels to packed 32-bit RGBA.
     * @param src 8-bit palette indices at {@code srcOff}
     * @param palette 768-byte RGB lookup table (3 bytes per index) at {@code palOff}
     * @param out receives {@code numPixels} RGBA pixels at {@code outOff}
     */
    public native void swsConvertPalette8ToPacked32(byte[] src, int srcOff,
                                                    byte[] palette, int palOff,
                                                    byte[] out, int outOff, int numPixels);

    /** Convert palette (indexed) pixels to packed 24-bit RGB. */
    public native void swsConvertPalette8ToPacked24(byte[] src, int srcOff,
                                                    byte[] palette, int palOff,
                                                    byte[] out, int outOff, int numPixels);

    /* -------------------------------------------------------------- */
    /* libswresample: alloc_set_opts2, is_initialized, get_class       */
    /* -------------------------------------------------------------- */

    /**
     * Allocate and configure a resampler in one step (new AVChannelLayout API).
     * @return SwrContext handle, or 0 on failure
     */
    public native long swrAllocSetOpts2(int outFmt, int outRate, long outMask,
                                        int inFmt, int inRate, long inMask);

    /** @return 1 if the resampler context is initialized, 0 otherwise. */
    public native int swrIsInitialized(long ctx);

    /** @return the AVClass handle of libswresample (for option listing). */
    public native long swrGetClass();

    /* -------------------------------------------------------------- */
    /* struct field access: stream metadata, codecpar, crops, time_base*/
    /* -------------------------------------------------------------- */

    /** @return value of stream {@code index}'s metadata key, or null. */
    public native String streamGetMetadata(long ctx, int index, String key);

    /** Set/replace stream {@code index}'s metadata {@code key} = {@code val}. @return 0 or AVERROR. */
    public native int streamSetMetadata(long ctx, int index, String key, String val);

    /** @return {@code video_delay} of stream {@code index}'s codecpar. */
    public native int streamGetParVideoDelay(long ctx, int index);
    /** @return {@code trailing_padding} of stream {@code index}'s codecpar. */
    public native int streamGetParTrailingPadding(long ctx, int index);
    /** @return {@code seek_preroll} of stream {@code index}'s codecpar. */
    public native int streamGetParSeekPreroll(long ctx, int index);
    /** @return {@code initial_padding} of stream {@code index}'s codecpar. */
    public native int streamGetParInitialPadding(long ctx, int index);

    /**
     * @return {@code profile} of stream {@code index}'s codecpar (an
     * {@code AV_PROFILE_*} value such as {@link #AV_PROFILE_DTS_HD_MA}), or
     * {@link #AV_PROFILE_UNKNOWN}. Needed to tell DTS-HD/E-AC3 extension
     * streams apart from their base formats when deciding whether a
     * compressed-bitstream passthrough sink can accept the stream as-is.
     */
    public native int streamGetParProfile(long ctx, int index);

    /**
     * @return the channel-mask bits of stream {@code index}'s codecpar
     * {@code ch_layout}, or 0 when the layout is not mask-ordered (custom or
     * ambisonic orders have no meaningful bitmask). Use
     * {@link #getDefaultChannelLayout} with the channel count as a fallback.
     */
    public native long streamGetParChannelLayout(long ctx, int index);

    /** @return the frame's crop_top field (display cropping). */
    public native int frameGetCropTop(long frame);
    /** @return the frame's crop_bottom field. */
    public native int frameGetCropBottom(long frame);
    /** @return the frame's crop_left field. */
    public native int frameGetCropLeft(long frame);
    /** @return the frame's crop_right field. */
    public native int frameGetCropRight(long frame);
    /** Set all four crop fields of the frame. */
    public native void frameSetCrop(long frame, int top, int bottom, int left, int right);

    /** @return numerator of the packet's {@code time_base}. */
    public native int packetGetTimeBaseNum(long pkt);
    /** @return denominator of the packet's {@code time_base}. */
    public native int packetGetTimeBaseDen(long pkt);
    /** Set the packet's {@code time_base}. */
    public native void packetSetTimeBase(long pkt, int num, int den);


    /* ================================================================== */
    /* Round 5: remaining public-API gap closure (see ffmpeg_jni.c        */
    /*          "Round 5")                                                 */
    /* ================================================================== */

    /* -------------------------------------------------------------- */
    /* libavformat: open input with an explicit AVInputFormat*         */
    /* -------------------------------------------------------------- */

    /**
     * Variant of {@link #formatOpenInput} that honours an explicit
     * {@link AVInputFormat} handle from {@link #findInputFormat}, instead of
     * letting FFmpeg auto-detect the demuxer.
     * @param inputFmt an {@code AVInputFormat} handle ({@link #findInputFormat}), or 0 for auto-detect
     * @return AVFormatContext handle, or 0 on failure
     */
    public native long formatOpenInputFmt(long inputFmt, String url, long optionsBox);

    /* -------------------------------------------------------------- */
    /* libavcodec: subtitle decoding                                    */
    /* -------------------------------------------------------------- */

    /**
     * Allocate a zero-initialised {@code AVSubtitle}. After a successful
     * {@link #subtitleDecode} (gotSubPtr[0]!=0) free it with
     * {@link #subtitleFree}. @return AVSubtitle handle, or 0.
     */
    public native long subtitleAlloc();

    /** Free subtitle data and the object (handle becomes invalid). */
    public native void subtitleFree(long sub);

    /**
     * Decode one subtitle packet into a preallocated {@code AVSubtitle}.
     * @param gotSubPtr a 1-element int[] receiving {@code got_sub_ptr};
     *                  when [0]!=0 the caller must call {@link #subtitleFree}.
     * @return 0 (or the number of bytes consumed) on success, negative AVERROR
     */
    public native int subtitleDecode(long codecCtx, long sub, long pkt, int[] gotSubPtr);

    /** @return number of subtitle rectangles, 0 if none. */
    public native int subtitleGetNumRects(long sub);
    /** @return subtitle pts (in AV_TIME_BASE), or {@link #AV_NOPTS_VALUE}. */
    public native long subtitleGetPts(long sub);
    /** @return 0 for graphics, non-zero for text. */
    public native int subtitleGetFormat(long sub);
    /** @return relative display start time (ms). */
    public native int subtitleGetStartDisplayTime(long sub);
    /** @return relative display end time (ms). */
    public native int subtitleGetEndDisplayTime(long sub);

    /** @return plain-text (wantAss{@code ==0}) or ASS/SSA ({@code wantAss!=0}) text of a rectangle, or null. */
    public native String subtitleRectGetText(long sub, int rectIdx, int wantAss);

    /** @return top-left x of subtitle rectangle {@code rectIdx}. */
    public native int subtitleRectGetX(long sub, int rectIdx);
    /** @return top-left y of subtitle rectangle {@code rectIdx}. */
    public native int subtitleRectGetY(long sub, int rectIdx);
    /** @return width of subtitle rectangle {@code rectIdx}. */
    public native int subtitleRectGetW(long sub, int rectIdx);
    /** @return height of subtitle rectangle {@code rectIdx}. */
    public native int subtitleRectGetH(long sub, int rectIdx);
    /** @return {@code SUBTITLE_*} type of rectangle {@code rectIdx}, or -1. */
    public native int subtitleRectGetType(long sub, int rectIdx);

    /* -------------------------------------------------------------- */
    /* libass: application-driven subtitle rendering                  */
    /*                                                                */
    /* The built-in "subtitles"/"ass" filters re-open the media file   */
    /* with their OWN demuxer at graph-config time and read the whole  */
    /* input once. For an app that already owns the source through    */
    /* formatOpenInput that is duplicate IO (a second full pass, a    */
    /* second connection) and it ignores your interrupt_callback.     */
    /* These wrappers drive libass directly with your own demuxer:    */
    /* feed events, pull fonts out of the container, render.          */
    /*                                                                */
    /* Compiled into libffmpeg_jni.so ONLY when FFmpeg was configured */
    /* with --enable-libass; otherwise the first call throws          */
    /* {@link UnsatisfiedLinkError}.                                  */
    /* -------------------------------------------------------------- */

    /**
     * Typical setup (MKV with embedded ASS + attached fonts):
     * <pre>
     * long lib = assLibCreate();
     * long rend = assRendererCreate(lib);
     * long track = assTrackCreate(lib);
     * // MKV AttachedFile fonts are AVMEDIA_TYPE_ATTACHMENT streams (4):
     * for (int i = 0; i &lt; getNbStreams(fmt); i++) {
     *     if (streamGetCodecType(fmt, i) != AVMEDIA_TYPE_ATTACHMENT)
     *         continue;
     *     String mime = streamGetMetadata(fmt, i, "mimetype");
     *     if (mime == null || !(mime.contains("font") || mime.contains("truetype")))
     *         continue;   // leaves out cover art / lyrics attachments
     *     String fname = streamGetMetadata(fmt, i, "filename");
     *     int size = streamGetParExtradataSize(fmt, i);
     *     byte[] font = new byte[size];
     *     streamGetExtradata(fmt, i, font, 0, size);
     *     assAddFont(lib, fname, font, 0, size);
     * }
     * assSetFonts(rend, null, "sans-serif", ASS_FONT_SELECT_ENABLE);
     * assSetFrameSize(rend, viewWidth, viewHeight);
     * // Font diagnostics (opt-in; default is already 4):
     * //   adb logcat -s "MediaHub/Libass"
     * //   I MediaHub/Libass: fontselect: (Family, 700, 0) -&gt; path, faceIndex, psname
     * //   I MediaHub/Libass: Glyph 0x7EED not found, selecting one more font for (...)
     * //   E MediaHub/Libass: Glyph 0x7EED not found in font for (...)
     * assSetLogLevel(4);   // MSGL_INFO
     *
     * // subtitle stream: allocCodecContext + codecParametersToContext +
     * // codecOpen2, then per decoded event:
     * byte[] hdr = new byte[codecGetSubtitleHeaderSize(subCtx)];  // SSA/ASS style header
     * if (hdr.length &gt; 0) {
     *     codecGetSubtitleHeader(subCtx, hdr, 0, hdr.length);
     *     assProcessCodecPrivate(track, hdr, 0, hdr.length);
     * }
     * // ASS packets: feed the raw line via packetCopyData + assProcessData.
     * // SRT/VTT: assProcessChunk(rectTextBytes,
     * //   (subtitleGetPts(s)*1000 + startDisplayTime)/AV_TIME_BASE?, dur...).
     *
     * long frameNowMs = ...;                 // video clock
     * int[] dc = new int[1];
     * for (long img = assRenderFrame(rend, track, frameNowMs, dc); img != 0;
     *          img = assImageGetNext(img)) {
     *     int w = assImageGetWidth(img), h = assImageGetHeight(img),
     *         stride = assImageGetStride(img);
     *     byte[] bits = new byte[stride * h];
     *     assImageGetBitmap(img, bits, 0, bits.length);            // 8-bit alpha
     *     blendOntoCanvas(assImageGetTop(img), assImageGetLeft(img),
     *                     w, h, bits, assImageGetColor(img), assImageGetType(img));
     * }
     * // on seek: assTrackFlushEvents(track);
     * // teardown: assTrackFree(track); assRendererFree(rend); assLibFree(lib);
     * </pre>
     */
    /** @return ASS_Library handle, or 0 on failure. */
    public native long assLibCreate();
    /** Free an ASS_Library (renderers/tracks created from it first). */
    public native void assLibFree(long lib);

    /**
     * Set the threshold for libass messages forwarded to logcat under the
     * {@code MediaHub/Libass} tag. Values are libass {@code MSGL_*} levels:
     * 0 fatal, 1 error, 2/3 warning, 4/5 info, 6 verbose, 7 debug.
     *
     * <p>The default is 4 (info) and should normally stay there: the
     * {@code fontselect: (family, bold, italic) -> file, faceIndex, psname}
     * success line and {@code Glyph 0x... not found, selecting one more font}
     * are both {@code MSGL_INFO}. Filtering at warning level would hide the
     * font decision this channel exists to expose. Level 6 additionally logs
     * every subtitle event and is only useful for short debugging sessions.
     *
     * <p>This does not affect {@link #logSetLevel}, which controls the
     * separate {@code FFmpeg} tag. Gate on {@link #CAP_LIBASS_FONT_LOG}.
     */
    public native void assSetLogLevel(int level);

    /** @return the current {@code MediaHub/Libass} threshold (default 4). */
    public native int assGetLogLevel();

    /** @return ASS_Renderer handle bound to {@code lib}, or 0. */
    public native long assRendererCreate(long lib);
    public native void assRendererFree(long rend);

    /** @return ASS_Track handle (one per subtitle stream) for {@code lib}, or 0. */
    public native long assTrackCreate(long lib);
    public native void assTrackFree(long track);
    /** Drop queued/visible events (call after a seek). */
    public native void assTrackFlushEvents(long track);
    /** @return script PlayResX (0 if unset). */
    public native int assTrackGetPlayResX(long track);
    /** @return script PlayResY (0 if unset). */
    public native int assTrackGetPlayResY(long track);

    /**
     * Register an in-memory font, e.g. an MKV AttachedFile extracted with
     * {@link #streamGetExtradata}. {@code name} should be the stored attachment
     * filename (keep the extension) so the script's style family resolves;
     * the bytes are copied.
     * @return 0 on success, negative AVERROR on bad args
     */
    public native int assAddFont(long lib, String name, byte[] in, int off, int len);
    /** Clear the in-memory fonts registered with {@code lib}. */
    public native void assClearFonts(long lib);
    /** Extra directory scanned by the selected font provider. */
    public native void assSetFontsDir(long lib, String dir);
    /**
     * Enable font matching after the embedded fonts are registered.
     * @param provider {@code ASS_FONT_SELECT_*}
     * @return 0 on success, negative AVERROR
     */
    public native int assSetFonts(long rend, String defaultFont, String defaultFamily, int provider);

    /** Set the target frame (view) size in pixels. */
    public native void assSetFrameSize(long rend, int w, int h);
    /** Set the "storage" size the script was authored against (zoom base). */
    public native void assSetStorageSize(long rend, int w, int h);
    /** Video margins (letterbox) in pixels. */
    public native void assSetMargins(long rend, int top, int bottom, int left, int right);
    /** 1 to render subtitles inside {@link #assSetMargins}'s margins. */
    public native void assSetUseMargins(long rend, int use);
    /** Uniform font size factor (1.0 = native). */
    public native void assSetFontScale(long rend, double scale);

    /**
     * Feed the SSA/ASS script header ({@link #codecGetSubtitleHeader}).
     * @return 0 or negative AVERROR
     */
    public native int assProcessCodecPrivate(long track, byte[] in, int off, int len);
    /**
     * Feed raw script text incrementally (MKV ASS packets are complete
     * {@code Dialogue:} lines). @return 0 or negative AVERROR.
     */
    public native int assProcessData(long track, byte[] in, int off, int len);
    /**
     * Feed ONE already-timed event (decoded SRT/WebVTT text). Times in ms
     * (derive from {@link #subtitleGetPts} + start/end display time).
     * @return 0 or negative AVERROR
     */
    public native int assProcessChunk(long track, byte[] in, int off, int len,
                                      long startMs, long durationMs);

    /**
     * Rasterize {@code track} at {@code nowMs}.
     * @param detectChange optional 1-int[] receiving libass ASS_CHANGE_* bits
     * @return first {@code ASS_Image} handle (0 = nothing visible); the list
     *         is owned by the renderer and regenerated by every call.
     */
    public native long assRenderFrame(long rend, long track, long nowMs, int[] detectChange);

    /** @return next image handle in the list, 0 at end. */
    public native long assImageGetNext(long img);
    public native int assImageGetWidth(long img);
    public native int assImageGetHeight(long img);
    /** @return row pitch of the alpha bitmap. */
    public native int assImageGetStride(long img);
    /** @return foreground color as unsigned packed ARGB. */
    public native long assImageGetColor(long img);
    /** @return image layer type: 0 character, 1 outline, 2 shadow. */
    public native int assImageGetType(long img);
    /** @return image top / left position in the frame, in pixels. */
    public native int assImageGetTop(long img);
    public native int assImageGetLeft(long img);
    /**
     * Copy the alpha bitmap ({@code stride*height} bytes) into {@code out}.
     * @return bytes copied, or negative AVERROR when {@code out} is too small
     */
    public native int assImageGetBitmap(long img, byte[] out, int off, int len);

    /** @return size of AVCodecContext::subtitle_header (SSA/ASS script format
     *  header), 0 when the decoder has none. */
    public native int codecGetSubtitleHeaderSize(long codecCtx);
    /**
     * Copy up to {@code len} bytes of {@code subtitle_header} into {@code out}.
     * @return bytes copied, or -1 when there is no subtitle_header.
     */
    public native int codecGetSubtitleHeader(long codecCtx, byte[] out, int off, int len);

    /* -------------------------------------------------------------- */
    /* libavcodec: descriptor iteration, hw config, class               */
    /* -------------------------------------------------------------- */

    /**
     * Iterate codec descriptors. Pass 0 to start (returns the first
     * descriptor), then pass the previous handle to advance.
     * @return an {@code AVCodecDescriptor} handle, or 0 at the end.
     */
    public native long codecDescriptorNext(long prev);

    /** @return descriptor for a codec id, or 0 if none. */
    public native long codecDescriptorGet(int codecId);
    /** @return codec id of a descriptor handle ({@code AV_CODEC_ID_*}). */
    public native int codecDescriptorGetId(long desc);
    /** @return media type ({@code AVMEDIA_TYPE_*}) of a descriptor. */
    public native int codecDescriptorGetType(long desc);
    /** @return short name (e.g. "h264") of a descriptor. */
    public native String codecDescriptorGetName(long desc);
    /** @return long descriptive name of a descriptor, or null. */
    public native String codecDescriptorGetLongName(long desc);
    /** @return {@code AV_CODEC_PROP_*} flags of a descriptor. */
    public native int codecDescriptorGetProps(long desc);

    /**
     * Query the supported hardware configuration of a codec at {@code index}
     * (index 0,1,2,... until null). @return int[]{pix_fmt, methods,
     * device_type}, or null when index is out of range / unsupported.
     */
    public native int[] codecGetHwConfig(long codec, int index);

    /** @return {@code AVClass} handle of libavcodec (for option introspection). */
    public native long codecGetClass();

    /* -------------------------------------------------------------- */
    /* libavutil: timestamp math (compare_mod / add_stable)            */
    /* -------------------------------------------------------------- */

    /**
     * Compare {@code a} and {@code b} in the modulo domain (e.g. RTP/NTP
     * wrap-around). @return -1, 0, or 1.
     */
    public native long compareMod(long a, long b, long mod);

    /**
     * Add {@code inc} (in {@code incTb}/units) to {@code ts} (in {@code tsTb}
     * units) without precision drift. @return the result in {@code tsTb} units.
     */
    public native long addStable(int tsTbNum, int tsTbDen, long ts,
                                 int incTbNum, int incTbDen, long inc);

    /* -------------------------------------------------------------- */
    /* libavutil: display (rotation) helpers                           */
    /* -------------------------------------------------------------- */

    /**
     * Extract the clockwise rotation angle (degrees) from a 3x3 display
     * matrix given as an {@code int[9]} (row-major).
     */
    public native double displayRotationGet(int[] matrix);

    /** Set the rotation angle in a display matrix {@code int[9]} (in place). */
    public native void displayRotationSet(int[] matrix, double angle);

    /** Flip a display matrix {@code int[9]} horizontally and/or vertically. */
    public native void displayMatrixFlip(int[] matrix, int hflip, int vflip);

    /* -------------------------------------------------------------- */
    /* libavfilter: graph_parse / graph_parse2 (+ AVFilterInOut)       */
    /* -------------------------------------------------------------- */

    /** Allocate an {@code AVFilterInOut} head (free with {@link #filterInOutFree}). */
    public native long filterInOutAlloc();
    /** Free an {@code AVFilterInOut} head and any chained nodes. */
    public native void filterInOutFree(long io);
    /** Set the label name of an {@code AVFilterInOut}. */
    public native void filterInOutSetName(long io, String name);
    /** @return the label name of an {@code AVFilterInOut}, or null. */
    public native String filterInOutGetName(long io);
    /** @return the next node of an {@code AVFilterInOut} chain, or 0. */
    public native long filterInOutGetNext(long io);

    /**
     * Parse a filter chain, linking it into the graph. {@code inputs} and
     * {@code outputs} are {@code AVFilterInOut} handles (or 0) that the graph
     * takes ownership of. @return 0 on success, negative AVERROR.
     */
    public native int filterGraphParse(long graph, String filters, long inputs, long outputs);

    /**
     * Parse a filter chain describing its own {@code [label]}s. Populates
     * {@code inputsOut[0]}/{@code outputsOut[0]} (1-element long[]) with the
     * resulting linked {@code AVFilterInOut} lists; Java must free each head
     * with {@link #filterInOutFree} and walk the chain via
     * {@link #filterInOutGetNext}. @return 0 on success, negative AVERROR.
     */
    public native int filterGraphParse2(long graph, String filters,
                                        long[] inputsOut, long[] outputsOut);

    /* -------------------------------------------------------------- */
    /* libswscale: class / manual allocation                           */
    /* -------------------------------------------------------------- */

    /** @return {@code AVClass} handle of libswscale. */
    public native long swsGetClass();

    /**
     * Allocate an options-enabled {@code SwsContext} (configure with the
     * {@code optSet*} helpers, then call {@link #swsInitContext}). Free with
     * {@link #swsFreeContext}. @return SwsContext handle, or 0.
     */
    public native long swsAllocContext();


    /* ================================================================== */
    /* Constants mirrored from FFmpeg                                      */
    /* ================================================================== */

    /** Undefined timestamp, mirrors FFmpeg's {@code AV_NOPTS_VALUE}. */
    public static final long AV_NOPTS_VALUE = 0x8000000000000000L;

    /** Media-type constants mirroring {@code enum AVMediaType}. */
    public static final int AVMEDIA_TYPE_UNKNOWN   = -1;
    public static final int AVMEDIA_TYPE_VIDEO       = 0;
    public static final int AVMEDIA_TYPE_AUDIO       = 1;
    public static final int AVMEDIA_TYPE_DATA         = 2;
    public static final int AVMEDIA_TYPE_SUBTITLE     = 3;
    public static final int AVMEDIA_TYPE_ATTACHMENT = 4;

    /** Pixel-format constants mirroring {@code enum AVPixelFormat}. */
    public static final int AV_PIX_FMT_NONE  = -1;
    public static final int AV_PIX_FMT_YUV420P = 0;
    public static final int AV_PIX_FMT_YUYV422 = 1;
    public static final int AV_PIX_FMT_RGB24    = 2;
    public static final int AV_PIX_FMT_BGR24    = 3;
    public static final int AV_PIX_FMT_RGBA     = 26;
    public static final int AV_PIX_FMT_BGRA     = 28;
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
     * carry a non-null {@code AVMediaCodecBuffer*} in {@code frame->data[3]},
     * which is what {@link #mediasurfaceGetBuffer} hands back for rendering.
     * Selected by the mediacodec decoder iff {@code avctx->hw_device_ctx} is
     * a {@link #AV_HWDEVICE_TYPE_MEDIACODEC} device whose {@code hwctx->surface}
     * is the App Surface (see {@link #mediasurfaceHwdeviceCreate}).
     */
    public static final int AV_PIX_FMT_MEDIACODEC = 164;

    /**
     * Hardware device type constants mirroring {@code enum AVHWDeviceType}.
     * MediaCodec Surface zero-copy requires {@link #AV_HWDEVICE_TYPE_MEDIACODEC}.
     */
    public static final int AV_HWDEVICE_TYPE_MEDIACODEC = 10;

    /** Sample-format constants mirroring {@code enum AVSampleFormat}. */
    public static final int AV_SAMPLE_FMT_NONE  = -1;
    public static final int AV_SAMPLE_FMT_U8    = 0;
    public static final int AV_SAMPLE_FMT_S16   = 1;
    public static final int AV_SAMPLE_FMT_S32   = 2;
    public static final int AV_SAMPLE_FMT_FLT    = 3;
    public static final int AV_SAMPLE_FMT_DBL    = 4;
    public static final int AV_SAMPLE_FMT_U8P    = 5;
    public static final int AV_SAMPLE_FMT_S16P   = 6;
    public static final int AV_SAMPLE_FMT_S32P   = 7;
    public static final int AV_SAMPLE_FMT_FLTP    = 8;
    public static final int AV_SAMPLE_FMT_DBLP    = 9;

    /**
     * Channel constants mirrored from {@code libavutil/channel_layout.h}.
     * Values match the native enums/macros exactly. The {@code AV_CH_LAYOUT_*}
     * and {@code AV_CH_*} masks are 64-bit bitfields and are declared as
     * {@code long}; the channel indices and orders are {@code int}.
     */

    /** Channel indices, {@code enum AVChannel}. */
    public static final int AV_CHAN_NONE                   = -1;
    public static final int AV_CHAN_FRONT_LEFT             = 0;
    public static final int AV_CHAN_FRONT_RIGHT            = 1;
    public static final int AV_CHAN_FRONT_CENTER           = 2;
    public static final int AV_CHAN_LOW_FREQUENCY          = 3;
    public static final int AV_CHAN_BACK_LEFT              = 4;
    public static final int AV_CHAN_BACK_RIGHT             = 5;
    public static final int AV_CHAN_FRONT_LEFT_OF_CENTER   = 6;
    public static final int AV_CHAN_FRONT_RIGHT_OF_CENTER  = 7;
    public static final int AV_CHAN_BACK_CENTER            = 8;
    public static final int AV_CHAN_SIDE_LEFT              = 9;
    public static final int AV_CHAN_SIDE_RIGHT             = 10;
    public static final int AV_CHAN_TOP_CENTER             = 11;
    public static final int AV_CHAN_TOP_FRONT_LEFT         = 12;
    public static final int AV_CHAN_TOP_FRONT_CENTER       = 13;
    public static final int AV_CHAN_TOP_FRONT_RIGHT        = 14;
    public static final int AV_CHAN_TOP_BACK_LEFT          = 15;
    public static final int AV_CHAN_TOP_BACK_CENTER        = 16;
    public static final int AV_CHAN_TOP_BACK_RIGHT         = 17;
    public static final int AV_CHAN_STEREO_LEFT            = 29;
    public static final int AV_CHAN_STEREO_RIGHT           = 30;
    public static final int AV_CHAN_WIDE_LEFT              = 31;
    public static final int AV_CHAN_WIDE_RIGHT             = 32;
    public static final int AV_CHAN_SURROUND_DIRECT_LEFT   = 33;
    public static final int AV_CHAN_SURROUND_DIRECT_RIGHT  = 34;
    public static final int AV_CHAN_LOW_FREQUENCY_2        = 35;
    public static final int AV_CHAN_TOP_SIDE_LEFT          = 36;
    public static final int AV_CHAN_TOP_SIDE_RIGHT         = 37;
    public static final int AV_CHAN_BOTTOM_FRONT_CENTER    = 38;
    public static final int AV_CHAN_BOTTOM_FRONT_LEFT      = 39;
    public static final int AV_CHAN_BOTTOM_FRONT_RIGHT     = 40;
    public static final int AV_CHAN_SIDE_SURROUND_LEFT     = 41;
    public static final int AV_CHAN_SIDE_SURROUND_RIGHT    = 42;
    public static final int AV_CHAN_TOP_SURROUND_LEFT      = 43;
    public static final int AV_CHAN_TOP_SURROUND_RIGHT     = 44;
    public static final int AV_CHAN_BINAURAL_LEFT          = 61;
    public static final int AV_CHAN_BINAURAL_RIGHT         = 62;
    public static final int AV_CHAN_UNUSED                 = 512;   // 0x200
    public static final int AV_CHAN_UNKNOWN                = 768;   // 0x300
    public static final int AV_CHAN_AMBISONIC_BASE         = 1024;  // 0x400
    public static final int AV_CHAN_AMBISONIC_END          = 2047;  // 0x7ff

    /** Per-channel bit masks, {@code AV_CH_*} = {@code 1ULL << AV_CHAN_*}. 64-bit. */
    public static final long AV_CH_FRONT_LEFT             = 1L;
    public static final long AV_CH_FRONT_RIGHT            = 2L;
    public static final long AV_CH_FRONT_CENTER           = 4L;
    public static final long AV_CH_LOW_FREQUENCY          = 8L;
    public static final long AV_CH_BACK_LEFT              = 16L;
    public static final long AV_CH_BACK_RIGHT             = 32L;
    public static final long AV_CH_FRONT_LEFT_OF_CENTER   = 64L;
    public static final long AV_CH_FRONT_RIGHT_OF_CENTER  = 128L;
    public static final long AV_CH_BACK_CENTER            = 256L;
    public static final long AV_CH_SIDE_LEFT              = 512L;
    public static final long AV_CH_SIDE_RIGHT             = 1024L;
    public static final long AV_CH_TOP_CENTER             = 2048L;
    public static final long AV_CH_TOP_FRONT_LEFT         = 4096L;
    public static final long AV_CH_TOP_FRONT_CENTER       = 8192L;
    public static final long AV_CH_TOP_FRONT_RIGHT        = 16384L;
    public static final long AV_CH_TOP_BACK_LEFT          = 32768L;
    public static final long AV_CH_TOP_BACK_CENTER        = 65536L;
    public static final long AV_CH_TOP_BACK_RIGHT         = 131072L;
    public static final long AV_CH_STEREO_LEFT            = 536870912L;
    public static final long AV_CH_STEREO_RIGHT           = 1073741824L;
    public static final long AV_CH_WIDE_LEFT              = 2147483648L;
    public static final long AV_CH_WIDE_RIGHT             = 4294967296L;
    public static final long AV_CH_SURROUND_DIRECT_LEFT   = 8589934592L;
    public static final long AV_CH_SURROUND_DIRECT_RIGHT  = 17179869184L;
    public static final long AV_CH_LOW_FREQUENCY_2        = 34359738368L;
    public static final long AV_CH_TOP_SIDE_LEFT          = 68719476736L;
    public static final long AV_CH_TOP_SIDE_RIGHT         = 137438953472L;
    public static final long AV_CH_BOTTOM_FRONT_CENTER    = 274877906944L;
    public static final long AV_CH_BOTTOM_FRONT_LEFT      = 549755813888L;
    public static final long AV_CH_BOTTOM_FRONT_RIGHT     = 1099511627776L;
    public static final long AV_CH_SIDE_SURROUND_LEFT     = 2199023255552L;
    public static final long AV_CH_SIDE_SURROUND_RIGHT    = 4398046511104L;
    public static final long AV_CH_TOP_SURROUND_LEFT      = 8796093022208L;
    public static final long AV_CH_TOP_SURROUND_RIGHT     = 17592186044416L;
    public static final long AV_CH_BINAURAL_LEFT          = 2305843009213693952L;
    public static final long AV_CH_BINAURAL_RIGHT         = 4611686018427387904L;

    /** Channel-layout bit masks, {@code AV_CH_LAYOUT_*} from {@code libavutil/channel_layout.h}. 64-bit. */
    public static final long AV_CH_LAYOUT_MONO              = 4L;
    public static final long AV_CH_LAYOUT_STEREO            = 3L;
    public static final long AV_CH_LAYOUT_2POINT1           = 11L;
    public static final long AV_CH_LAYOUT_2_1               = 259L;
    public static final long AV_CH_LAYOUT_SURROUND          = 7L;
    public static final long AV_CH_LAYOUT_3POINT1           = 15L;
    public static final long AV_CH_LAYOUT_4POINT0           = 263L;
    public static final long AV_CH_LAYOUT_4POINT1           = 271L;
    public static final long AV_CH_LAYOUT_2_2               = 1539L;
    public static final long AV_CH_LAYOUT_QUAD              = 51L;
    public static final long AV_CH_LAYOUT_5POINT0           = 1543L;
    public static final long AV_CH_LAYOUT_5POINT1           = 1551L;
    public static final long AV_CH_LAYOUT_5POINT0_BACK      = 55L;
    public static final long AV_CH_LAYOUT_5POINT1_BACK      = 63L;
    public static final long AV_CH_LAYOUT_6POINT0           = 1799L;
    public static final long AV_CH_LAYOUT_6POINT0_FRONT     = 1731L;
    public static final long AV_CH_LAYOUT_HEXAGONAL         = 311L;
    public static final long AV_CH_LAYOUT_3POINT1POINT2     = 20495L;
    public static final long AV_CH_LAYOUT_6POINT1           = 1807L;
    public static final long AV_CH_LAYOUT_6POINT1_BACK      = 319L;
    public static final long AV_CH_LAYOUT_6POINT1_FRONT     = 1739L;
    public static final long AV_CH_LAYOUT_7POINT0           = 1591L;
    public static final long AV_CH_LAYOUT_7POINT0_FRONT     = 1735L;
    public static final long AV_CH_LAYOUT_7POINT1           = 1599L;
    public static final long AV_CH_LAYOUT_7POINT1_WIDE      = 1743L;
    public static final long AV_CH_LAYOUT_7POINT1_WIDE_BACK = 255L;
    public static final long AV_CH_LAYOUT_5POINT1POINT2     = 22031L;
    public static final long AV_CH_LAYOUT_5POINT1POINT2_BACK = 20543L;
    public static final long AV_CH_LAYOUT_OCTAGONAL         = 1847L;
    public static final long AV_CH_LAYOUT_CUBE              = 184371L;
    public static final long AV_CH_LAYOUT_5POINT1POINT4     = 185871L;
    public static final long AV_CH_LAYOUT_5POINT1POINT6     = 196111L;
    public static final long AV_CH_LAYOUT_7POINT1POINT2     = 22079L;
    public static final long AV_CH_LAYOUT_7POINT1POINT4     = 185919L;
    public static final long AV_CH_LAYOUT_7POINT1POINT6     = 196159L;
    public static final long AV_CH_LAYOUT_7POINT2POINT3     = 34359825983L;
    public static final long AV_CH_LAYOUT_9POINT1POINT4     = 186111L;
    public static final long AV_CH_LAYOUT_9POINT1POINT6     = 206344616319L;
    public static final long AV_CH_LAYOUT_HEXADECAGONAL     = 6442710839L;
    public static final long AV_CH_LAYOUT_BINAURAL          = 6917529027641081856L;
    public static final long AV_CH_LAYOUT_STEREO_DOWNMIX    = 1610612736L;
    public static final long AV_CH_LAYOUT_22POINT2          = 2164849779327L;
    /** @deprecated aliases kept for ABI compatibility (FF_API_CHANNEL_LAYOUT_BACK). */
    public static final long AV_CH_LAYOUT_5POINT1POINT4_BACK = 185871L;
    public static final long AV_CH_LAYOUT_7POINT1POINT4_BACK = 185919L;
    public static final long AV_CH_LAYOUT_9POINT1POINT4_BACK = 186111L;
    public static final long AV_CH_LAYOUT_7POINT1_TOP_BACK   = 20543L;

    /** Channel orders, {@code enum AVChannelOrder}. */
    public static final int AV_CHANNEL_ORDER_UNSPEC     = 0;
    public static final int AV_CHANNEL_ORDER_NATIVE     = 1;
    public static final int AV_CHANNEL_ORDER_CUSTOM     = 2;
    public static final int AV_CHANNEL_ORDER_AMBISONIC  = 3;
    public static final int FF_CHANNEL_ORDER_NB         = 4;

    /** Matrix encoding values, {@code enum AVMatrixEncoding}. */
    public static final int AV_MATRIX_ENCODING_NONE           = 0;
    public static final int AV_MATRIX_ENCODING_DOLBY          = 1;
    public static final int AV_MATRIX_ENCODING_DPLII          = 2;
    public static final int AV_MATRIX_ENCODING_DPLIIX         = 3;
    public static final int AV_MATRIX_ENCODING_DPLIIZ         = 4;
    public static final int AV_MATRIX_ENCODING_DOLBYEX        = 5;
    public static final int AV_MATRIX_ENCODING_DOLBYHEADPHONE = 6;
    public static final int AV_MATRIX_ENCODING_NB             = 7;

    /** SWS algorithm flags mirroring {@code libswscale}. */
    public static final int SWS_FAST_BILINEAR = 1;
    public static final int SWS_BILINEAR       = 2;
    public static final int SWS_BICUBIC          = 4;
    public static final int SWS_X                = 8;
    public static final int SWS_POINT            = 0x10;
    public static final int SWS_AREA              = 0x20;
    public static final int SWS_BICUBLIN          = 0x40;
    public static final int SWS_LANCZOS           = 0x80;

    /** Dictionary flags mirroring {@code AV_DICT_*}. */
    public static final int AV_DICT_MATCH_CASE    = 1;
    public static final int AV_DICT_IGNORE_SUFFIX = 2;
    public static final int AV_DICT_DONT_STRDUP    = 4;
    public static final int AV_DICT_DONT_OVERWRITE  = 16;
    public static final int AV_DICT_APPEND          = 32;
    public static final int AV_DICT_MULTIKEY        = 64;

    /** Seek flags mirroring {@code AVSEEK_FLAG_*}. */
    public static final int AVSEEK_FLAG_BACKWARD = 1;
    public static final int AVSEEK_FLAG_BYTE      = 2;
    public static final int AVSEEK_FLAG_ANY        = 4;
    public static final int AVSEEK_FLAG_FRAME     = 8;

    /** AVIO flags mirroring {@code AVIO_FLAG_*}. */
    public static final int AVIO_FLAG_READ      = 1;
    public static final int AVIO_FLAG_WRITE     = 2;
    public static final int AVIO_FLAG_NONBLOCK  = 8;
    public static final int AVIO_FLAG_DIRECT    = 0x8000;

    /** Packet flag bits mirroring {@code AV_PKT_FLAG_*}. */
    public static final int AV_PKT_FLAG_KEY        = 0x0001;
    public static final int AV_PKT_FLAG_CORRUPT    = 0x0002;
    public static final int AV_PKT_FLAG_DISCARD    = 0x0004;
    public static final int AV_PKT_FLAG_TRUSTED    = 0x0008;
    public static final int AV_PKT_FLAG_DISPOSABLE = 0x0010;

    /** Subtitle-rectangle type constants mirroring {@code enum AVSubtitleType}
     *  (use with {@link #subtitleRectGetType}). */
    public static final int SUBTITLE_NONE   = 0;
    public static final int SUBTITLE_BITMAP = 1;
    public static final int SUBTITLE_TEXT   = 2;
    public static final int SUBTITLE_ASS    = 3;

    /** Time base: FFmpeg timestamps are in {@code AV_TIME_BASE}ths of a second (1 000 000). */
    public static final long AV_TIME_BASE = 1_000_000L;

    /**
     * Time base as a rational, identical to
     * {@code AV_TIME_BASE_Q = (AVRational){1, AV_TIME_BASE}} = 1/1 000 000.
     */
    public static final int AV_TIME_BASE_Q_NUM = 1;
    public static final int AV_TIME_BASE_Q_DEN = 1_000_000;

    /** Log levels mirroring {@code AV_LOG_*}. Pass to {@link #logSetLevel}. */
    public static final int AV_LOG_QUIET      = -8;
    public static final int AV_LOG_PANIC       = 0;
    public static final int AV_LOG_FATAL       = 8;
    public static final int AV_LOG_ERROR      = 16;
    public static final int AV_LOG_WARNING    = 24;
    public static final int AV_LOG_INFO        = 32;
    public static final int AV_LOG_VERBOSE   = 40;
    public static final int AV_LOG_DEBUG     = 48;
    public static final int AV_LOG_TRACE     = 56;

    /** Rounding modes mirroring {@code enum AVRounding}, for {@link #rescaleRnd}. */
    public static final int AV_ROUND_ZERO          = 0;
    public static final int AV_ROUND_INF            = 1;
    public static final int AV_ROUND_DOWN          = 2;
    public static final int AV_ROUND_UP              = 3;
    public static final int AV_ROUND_NEAR_INF    = 5;
    public static final int AV_ROUND_PASS_MINMAX = 8192;

    /**
     * Codec id constants, mirrors every entry of {@code enum AVCodecID}
     * from {@code libavcodec/codec_id.h}. Values are kept in sync with
     * the native enum; use these instead of hard-coding magic numbers.
     */
    public static final int AV_CODEC_ID_NONE = 0;
    public static final int AV_CODEC_ID_MPEG1VIDEO = 1;
    public static final int AV_CODEC_ID_MPEG2VIDEO = 2;
    public static final int AV_CODEC_ID_H261 = 3;
    public static final int AV_CODEC_ID_H263 = 4;
    public static final int AV_CODEC_ID_RV10 = 5;
    public static final int AV_CODEC_ID_RV20 = 6;
    public static final int AV_CODEC_ID_MJPEG = 7;
    public static final int AV_CODEC_ID_MJPEGB = 8;
    public static final int AV_CODEC_ID_LJPEG = 9;
    public static final int AV_CODEC_ID_SP5X = 10;
    public static final int AV_CODEC_ID_JPEGLS = 11;
    public static final int AV_CODEC_ID_MPEG4 = 12;
    public static final int AV_CODEC_ID_RAWVIDEO = 13;
    public static final int AV_CODEC_ID_MSMPEG4V1 = 14;
    public static final int AV_CODEC_ID_MSMPEG4V2 = 15;
    public static final int AV_CODEC_ID_MSMPEG4V3 = 16;
    public static final int AV_CODEC_ID_WMV1 = 17;
    public static final int AV_CODEC_ID_WMV2 = 18;
    public static final int AV_CODEC_ID_H263P = 19;
    public static final int AV_CODEC_ID_H263I = 20;
    public static final int AV_CODEC_ID_FLV1 = 21;
    public static final int AV_CODEC_ID_SVQ1 = 22;
    public static final int AV_CODEC_ID_SVQ3 = 23;
    public static final int AV_CODEC_ID_DVVIDEO = 24;
    public static final int AV_CODEC_ID_HUFFYUV = 25;
    public static final int AV_CODEC_ID_CYUV = 26;
    public static final int AV_CODEC_ID_H264 = 27;
    public static final int AV_CODEC_ID_INDEO3 = 28;
    public static final int AV_CODEC_ID_VP3 = 29;
    public static final int AV_CODEC_ID_THEORA = 30;
    public static final int AV_CODEC_ID_ASV1 = 31;
    public static final int AV_CODEC_ID_ASV2 = 32;
    public static final int AV_CODEC_ID_FFV1 = 33;
    public static final int AV_CODEC_ID_4XM = 34;
    public static final int AV_CODEC_ID_VCR1 = 35;
    public static final int AV_CODEC_ID_CLJR = 36;
    public static final int AV_CODEC_ID_MDEC = 37;
    public static final int AV_CODEC_ID_ROQ = 38;
    public static final int AV_CODEC_ID_INTERPLAY_VIDEO = 39;
    public static final int AV_CODEC_ID_XAN_WC3 = 40;
    public static final int AV_CODEC_ID_XAN_WC4 = 41;
    public static final int AV_CODEC_ID_RPZA = 42;
    public static final int AV_CODEC_ID_CINEPAK = 43;
    public static final int AV_CODEC_ID_WS_VQA = 44;
    public static final int AV_CODEC_ID_MSRLE = 45;
    public static final int AV_CODEC_ID_MSVIDEO1 = 46;
    public static final int AV_CODEC_ID_IDCIN = 47;
    public static final int AV_CODEC_ID_8BPS = 48;
    public static final int AV_CODEC_ID_SMC = 49;
    public static final int AV_CODEC_ID_FLIC = 50;
    public static final int AV_CODEC_ID_TRUEMOTION1 = 51;
    public static final int AV_CODEC_ID_VMDVIDEO = 52;
    public static final int AV_CODEC_ID_MSZH = 53;
    public static final int AV_CODEC_ID_ZLIB = 54;
    public static final int AV_CODEC_ID_QTRLE = 55;
    public static final int AV_CODEC_ID_TSCC = 56;
    public static final int AV_CODEC_ID_ULTI = 57;
    public static final int AV_CODEC_ID_QDRAW = 58;
    public static final int AV_CODEC_ID_VIXL = 59;
    public static final int AV_CODEC_ID_QPEG = 60;
    public static final int AV_CODEC_ID_PNG = 61;
    public static final int AV_CODEC_ID_PPM = 62;
    public static final int AV_CODEC_ID_PBM = 63;
    public static final int AV_CODEC_ID_PGM = 64;
    public static final int AV_CODEC_ID_PGMYUV = 65;
    public static final int AV_CODEC_ID_PAM = 66;
    public static final int AV_CODEC_ID_FFVHUFF = 67;
    public static final int AV_CODEC_ID_RV30 = 68;
    public static final int AV_CODEC_ID_RV40 = 69;
    public static final int AV_CODEC_ID_VC1 = 70;
    public static final int AV_CODEC_ID_WMV3 = 71;
    public static final int AV_CODEC_ID_LOCO = 72;
    public static final int AV_CODEC_ID_WNV1 = 73;
    public static final int AV_CODEC_ID_AASC = 74;
    public static final int AV_CODEC_ID_INDEO2 = 75;
    public static final int AV_CODEC_ID_FRAPS = 76;
    public static final int AV_CODEC_ID_TRUEMOTION2 = 77;
    public static final int AV_CODEC_ID_BMP = 78;
    public static final int AV_CODEC_ID_CSCD = 79;
    public static final int AV_CODEC_ID_MMVIDEO = 80;
    public static final int AV_CODEC_ID_ZMBV = 81;
    public static final int AV_CODEC_ID_AVS = 82;
    public static final int AV_CODEC_ID_SMACKVIDEO = 83;
    public static final int AV_CODEC_ID_NUV = 84;
    public static final int AV_CODEC_ID_KMVC = 85;
    public static final int AV_CODEC_ID_FLASHSV = 86;
    public static final int AV_CODEC_ID_CAVS = 87;
    public static final int AV_CODEC_ID_JPEG2000 = 88;
    public static final int AV_CODEC_ID_VMNC = 89;
    public static final int AV_CODEC_ID_VP5 = 90;
    public static final int AV_CODEC_ID_VP6 = 91;
    public static final int AV_CODEC_ID_VP6F = 92;
    public static final int AV_CODEC_ID_TARGA = 93;
    public static final int AV_CODEC_ID_DSICINVIDEO = 94;
    public static final int AV_CODEC_ID_TIERTEXSEQVIDEO = 95;
    public static final int AV_CODEC_ID_TIFF = 96;
    public static final int AV_CODEC_ID_GIF = 97;
    public static final int AV_CODEC_ID_DXA = 98;
    public static final int AV_CODEC_ID_DNXHD = 99;
    public static final int AV_CODEC_ID_THP = 100;
    public static final int AV_CODEC_ID_SGI = 101;
    public static final int AV_CODEC_ID_C93 = 102;
    public static final int AV_CODEC_ID_BETHSOFTVID = 103;
    public static final int AV_CODEC_ID_PTX = 104;
    public static final int AV_CODEC_ID_TXD = 105;
    public static final int AV_CODEC_ID_VP6A = 106;
    public static final int AV_CODEC_ID_AMV = 107;
    public static final int AV_CODEC_ID_VB = 108;
    public static final int AV_CODEC_ID_PCX = 109;
    public static final int AV_CODEC_ID_SUNRAST = 110;
    public static final int AV_CODEC_ID_INDEO4 = 111;
    public static final int AV_CODEC_ID_INDEO5 = 112;
    public static final int AV_CODEC_ID_MIMIC = 113;
    public static final int AV_CODEC_ID_RL2 = 114;
    public static final int AV_CODEC_ID_ESCAPE124 = 115;
    public static final int AV_CODEC_ID_DIRAC = 116;
    public static final int AV_CODEC_ID_BFI = 117;
    public static final int AV_CODEC_ID_CMV = 118;
    public static final int AV_CODEC_ID_MOTIONPIXELS = 119;
    public static final int AV_CODEC_ID_TGV = 120;
    public static final int AV_CODEC_ID_TGQ = 121;
    public static final int AV_CODEC_ID_TQI = 122;
    public static final int AV_CODEC_ID_AURA = 123;
    public static final int AV_CODEC_ID_AURA2 = 124;
    public static final int AV_CODEC_ID_V210X = 125;
    public static final int AV_CODEC_ID_TMV = 126;
    public static final int AV_CODEC_ID_V210 = 127;
    public static final int AV_CODEC_ID_DPX = 128;
    public static final int AV_CODEC_ID_MAD = 129;
    public static final int AV_CODEC_ID_FRWU = 130;
    public static final int AV_CODEC_ID_FLASHSV2 = 131;
    public static final int AV_CODEC_ID_CDGRAPHICS = 132;
    public static final int AV_CODEC_ID_R210 = 133;
    public static final int AV_CODEC_ID_ANM = 134;
    public static final int AV_CODEC_ID_BINKVIDEO = 135;
    public static final int AV_CODEC_ID_IFF_ILBM = 136;
    public static final int AV_CODEC_ID_KGV1 = 137;
    public static final int AV_CODEC_ID_YOP = 138;
    public static final int AV_CODEC_ID_VP8 = 139;
    public static final int AV_CODEC_ID_PICTOR = 140;
    public static final int AV_CODEC_ID_ANSI = 141;
    public static final int AV_CODEC_ID_A64_MULTI = 142;
    public static final int AV_CODEC_ID_A64_MULTI5 = 143;
    public static final int AV_CODEC_ID_R10K = 144;
    public static final int AV_CODEC_ID_MXPEG = 145;
    public static final int AV_CODEC_ID_LAGARITH = 146;
    public static final int AV_CODEC_ID_PRORES = 147;
    public static final int AV_CODEC_ID_JV = 148;
    public static final int AV_CODEC_ID_DFA = 149;
    public static final int AV_CODEC_ID_WMV3IMAGE = 150;
    public static final int AV_CODEC_ID_VC1IMAGE = 151;
    public static final int AV_CODEC_ID_UTVIDEO = 152;
    public static final int AV_CODEC_ID_BMV_VIDEO = 153;
    public static final int AV_CODEC_ID_VBLE = 154;
    public static final int AV_CODEC_ID_DXTORY = 155;
    public static final int AV_CODEC_ID_XWD = 156;
    public static final int AV_CODEC_ID_CDXL = 157;
    public static final int AV_CODEC_ID_XBM = 158;
    public static final int AV_CODEC_ID_ZEROCODEC = 159;
    public static final int AV_CODEC_ID_MSS1 = 160;
    public static final int AV_CODEC_ID_MSA1 = 161;
    public static final int AV_CODEC_ID_TSCC2 = 162;
    public static final int AV_CODEC_ID_MTS2 = 163;
    public static final int AV_CODEC_ID_CLLC = 164;
    public static final int AV_CODEC_ID_MSS2 = 165;
    public static final int AV_CODEC_ID_VP9 = 166;
    public static final int AV_CODEC_ID_AIC = 167;
    public static final int AV_CODEC_ID_ESCAPE130 = 168;
    public static final int AV_CODEC_ID_G2M = 169;
    public static final int AV_CODEC_ID_WEBP = 170;
    public static final int AV_CODEC_ID_HNM4_VIDEO = 171;
    public static final int AV_CODEC_ID_HEVC = 172;
    public static final int AV_CODEC_ID_FIC = 173;
    public static final int AV_CODEC_ID_ALIAS_PIX = 174;
    public static final int AV_CODEC_ID_BRENDER_PIX = 175;
    public static final int AV_CODEC_ID_PAF_VIDEO = 176;
    public static final int AV_CODEC_ID_EXR = 177;
    public static final int AV_CODEC_ID_VP7 = 178;
    public static final int AV_CODEC_ID_SANM = 179;
    public static final int AV_CODEC_ID_SGIRLE = 180;
    public static final int AV_CODEC_ID_MVC1 = 181;
    public static final int AV_CODEC_ID_MVC2 = 182;
    public static final int AV_CODEC_ID_HQX = 183;
    public static final int AV_CODEC_ID_TDSC = 184;
    public static final int AV_CODEC_ID_HQ_HQA = 185;
    public static final int AV_CODEC_ID_HAP = 186;
    public static final int AV_CODEC_ID_DDS = 187;
    public static final int AV_CODEC_ID_DXV = 188;
    public static final int AV_CODEC_ID_SCREENPRESSO = 189;
    public static final int AV_CODEC_ID_RSCC = 190;
    public static final int AV_CODEC_ID_AVS2 = 191;
    public static final int AV_CODEC_ID_PGX = 192;
    public static final int AV_CODEC_ID_AVS3 = 193;
    public static final int AV_CODEC_ID_MSP2 = 194;
    public static final int AV_CODEC_ID_VVC = 195;
    public static final int AV_CODEC_ID_Y41P = 196;
    public static final int AV_CODEC_ID_AVRP = 197;
    public static final int AV_CODEC_ID_012V = 198;
    public static final int AV_CODEC_ID_AVUI = 199;
    public static final int AV_CODEC_ID_TARGA_Y216 = 200;
    public static final int AV_CODEC_ID_YUV4 = 201;
    public static final int AV_CODEC_ID_AVRN = 202;
    public static final int AV_CODEC_ID_CPIA = 203;
    public static final int AV_CODEC_ID_XFACE = 204;
    public static final int AV_CODEC_ID_SNOW = 205;
    public static final int AV_CODEC_ID_SMVJPEG = 206;
    public static final int AV_CODEC_ID_APNG = 207;
    public static final int AV_CODEC_ID_DAALA = 208;
    public static final int AV_CODEC_ID_CFHD = 209;
    public static final int AV_CODEC_ID_TRUEMOTION2RT = 210;
    public static final int AV_CODEC_ID_M101 = 211;
    public static final int AV_CODEC_ID_MAGICYUV = 212;
    public static final int AV_CODEC_ID_SHEERVIDEO = 213;
    public static final int AV_CODEC_ID_YLC = 214;
    public static final int AV_CODEC_ID_PSD = 215;
    public static final int AV_CODEC_ID_PIXLET = 216;
    public static final int AV_CODEC_ID_SPEEDHQ = 217;
    public static final int AV_CODEC_ID_FMVC = 218;
    public static final int AV_CODEC_ID_SCPR = 219;
    public static final int AV_CODEC_ID_CLEARVIDEO = 220;
    public static final int AV_CODEC_ID_XPM = 221;
    public static final int AV_CODEC_ID_AV1 = 222;
    public static final int AV_CODEC_ID_BITPACKED = 223;
    public static final int AV_CODEC_ID_MSCC = 224;
    public static final int AV_CODEC_ID_SRGC = 225;
    public static final int AV_CODEC_ID_SVG = 226;
    public static final int AV_CODEC_ID_GDV = 227;
    public static final int AV_CODEC_ID_FITS = 228;
    public static final int AV_CODEC_ID_IMM4 = 229;
    public static final int AV_CODEC_ID_PROSUMER = 230;
    public static final int AV_CODEC_ID_MWSC = 231;
    public static final int AV_CODEC_ID_WCMV = 232;
    public static final int AV_CODEC_ID_RASC = 233;
    public static final int AV_CODEC_ID_HYMT = 234;
    public static final int AV_CODEC_ID_ARBC = 235;
    public static final int AV_CODEC_ID_AGM = 236;
    public static final int AV_CODEC_ID_LSCR = 237;
    public static final int AV_CODEC_ID_VP4 = 238;
    public static final int AV_CODEC_ID_IMM5 = 239;
    public static final int AV_CODEC_ID_MVDV = 240;
    public static final int AV_CODEC_ID_MVHA = 241;
    public static final int AV_CODEC_ID_CDTOONS = 242;
    public static final int AV_CODEC_ID_MV30 = 243;
    public static final int AV_CODEC_ID_NOTCHLC = 244;
    public static final int AV_CODEC_ID_PFM = 245;
    public static final int AV_CODEC_ID_MOBICLIP = 246;
    public static final int AV_CODEC_ID_PHOTOCD = 247;
    public static final int AV_CODEC_ID_IPU = 248;
    public static final int AV_CODEC_ID_ARGO = 249;
    public static final int AV_CODEC_ID_CRI = 250;
    public static final int AV_CODEC_ID_SIMBIOSIS_IMX = 251;
    public static final int AV_CODEC_ID_SGA_VIDEO = 252;
    public static final int AV_CODEC_ID_GEM = 253;
    public static final int AV_CODEC_ID_VBN = 254;
    public static final int AV_CODEC_ID_JPEGXL = 255;
    public static final int AV_CODEC_ID_QOI = 256;
    public static final int AV_CODEC_ID_PHM = 257;
    public static final int AV_CODEC_ID_RADIANCE_HDR = 258;
    public static final int AV_CODEC_ID_WBMP = 259;
    public static final int AV_CODEC_ID_MEDIA100 = 260;
    public static final int AV_CODEC_ID_VQC = 261;
    public static final int AV_CODEC_ID_PDV = 262;
    public static final int AV_CODEC_ID_EVC = 263;
    public static final int AV_CODEC_ID_RTV1 = 264;
    public static final int AV_CODEC_ID_VMIX = 265;
    public static final int AV_CODEC_ID_LEAD = 266;
    public static final int AV_CODEC_ID_DNXUC = 267;
    public static final int AV_CODEC_ID_RV60 = 268;
    public static final int AV_CODEC_ID_JPEGXL_ANIM = 269;
    public static final int AV_CODEC_ID_APV = 270;
    public static final int AV_CODEC_ID_PRORES_RAW = 271;
    public static final int AV_CODEC_ID_JPEGXS = 272;
    public static final int AV_CODEC_ID_WEBP_ANIM = 273;
    public static final int AV_CODEC_ID_ASTC = 274;
    public static final int AV_CODEC_ID_RW2 = 275;
    public static final int AV_CODEC_ID_IMM6 = 276;
    /* Audio: PCM codecs */
    public static final int AV_CODEC_ID_FIRST_AUDIO = 0x10000; // = 65536
    public static final int AV_CODEC_ID_PCM_S16LE = 0x10000; // = 65536
    public static final int AV_CODEC_ID_PCM_S16BE = 0x10001; // = 65537
    public static final int AV_CODEC_ID_PCM_U16LE = 0x10002; // = 65538
    public static final int AV_CODEC_ID_PCM_U16BE = 0x10003; // = 65539
    public static final int AV_CODEC_ID_PCM_S8 = 0x10004; // = 65540
    public static final int AV_CODEC_ID_PCM_U8 = 0x10005; // = 65541
    public static final int AV_CODEC_ID_PCM_MULAW = 0x10006; // = 65542
    public static final int AV_CODEC_ID_PCM_ALAW = 0x10007; // = 65543
    public static final int AV_CODEC_ID_PCM_S32LE = 0x10008; // = 65544
    public static final int AV_CODEC_ID_PCM_S32BE = 0x10009; // = 65545
    public static final int AV_CODEC_ID_PCM_U32LE = 0x1000A; // = 65546
    public static final int AV_CODEC_ID_PCM_U32BE = 0x1000B; // = 65547
    public static final int AV_CODEC_ID_PCM_S24LE = 0x1000C; // = 65548
    public static final int AV_CODEC_ID_PCM_S24BE = 0x1000D; // = 65549
    public static final int AV_CODEC_ID_PCM_U24LE = 0x1000E; // = 65550
    public static final int AV_CODEC_ID_PCM_U24BE = 0x1000F; // = 65551
    public static final int AV_CODEC_ID_PCM_S24DAUD = 0x10010; // = 65552
    public static final int AV_CODEC_ID_PCM_ZORK = 0x10011; // = 65553
    public static final int AV_CODEC_ID_PCM_S16LE_PLANAR = 0x10012; // = 65554
    public static final int AV_CODEC_ID_PCM_DVD = 0x10013; // = 65555
    public static final int AV_CODEC_ID_PCM_F32BE = 0x10014; // = 65556
    public static final int AV_CODEC_ID_PCM_F32LE = 0x10015; // = 65557
    public static final int AV_CODEC_ID_PCM_F64BE = 0x10016; // = 65558
    public static final int AV_CODEC_ID_PCM_F64LE = 0x10017; // = 65559
    public static final int AV_CODEC_ID_PCM_BLURAY = 0x10018; // = 65560
    public static final int AV_CODEC_ID_PCM_LXF = 0x10019; // = 65561
    public static final int AV_CODEC_ID_S302M = 0x1001A; // = 65562
    public static final int AV_CODEC_ID_PCM_S8_PLANAR = 0x1001B; // = 65563
    public static final int AV_CODEC_ID_PCM_S24LE_PLANAR = 0x1001C; // = 65564
    public static final int AV_CODEC_ID_PCM_S32LE_PLANAR = 0x1001D; // = 65565
    public static final int AV_CODEC_ID_PCM_S16BE_PLANAR = 0x1001E; // = 65566
    public static final int AV_CODEC_ID_PCM_S64LE = 0x1001F; // = 65567
    public static final int AV_CODEC_ID_PCM_S64BE = 0x10020; // = 65568
    public static final int AV_CODEC_ID_PCM_F16LE = 0x10021; // = 65569
    public static final int AV_CODEC_ID_PCM_F24LE = 0x10022; // = 65570
    public static final int AV_CODEC_ID_PCM_VIDC = 0x10023; // = 65571
    public static final int AV_CODEC_ID_PCM_SGA = 0x10024; // = 65572
    public static final int AV_CODEC_ID_PCM_DVDA = 0x10025; // = 65573
    /* Audio: ADPCM codecs */
    public static final int AV_CODEC_ID_ADPCM_IMA_QT = 0x11000; // = 69632
    public static final int AV_CODEC_ID_ADPCM_IMA_WAV = 0x11001; // = 69633
    public static final int AV_CODEC_ID_ADPCM_IMA_DK3 = 0x11002; // = 69634
    public static final int AV_CODEC_ID_ADPCM_IMA_DK4 = 0x11003; // = 69635
    public static final int AV_CODEC_ID_ADPCM_IMA_WS = 0x11004; // = 69636
    public static final int AV_CODEC_ID_ADPCM_IMA_SMJPEG = 0x11005; // = 69637
    public static final int AV_CODEC_ID_ADPCM_MS = 0x11006; // = 69638
    public static final int AV_CODEC_ID_ADPCM_4XM = 0x11007; // = 69639
    public static final int AV_CODEC_ID_ADPCM_XA = 0x11008; // = 69640
    public static final int AV_CODEC_ID_ADPCM_ADX = 0x11009; // = 69641
    public static final int AV_CODEC_ID_ADPCM_EA = 0x1100A; // = 69642
    public static final int AV_CODEC_ID_ADPCM_G726 = 0x1100B; // = 69643
    public static final int AV_CODEC_ID_ADPCM_CT = 0x1100C; // = 69644
    public static final int AV_CODEC_ID_ADPCM_SWF = 0x1100D; // = 69645
    public static final int AV_CODEC_ID_ADPCM_YAMAHA = 0x1100E; // = 69646
    public static final int AV_CODEC_ID_ADPCM_SBPRO_4 = 0x1100F; // = 69647
    public static final int AV_CODEC_ID_ADPCM_SBPRO_3 = 0x11010; // = 69648
    public static final int AV_CODEC_ID_ADPCM_SBPRO_2 = 0x11011; // = 69649
    public static final int AV_CODEC_ID_ADPCM_THP = 0x11012; // = 69650
    public static final int AV_CODEC_ID_ADPCM_IMA_AMV = 0x11013; // = 69651
    public static final int AV_CODEC_ID_ADPCM_EA_R1 = 0x11014; // = 69652
    public static final int AV_CODEC_ID_ADPCM_EA_R3 = 0x11015; // = 69653
    public static final int AV_CODEC_ID_ADPCM_EA_R2 = 0x11016; // = 69654
    public static final int AV_CODEC_ID_ADPCM_IMA_EA_SEAD = 0x11017; // = 69655
    public static final int AV_CODEC_ID_ADPCM_IMA_EA_EACS = 0x11018; // = 69656
    public static final int AV_CODEC_ID_ADPCM_EA_XAS = 0x11019; // = 69657
    public static final int AV_CODEC_ID_ADPCM_EA_MAXIS_XA = 0x1101A; // = 69658
    public static final int AV_CODEC_ID_ADPCM_IMA_ISS = 0x1101B; // = 69659
    public static final int AV_CODEC_ID_ADPCM_G722 = 0x1101C; // = 69660
    public static final int AV_CODEC_ID_ADPCM_IMA_APC = 0x1101D; // = 69661
    public static final int AV_CODEC_ID_ADPCM_VIMA = 0x1101E; // = 69662
    public static final int AV_CODEC_ID_ADPCM_AFC = 0x1101F; // = 69663
    public static final int AV_CODEC_ID_ADPCM_IMA_OKI = 0x11020; // = 69664
    public static final int AV_CODEC_ID_ADPCM_DTK = 0x11021; // = 69665
    public static final int AV_CODEC_ID_ADPCM_IMA_RAD = 0x11022; // = 69666
    public static final int AV_CODEC_ID_ADPCM_G726LE = 0x11023; // = 69667
    public static final int AV_CODEC_ID_ADPCM_THP_LE = 0x11024; // = 69668
    public static final int AV_CODEC_ID_ADPCM_PSX = 0x11025; // = 69669
    public static final int AV_CODEC_ID_ADPCM_AICA = 0x11026; // = 69670
    public static final int AV_CODEC_ID_ADPCM_IMA_DAT4 = 0x11027; // = 69671
    public static final int AV_CODEC_ID_ADPCM_MTAF = 0x11028; // = 69672
    public static final int AV_CODEC_ID_ADPCM_AGM = 0x11029; // = 69673
    public static final int AV_CODEC_ID_ADPCM_ARGO = 0x1102A; // = 69674
    public static final int AV_CODEC_ID_ADPCM_IMA_SSI = 0x1102B; // = 69675
    public static final int AV_CODEC_ID_ADPCM_ZORK = 0x1102C; // = 69676
    public static final int AV_CODEC_ID_ADPCM_IMA_APM = 0x1102D; // = 69677
    public static final int AV_CODEC_ID_ADPCM_IMA_ALP = 0x1102E; // = 69678
    public static final int AV_CODEC_ID_ADPCM_IMA_MTF = 0x1102F; // = 69679
    public static final int AV_CODEC_ID_ADPCM_IMA_CUNNING = 0x11030; // = 69680
    public static final int AV_CODEC_ID_ADPCM_IMA_MOFLEX = 0x11031; // = 69681
    public static final int AV_CODEC_ID_ADPCM_IMA_ACORN = 0x11032; // = 69682
    public static final int AV_CODEC_ID_ADPCM_XMD = 0x11033; // = 69683
    public static final int AV_CODEC_ID_ADPCM_IMA_XBOX = 0x11034; // = 69684
    public static final int AV_CODEC_ID_ADPCM_SANYO = 0x11035; // = 69685
    public static final int AV_CODEC_ID_ADPCM_IMA_HVQM4 = 0x11036; // = 69686
    public static final int AV_CODEC_ID_ADPCM_IMA_PDA = 0x11037; // = 69687
    public static final int AV_CODEC_ID_ADPCM_N64 = 0x11038; // = 69688
    public static final int AV_CODEC_ID_ADPCM_IMA_HVQM2 = 0x11039; // = 69689
    public static final int AV_CODEC_ID_ADPCM_IMA_MAGIX = 0x1103A; // = 69690
    public static final int AV_CODEC_ID_ADPCM_PSXC = 0x1103B; // = 69691
    public static final int AV_CODEC_ID_ADPCM_CIRCUS = 0x1103C; // = 69692
    public static final int AV_CODEC_ID_ADPCM_IMA_ESCAPE = 0x1103D; // = 69693
    public static final int AV_CODEC_ID_ADPCM_RHETOREX = 0x1103E; // = 69694
    public static final int AV_CODEC_ID_ADPCM_IMA_CITRIX = 0x1103F; // = 69695
    /* Audio: AMR */
    public static final int AV_CODEC_ID_AMR_NB = 0x12000; // = 73728
    public static final int AV_CODEC_ID_AMR_WB = 0x12001; // = 73729
    /* Audio: RealAudio */
    public static final int AV_CODEC_ID_RA_144 = 0x13000; // = 77824
    public static final int AV_CODEC_ID_RA_288 = 0x13001; // = 77825
    /* Audio: DPCM codecs */
    public static final int AV_CODEC_ID_ROQ_DPCM = 0x14000; // = 81920
    public static final int AV_CODEC_ID_INTERPLAY_DPCM = 0x14001; // = 81921
    public static final int AV_CODEC_ID_XAN_DPCM = 0x14002; // = 81922
    public static final int AV_CODEC_ID_SOL_DPCM = 0x14003; // = 81923
    public static final int AV_CODEC_ID_SDX2_DPCM = 0x14004; // = 81924
    public static final int AV_CODEC_ID_GREMLIN_DPCM = 0x14005; // = 81925
    public static final int AV_CODEC_ID_DERF_DPCM = 0x14006; // = 81926
    public static final int AV_CODEC_ID_WADY_DPCM = 0x14007; // = 81927
    public static final int AV_CODEC_ID_CBD2_DPCM = 0x14008; // = 81928
    /* Audio codecs */
    public static final int AV_CODEC_ID_MP2 = 0x15000; // = 86016
    public static final int AV_CODEC_ID_MP3 = 0x15001; // = 86017
    public static final int AV_CODEC_ID_AAC = 0x15002; // = 86018
    public static final int AV_CODEC_ID_AC3 = 0x15003; // = 86019
    public static final int AV_CODEC_ID_DTS = 0x15004; // = 86020
    public static final int AV_CODEC_ID_VORBIS = 0x15005; // = 86021
    public static final int AV_CODEC_ID_DVAUDIO = 0x15006; // = 86022
    public static final int AV_CODEC_ID_WMAV1 = 0x15007; // = 86023
    public static final int AV_CODEC_ID_WMAV2 = 0x15008; // = 86024
    public static final int AV_CODEC_ID_MACE3 = 0x15009; // = 86025
    public static final int AV_CODEC_ID_MACE6 = 0x1500A; // = 86026
    public static final int AV_CODEC_ID_VMDAUDIO = 0x1500B; // = 86027
    public static final int AV_CODEC_ID_FLAC = 0x1500C; // = 86028
    public static final int AV_CODEC_ID_MP3ADU = 0x1500D; // = 86029
    public static final int AV_CODEC_ID_MP3ON4 = 0x1500E; // = 86030
    public static final int AV_CODEC_ID_SHORTEN = 0x1500F; // = 86031
    public static final int AV_CODEC_ID_ALAC = 0x15010; // = 86032
    public static final int AV_CODEC_ID_WESTWOOD_SND1 = 0x15011; // = 86033
    public static final int AV_CODEC_ID_GSM = 0x15012; // = 86034
    public static final int AV_CODEC_ID_QDM2 = 0x15013; // = 86035
    public static final int AV_CODEC_ID_COOK = 0x15014; // = 86036
    public static final int AV_CODEC_ID_TRUESPEECH = 0x15015; // = 86037
    public static final int AV_CODEC_ID_TTA = 0x15016; // = 86038
    public static final int AV_CODEC_ID_SMACKAUDIO = 0x15017; // = 86039
    public static final int AV_CODEC_ID_QCELP = 0x15018; // = 86040
    public static final int AV_CODEC_ID_WAVPACK = 0x15019; // = 86041
    public static final int AV_CODEC_ID_DSICINAUDIO = 0x1501A; // = 86042
    public static final int AV_CODEC_ID_IMC = 0x1501B; // = 86043
    public static final int AV_CODEC_ID_MUSEPACK7 = 0x1501C; // = 86044
    public static final int AV_CODEC_ID_MLP = 0x1501D; // = 86045
    public static final int AV_CODEC_ID_GSM_MS = 0x1501E; // = 86046
    public static final int AV_CODEC_ID_ATRAC3 = 0x1501F; // = 86047
    public static final int AV_CODEC_ID_APE = 0x15020; // = 86048
    public static final int AV_CODEC_ID_NELLYMOSER = 0x15021; // = 86049
    public static final int AV_CODEC_ID_MUSEPACK8 = 0x15022; // = 86050
    public static final int AV_CODEC_ID_SPEEX = 0x15023; // = 86051
    public static final int AV_CODEC_ID_WMAVOICE = 0x15024; // = 86052
    public static final int AV_CODEC_ID_WMAPRO = 0x15025; // = 86053
    public static final int AV_CODEC_ID_WMALOSSLESS = 0x15026; // = 86054
    public static final int AV_CODEC_ID_ATRAC3P = 0x15027; // = 86055
    public static final int AV_CODEC_ID_EAC3 = 0x15028; // = 86056
    public static final int AV_CODEC_ID_SIPR = 0x15029; // = 86057
    public static final int AV_CODEC_ID_MP1 = 0x1502A; // = 86058
    public static final int AV_CODEC_ID_TWINVQ = 0x1502B; // = 86059
    public static final int AV_CODEC_ID_TRUEHD = 0x1502C; // = 86060
    public static final int AV_CODEC_ID_MP4ALS = 0x1502D; // = 86061
    public static final int AV_CODEC_ID_ATRAC1 = 0x1502E; // = 86062
    public static final int AV_CODEC_ID_BINKAUDIO_RDFT = 0x1502F; // = 86063
    public static final int AV_CODEC_ID_BINKAUDIO_DCT = 0x15030; // = 86064
    public static final int AV_CODEC_ID_AAC_LATM = 0x15031; // = 86065
    public static final int AV_CODEC_ID_QDMC = 0x15032; // = 86066
    public static final int AV_CODEC_ID_CELT = 0x15033; // = 86067
    public static final int AV_CODEC_ID_G723_1 = 0x15034; // = 86068
    public static final int AV_CODEC_ID_G729 = 0x15035; // = 86069
    public static final int AV_CODEC_ID_8SVX_EXP = 0x15036; // = 86070
    public static final int AV_CODEC_ID_8SVX_FIB = 0x15037; // = 86071
    public static final int AV_CODEC_ID_BMV_AUDIO = 0x15038; // = 86072
    public static final int AV_CODEC_ID_RALF = 0x15039; // = 86073
    public static final int AV_CODEC_ID_IAC = 0x1503A; // = 86074
    public static final int AV_CODEC_ID_ILBC = 0x1503B; // = 86075
    public static final int AV_CODEC_ID_OPUS = 0x1503C; // = 86076
    public static final int AV_CODEC_ID_COMFORT_NOISE = 0x1503D; // = 86077
    public static final int AV_CODEC_ID_TAK = 0x1503E; // = 86078
    public static final int AV_CODEC_ID_METASOUND = 0x1503F; // = 86079
    public static final int AV_CODEC_ID_PAF_AUDIO = 0x15040; // = 86080
    public static final int AV_CODEC_ID_ON2AVC = 0x15041; // = 86081
    public static final int AV_CODEC_ID_DSS_SP = 0x15042; // = 86082
    public static final int AV_CODEC_ID_CODEC2 = 0x15043; // = 86083
    public static final int AV_CODEC_ID_FFWAVESYNTH = 0x15044; // = 86084
    public static final int AV_CODEC_ID_SONIC = 0x15045; // = 86085
    public static final int AV_CODEC_ID_SONIC_LS = 0x15046; // = 86086
    public static final int AV_CODEC_ID_EVRC = 0x15047; // = 86087
    public static final int AV_CODEC_ID_SMV = 0x15048; // = 86088
    public static final int AV_CODEC_ID_DSD_LSBF = 0x15049; // = 86089
    public static final int AV_CODEC_ID_DSD_MSBF = 0x1504A; // = 86090
    public static final int AV_CODEC_ID_DSD_LSBF_PLANAR = 0x1504B; // = 86091
    public static final int AV_CODEC_ID_DSD_MSBF_PLANAR = 0x1504C; // = 86092
    public static final int AV_CODEC_ID_4GV = 0x1504D; // = 86093
    public static final int AV_CODEC_ID_INTERPLAY_ACM = 0x1504E; // = 86094
    public static final int AV_CODEC_ID_XMA1 = 0x1504F; // = 86095
    public static final int AV_CODEC_ID_XMA2 = 0x15050; // = 86096
    public static final int AV_CODEC_ID_DST = 0x15051; // = 86097
    public static final int AV_CODEC_ID_ATRAC3AL = 0x15052; // = 86098
    public static final int AV_CODEC_ID_ATRAC3PAL = 0x15053; // = 86099
    public static final int AV_CODEC_ID_DOLBY_E = 0x15054; // = 86100
    public static final int AV_CODEC_ID_APTX = 0x15055; // = 86101
    public static final int AV_CODEC_ID_APTX_HD = 0x15056; // = 86102
    public static final int AV_CODEC_ID_SBC = 0x15057; // = 86103
    public static final int AV_CODEC_ID_ATRAC9 = 0x15058; // = 86104
    public static final int AV_CODEC_ID_HCOM = 0x15059; // = 86105
    public static final int AV_CODEC_ID_ACELP_KELVIN = 0x1505A; // = 86106
    public static final int AV_CODEC_ID_MPEGH_3D_AUDIO = 0x1505B; // = 86107
    public static final int AV_CODEC_ID_SIREN = 0x1505C; // = 86108
    public static final int AV_CODEC_ID_HCA = 0x1505D; // = 86109
    public static final int AV_CODEC_ID_FASTAUDIO = 0x1505E; // = 86110
    public static final int AV_CODEC_ID_MSNSIREN = 0x1505F; // = 86111
    public static final int AV_CODEC_ID_DFPWM = 0x15060; // = 86112
    public static final int AV_CODEC_ID_BONK = 0x15061; // = 86113
    public static final int AV_CODEC_ID_MISC4 = 0x15062; // = 86114
    public static final int AV_CODEC_ID_APAC = 0x15063; // = 86115
    public static final int AV_CODEC_ID_FTR = 0x15064; // = 86116
    public static final int AV_CODEC_ID_WAVARC = 0x15065; // = 86117
    public static final int AV_CODEC_ID_RKA = 0x15066; // = 86118
    public static final int AV_CODEC_ID_AC4 = 0x15067; // = 86119
    public static final int AV_CODEC_ID_OSQ = 0x15068; // = 86120
    public static final int AV_CODEC_ID_QOA = 0x15069; // = 86121
    public static final int AV_CODEC_ID_LC3 = 0x1506A; // = 86122
    public static final int AV_CODEC_ID_G728 = 0x1506B; // = 86123
    public static final int AV_CODEC_ID_AHX = 0x1506C; // = 86124
    public static final int AV_CODEC_ID_APPLE_APAC = 0x1506D; // = 86125
    /* Subtitle codecs */
    public static final int AV_CODEC_ID_FIRST_SUBTITLE = 0x17000; // = 94208
    public static final int AV_CODEC_ID_DVD_SUBTITLE = 0x17000; // = 94208
    public static final int AV_CODEC_ID_DVB_SUBTITLE = 0x17001; // = 94209
    public static final int AV_CODEC_ID_TEXT = 0x17002; // = 94210
    public static final int AV_CODEC_ID_XSUB = 0x17003; // = 94211
    public static final int AV_CODEC_ID_SSA = 0x17004; // = 94212
    public static final int AV_CODEC_ID_MOV_TEXT = 0x17005; // = 94213
    public static final int AV_CODEC_ID_HDMV_PGS_SUBTITLE = 0x17006; // = 94214
    public static final int AV_CODEC_ID_DVB_TELETEXT = 0x17007; // = 94215
    public static final int AV_CODEC_ID_SRT = 0x17008; // = 94216
    public static final int AV_CODEC_ID_MICRODVD = 0x17009; // = 94217
    public static final int AV_CODEC_ID_EIA_608 = 0x1700A; // = 94218
    public static final int AV_CODEC_ID_JACOSUB = 0x1700B; // = 94219
    public static final int AV_CODEC_ID_SAMI = 0x1700C; // = 94220
    public static final int AV_CODEC_ID_REALTEXT = 0x1700D; // = 94221
    public static final int AV_CODEC_ID_STL = 0x1700E; // = 94222
    public static final int AV_CODEC_ID_SUBVIEWER1 = 0x1700F; // = 94223
    public static final int AV_CODEC_ID_SUBVIEWER = 0x17010; // = 94224
    public static final int AV_CODEC_ID_SUBRIP = 0x17011; // = 94225
    public static final int AV_CODEC_ID_WEBVTT = 0x17012; // = 94226
    public static final int AV_CODEC_ID_MPL2 = 0x17013; // = 94227
    public static final int AV_CODEC_ID_VPLAYER = 0x17014; // = 94228
    public static final int AV_CODEC_ID_PJS = 0x17015; // = 94229
    public static final int AV_CODEC_ID_ASS = 0x17016; // = 94230
    public static final int AV_CODEC_ID_HDMV_TEXT_SUBTITLE = 0x17017; // = 94231
    public static final int AV_CODEC_ID_TTML = 0x17018; // = 94232
    public static final int AV_CODEC_ID_ARIB_CAPTION = 0x17019; // = 94233
    public static final int AV_CODEC_ID_IVTV_VBI = 0x1701A; // = 94234
    /* Other / attachments */
    public static final int AV_CODEC_ID_FIRST_UNKNOWN = 0x18000; // = 98304
    public static final int AV_CODEC_ID_TTF = 0x18000; // = 98304
    public static final int AV_CODEC_ID_SCTE_35 = 0x18001; // = 98305
    public static final int AV_CODEC_ID_EPG = 0x18002; // = 98306
    public static final int AV_CODEC_ID_BINTEXT = 0x18003; // = 98307
    public static final int AV_CODEC_ID_XBIN = 0x18004; // = 98308
    public static final int AV_CODEC_ID_IDF = 0x18005; // = 98309
    public static final int AV_CODEC_ID_OTF = 0x18006; // = 98310
    public static final int AV_CODEC_ID_SMPTE_KLV = 0x18007; // = 98311
    public static final int AV_CODEC_ID_DVD_NAV = 0x18008; // = 98312
    public static final int AV_CODEC_ID_TIMED_ID3 = 0x18009; // = 98313
    public static final int AV_CODEC_ID_BIN_DATA = 0x1800A; // = 98314
    public static final int AV_CODEC_ID_SMPTE_2038 = 0x1800B; // = 98315
    public static final int AV_CODEC_ID_LCEVC = 0x1800C; // = 98316
    public static final int AV_CODEC_ID_SMPTE_436M_ANC = 0x1800D; // = 98317
    public static final int AV_CODEC_ID_ITUT_T35 = 0x1800E; // = 98318
    /* Probe / fake / null codecs */
    public static final int AV_CODEC_ID_PROBE = 0x19000; // = 102400
    public static final int AV_CODEC_ID_MPEG2TS = 0x20000; // = 131072
    public static final int AV_CODEC_ID_MPEG4SYSTEMS = 0x20001; // = 131073
    public static final int AV_CODEC_ID_FFMETADATA = 0x21000; // = 135168
    public static final int AV_CODEC_ID_WRAPPED_AVFRAME = 0x21001; // = 135169
    public static final int AV_CODEC_ID_VNULL = 0x21002; // = 135170
    public static final int AV_CODEC_ID_ANULL = 0x21003; // = 135171

    /** @deprecated alias kept for parity with {@code AV_CODEC_ID_IFF_ILBM}. */
    public static final int AV_CODEC_ID_IFF_BYTERUN1 = AV_CODEC_ID_IFF_ILBM;

    /** @deprecated alias kept for parity with {@code AV_CODEC_ID_HEVC}. */
    public static final int AV_CODEC_ID_H265 = AV_CODEC_ID_HEVC;

    /** @deprecated alias kept for parity with {@code AV_CODEC_ID_VVC}. */
    public static final int AV_CODEC_ID_H266 = AV_CODEC_ID_VVC;

    /**
     * Misc codec constants mirrored from {@code libavcodec/defs.h}: input
     * padding, error-detection flags, compliance levels, every codec profile
     * ({@code AV_PROFILE_*}), level, field order, discard levels and audio
     * service types. Values match the native macros/enums.
     *
     * <p>The {@code AV_PROFILE_*} set is the full list from {@code defs.h};
     * for compressed-bitstream passthrough the relevant ones are the audio
     * profiles (e.g. {@link #AV_PROFILE_DTS}, {@link #AV_PROFILE_EAC3_DDP_ATMOS})
     * and the AAC profiles ({@link #AV_PROFILE_AAC_LOW} for ADTS packing).
     */
    public static final int AV_INPUT_BUFFER_PADDING_SIZE = 64;

    /** Error-detection flags, {@code AV_EF_*}. */
    public static final int AV_EF_CRCCHECK   = 1;
    public static final int AV_EF_BITSTREAM  = 2;
    public static final int AV_EF_BUFFER     = 4;
    public static final int AV_EF_EXPLODE    = 8;
    public static final int AV_EF_IGNORE_ERR = 0x8000;  // = 32768
    public static final int AV_EF_CAREFUL    = 0x10000; // = 65536
    public static final int AV_EF_COMPLIANT  = 0x20000; // = 131072
    public static final int AV_EF_AGGRESSIVE = 0x40000; // = 262144

    /** Compliance levels, {@code FF_COMPLIANCE_*}. */
    public static final int FF_COMPLIANCE_VERY_STRICT = 2;
    public static final int FF_COMPLIANCE_STRICT       = 1;
    public static final int FF_COMPLIANCE_NORMAL       = 0;
    public static final int FF_COMPLIANCE_UNOFFICIAL   = -1;
    public static final int FF_COMPLIANCE_EXPERIMENTAL = -2;

    /** Codec profiles, every {@code AV_PROFILE_*} from {@code libavcodec/defs.h}. */
    public static final int AV_PROFILE_AAC_ELD = 38;
    public static final int AV_PROFILE_AAC_HE = 4;
    public static final int AV_PROFILE_AAC_HE_V2 = 28;
    public static final int AV_PROFILE_AAC_LD = 22;
    public static final int AV_PROFILE_AAC_LOW = 1;
    public static final int AV_PROFILE_AAC_LTP = 3;
    public static final int AV_PROFILE_AAC_MAIN = 0;
    public static final int AV_PROFILE_AAC_SSR = 2;
    public static final int AV_PROFILE_AAC_USAC = 41;
    public static final int AV_PROFILE_APV_400_10 = 99;
    public static final int AV_PROFILE_APV_422_10 = 33;
    public static final int AV_PROFILE_APV_422_12 = 44;
    public static final int AV_PROFILE_APV_444_10 = 55;
    public static final int AV_PROFILE_APV_444_12 = 66;
    public static final int AV_PROFILE_APV_4444_10 = 77;
    public static final int AV_PROFILE_APV_4444_12 = 88;
    public static final int AV_PROFILE_ARIB_PROFILE_A = 0;
    public static final int AV_PROFILE_ARIB_PROFILE_C = 1;
    public static final int AV_PROFILE_ASTC_HDR = 4;
    public static final int AV_PROFILE_ASTC_HDR_RGB_LDR_A = 3;
    public static final int AV_PROFILE_ASTC_LDR = 2;
    public static final int AV_PROFILE_ASTC_LDR_SRGB = 1;
    public static final int AV_PROFILE_ASTC_LINEAR_ANY = 5;
    public static final int AV_PROFILE_AV1_HIGH = 1;
    public static final int AV_PROFILE_AV1_MAIN = 0;
    public static final int AV_PROFILE_AV1_PROFESSIONAL = 2;
    public static final int AV_PROFILE_CAVS_BROADCASTING = 72;
    public static final int AV_PROFILE_CAVS_JIZHUN = 32;
    public static final int AV_PROFILE_DNXHD = 0;
    public static final int AV_PROFILE_DNXHR_444 = 5;
    public static final int AV_PROFILE_DNXHR_HQ = 3;
    public static final int AV_PROFILE_DNXHR_HQX = 4;
    public static final int AV_PROFILE_DNXHR_LB = 1;
    public static final int AV_PROFILE_DNXHR_SQ = 2;
    public static final int AV_PROFILE_DTS = 20;
    public static final int AV_PROFILE_DTS_96_24 = 40;
    public static final int AV_PROFILE_DTS_ES = 30;
    public static final int AV_PROFILE_DTS_EXPRESS = 70;
    public static final int AV_PROFILE_DTS_HD_HRA = 50;
    public static final int AV_PROFILE_DTS_HD_MA = 60;
    public static final int AV_PROFILE_DTS_HD_MA_X = 61;
    public static final int AV_PROFILE_DTS_HD_MA_X_IMAX = 62;
    public static final int AV_PROFILE_EAC3_DDP_ATMOS = 30;
    public static final int AV_PROFILE_EVC_BASELINE = 0;
    public static final int AV_PROFILE_EVC_MAIN = 1;
    public static final int AV_PROFILE_H264_BASELINE = 66;
    public static final int AV_PROFILE_H264_CAVLC_444 = 44;
    public static final int AV_PROFILE_H264_CONSTRAINED = 512;
    public static final int AV_PROFILE_H264_CONSTRAINED_BASELINE = 578;
    public static final int AV_PROFILE_H264_EXTENDED = 88;
    public static final int AV_PROFILE_H264_HIGH = 100;
    public static final int AV_PROFILE_H264_HIGH_10 = 110;
    public static final int AV_PROFILE_H264_HIGH_10_INTRA = 2158;
    public static final int AV_PROFILE_H264_HIGH_422 = 122;
    public static final int AV_PROFILE_H264_HIGH_422_INTRA = 2170;
    public static final int AV_PROFILE_H264_HIGH_444 = 144;
    public static final int AV_PROFILE_H264_HIGH_444_INTRA = 2292;
    public static final int AV_PROFILE_H264_HIGH_444_PREDICTIVE = 244;
    public static final int AV_PROFILE_H264_INTRA = 2048;
    public static final int AV_PROFILE_H264_MAIN = 77;
    public static final int AV_PROFILE_H264_MULTIVIEW_HIGH = 118;
    public static final int AV_PROFILE_H264_STEREO_HIGH = 128;
    public static final int AV_PROFILE_HEVC_MAIN = 1;
    public static final int AV_PROFILE_HEVC_MAIN_10 = 2;
    public static final int AV_PROFILE_HEVC_MAIN_STILL_PICTURE = 3;
    public static final int AV_PROFILE_HEVC_MULTIVIEW_MAIN = 6;
    public static final int AV_PROFILE_HEVC_REXT = 4;
    public static final int AV_PROFILE_HEVC_SCC = 9;
    public static final int AV_PROFILE_JPEG2000_CSTREAM_NO_RESTRICTION = 0x8000; // = 32768
    public static final int AV_PROFILE_JPEG2000_CSTREAM_RESTRICTION_0 = 1;
    public static final int AV_PROFILE_JPEG2000_CSTREAM_RESTRICTION_1 = 2;
    public static final int AV_PROFILE_JPEG2000_DCINEMA_2K = 3;
    public static final int AV_PROFILE_JPEG2000_DCINEMA_4K = 4;
    public static final int AV_PROFILE_KLVA_ASYNC = 1;
    public static final int AV_PROFILE_KLVA_SYNC = 0;
    public static final int AV_PROFILE_MJPEG_HUFFMAN_BASELINE_DCT = 192;
    public static final int AV_PROFILE_MJPEG_HUFFMAN_EXTENDED_SEQUENTIAL_DCT = 193;
    public static final int AV_PROFILE_MJPEG_HUFFMAN_LOSSLESS = 195;
    public static final int AV_PROFILE_MJPEG_HUFFMAN_PROGRESSIVE_DCT = 194;
    public static final int AV_PROFILE_MJPEG_JPEG_LS = 247;
    public static final int AV_PROFILE_MPEG2_422 = 0;
    public static final int AV_PROFILE_MPEG2_AAC_HE = 131;
    public static final int AV_PROFILE_MPEG2_AAC_LOW = 128;
    public static final int AV_PROFILE_MPEG2_HIGH = 1;
    public static final int AV_PROFILE_MPEG2_MAIN = 4;
    public static final int AV_PROFILE_MPEG2_SIMPLE = 5;
    public static final int AV_PROFILE_MPEG2_SNR_SCALABLE = 3;
    public static final int AV_PROFILE_MPEG2_SS = 2;
    public static final int AV_PROFILE_MPEG4_ADVANCED_CODING = 11;
    public static final int AV_PROFILE_MPEG4_ADVANCED_CORE = 12;
    public static final int AV_PROFILE_MPEG4_ADVANCED_REAL_TIME = 9;
    public static final int AV_PROFILE_MPEG4_ADVANCED_SCALABLE_TEXTURE = 13;
    public static final int AV_PROFILE_MPEG4_ADVANCED_SIMPLE = 15;
    public static final int AV_PROFILE_MPEG4_BASIC_ANIMATED_TEXTURE = 7;
    public static final int AV_PROFILE_MPEG4_CORE = 2;
    public static final int AV_PROFILE_MPEG4_CORE_SCALABLE = 10;
    public static final int AV_PROFILE_MPEG4_HYBRID = 8;
    public static final int AV_PROFILE_MPEG4_MAIN = 3;
    public static final int AV_PROFILE_MPEG4_N_BIT = 4;
    public static final int AV_PROFILE_MPEG4_SCALABLE_TEXTURE = 5;
    public static final int AV_PROFILE_MPEG4_SIMPLE = 0;
    public static final int AV_PROFILE_MPEG4_SIMPLE_FACE_ANIMATION = 6;
    public static final int AV_PROFILE_MPEG4_SIMPLE_SCALABLE = 1;
    public static final int AV_PROFILE_MPEG4_SIMPLE_STUDIO = 14;
    public static final int AV_PROFILE_MPEGH_3DA_BL = 3;
    public static final int AV_PROFILE_MPEGH_3DA_HIGH = 1;
    public static final int AV_PROFILE_MPEGH_3DA_LC = 2;
    public static final int AV_PROFILE_MPEGH_3DA_MAIN = 0;
    public static final int AV_PROFILE_PRORES_4444 = 4;
    public static final int AV_PROFILE_PRORES_HQ = 3;
    public static final int AV_PROFILE_PRORES_LT = 1;
    public static final int AV_PROFILE_PRORES_PROXY = 0;
    public static final int AV_PROFILE_PRORES_RAW = 0;
    public static final int AV_PROFILE_PRORES_RAW_HQ = 1;
    public static final int AV_PROFILE_PRORES_STANDARD = 2;
    public static final int AV_PROFILE_PRORES_XQ = 5;
    public static final int AV_PROFILE_RESERVED = -100;
    public static final int AV_PROFILE_SBC_MSBC = 1;
    public static final int AV_PROFILE_TRUEHD_ATMOS = 30;
    public static final int AV_PROFILE_UNKNOWN = -99;
    public static final int AV_PROFILE_VC1_ADVANCED = 3;
    public static final int AV_PROFILE_VC1_COMPLEX = 2;
    public static final int AV_PROFILE_VC1_MAIN = 1;
    public static final int AV_PROFILE_VC1_SIMPLE = 0;
    public static final int AV_PROFILE_VP9_0 = 0;
    public static final int AV_PROFILE_VP9_1 = 1;
    public static final int AV_PROFILE_VP9_2 = 2;
    public static final int AV_PROFILE_VP9_3 = 3;
    public static final int AV_PROFILE_VVC_MAIN_10 = 1;
    public static final int AV_PROFILE_VVC_MAIN_10_444 = 33;

    /** Unknown level sentinel, {@code AV_LEVEL_UNKNOWN}. */
    public static final int AV_LEVEL_UNKNOWN = -99;

    /** Field-order values, {@code enum AVFieldOrder}. */
    public static final int AV_FIELD_UNKNOWN     = 0;
    public static final int AV_FIELD_PROGRESSIVE = 1;
    public static final int AV_FIELD_TT          = 2;
    public static final int AV_FIELD_BB          = 3;
    public static final int AV_FIELD_TB          = 4;
    public static final int AV_FIELD_BT          = 5;

    /** Discard levels, {@code enum AVDiscard}. */
    public static final int AVDISCARD_NONE    = -16;
    public static final int AVDISCARD_DEFAULT = 0;
    public static final int AVDISCARD_NONREF  = 8;
    public static final int AVDISCARD_BIDIR   = 16;
    public static final int AVDISCARD_NONINTRA = 24;
    public static final int AVDISCARD_NONKEY  = 32;
    public static final int AVDISCARD_ALL     = 48;

    /** Audio service types, {@code enum AVAudioServiceType}. */
    public static final int AV_AUDIO_SERVICE_TYPE_MAIN              = 0;
    public static final int AV_AUDIO_SERVICE_TYPE_EFFECTS           = 1;
    public static final int AV_AUDIO_SERVICE_TYPE_VISUALLY_IMPAIRED = 2;
    public static final int AV_AUDIO_SERVICE_TYPE_HEARING_IMPAIRED  = 3;
    public static final int AV_AUDIO_SERVICE_TYPE_DIALOGUE          = 4;
    public static final int AV_AUDIO_SERVICE_TYPE_COMMENTARY        = 5;
    public static final int AV_AUDIO_SERVICE_TYPE_EMERGENCY         = 6;
    public static final int AV_AUDIO_SERVICE_TYPE_VOICE_OVER        = 7;
    public static final int AV_AUDIO_SERVICE_TYPE_KARAOKE           = 8;
    public static final int AV_AUDIO_SERVICE_TYPE_NB                = 9;

    /**
     * {@code ANativeWindow} buffer formats for
     * {@link #windowRendererSetSize} (mirrors {@code AHardwareBuffer_Format}).
     */
    public static final int WINDOW_FORMAT_RGBA_8888 = 1;
    public static final int WINDOW_FORMAT_RGBX_8888 = 2;
    public static final int WINDOW_FORMAT_RGB_565    = 4;

    /**
     * Font-provider flags for {@link #assSetFonts} (libass
     * {@code ASS_FONT_SELECT_*}).
     */
    public static final int ASS_FONT_SELECT_OFF    = 0;
    public static final int ASS_FONT_SELECT_ENABLE = 1;
    public static final int ASS_FONT_SELECT_INVERT = 2;

    /** Codec-config enum values mirroring {@code enum AVCodecConfig} (use with {@link #codecGetSupportedConfigs}). */
    public static final int AV_CODEC_CONFIG_PIX_FORMAT      = 0;
    public static final int AV_CODEC_CONFIG_FRAME_RATE      = 1;
    public static final int AV_CODEC_CONFIG_SAMPLE_RATE     = 2;
    public static final int AV_CODEC_CONFIG_SAMPLE_FORMAT   = 3;
    public static final int AV_CODEC_CONFIG_CHANNEL_LAYOUT  = 4;
    public static final int AV_CODEC_CONFIG_COLOR_RANGE     = 5;
    public static final int AV_CODEC_CONFIG_COLOR_SPACE     = 6;
    public static final int AV_CODEC_CONFIG_ALPHA_MODE      = 7;

    /** AVOption search flags, mirroring {@code AV_OPT_SEARCH_*}. */
    public static final int AV_OPT_SEARCH_CHILDREN = 1 << 0;   /* 1  */
    public static final int AV_OPT_SEARCH_FAKE_OBJ = 1 << 1;   /* 2  */

    /** AVOption flag bits, mirroring {@code AV_OPT_FLAG_*}. */
    public static final int AV_OPT_FLAG_ENCODING_PARAM  = 1 << 0;   /* 1     */
    public static final int AV_OPT_FLAG_DECODING_PARAM  = 1 << 1;   /* 2     */
    public static final int AV_OPT_FLAG_AUDIO_PARAM     = 1 << 3;   /* 8     */
    public static final int AV_OPT_FLAG_VIDEO_PARAM     = 1 << 4;   /* 16    */
    public static final int AV_OPT_FLAG_SUBTITLE_PARAM  = 1 << 5;   /* 32    */
    public static final int AV_OPT_FLAG_EXPORT          = 1 << 6;   /* 64    */
    public static final int AV_OPT_FLAG_READONLY        = 1 << 7;   /* 128   */
    public static final int AV_OPT_FLAG_BSF_PARAM       = 1 << 8;   /* 256   */
    public static final int AV_OPT_FLAG_RUNTIME_PARAM   = 1 << 15;  /* 32768 */
    public static final int AV_OPT_FLAG_FILTERING_PARAM = 1 << 16;  /* 65536 */
    public static final int AV_OPT_FLAG_DEPRECATED      = 1 << 17;  /* 131072 */
}
