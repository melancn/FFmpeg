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
 *
 * <p><b>Constants.</b> The FFmpeg constants mirrored by this bridge (codec
 * ids, pixel formats, channel layouts, error codes, capability bits, ...) live
 * in {@code org.ffmpeg.consts} and its classes, not in this class.
 */
public final class FFMpegNative {

    /**
     * Register a native log callback that forwards FFmpeg logs to Android Logcat.
     * Forwarding honours the level set by {@link #logSetLevel} (default
     * {@code AV_LOG_INFO}); raise it to {@link org.ffmpeg.consts.AvLogLevel#AV_LOG_TRACE} to see the
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

    /**
     * Read the complete audio format of a stream's declared parameters.
     *
     * <p>Reads {@code codecpar} only: no decoder is opened, no packet is
     * read, nothing is resampled and no layout normaliser is called. An
     * UNSPEC stream therefore reports UNSPEC, not a plausible-looking stereo
     * default.
     *
     * <p>Gate on {@link org.ffmpeg.consts.BridgeApi#CAP_AUDIO_STREAM_FORMAT_INFO}. A library that
     * answers 0 there has no such capability: the feature must be refused,
     * never re-implemented from {@link #streamGetParChannelLayout} (whose 0
     * for non-NATIVE layouts cannot be told apart from "no layout").
     *
     * @param formatCtx   format context handle
     * @param streamIndex audio stream to describe
     * @param outInfo     at least {@link org.ffmpeg.consts.AudioFormatInfo#AUDIO_FORMAT_INFO_SLOTS} elements;
     *                    unknown slots are written as
     *                    {@link org.ffmpeg.consts.AudioFormatInfo#AUDIO_FORMAT_UNKNOWN_VALUE}
     * @param outChannelIds at least {@link org.ffmpeg.consts.AudioFormatInfo#AUDIO_FORMAT_MAX_CHANNELS}
     *                    elements; the first
     *                    {@link org.ffmpeg.consts.AudioFormatInfo#AUDIO_INFO_CHANNEL_IDS_COUNT} are part of
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
     * {@link org.ffmpeg.consts.BridgeApi#CAP_AUDIO_FRAME_FORMAT_INFO}.
     *
     * @param frame     decoded frame handle
     * @param outInfo   at least {@link org.ffmpeg.consts.AudioFormatInfo#AUDIO_FORMAT_INFO_SLOTS} elements
     * @param outChannelIds at least {@link org.ffmpeg.consts.AudioFormatInfo#AUDIO_FORMAT_MAX_CHANNELS}
     * @return 0 on success, or a negative {@code AVERROR} as for
     *         {@link #streamGetAudioFormatInfo}; nothing is written on
     *         failure
     */
    public native int frameGetAudioFormatInfo(long frame,
                                              long[] outInfo, int[] outChannelIds);

    /**
     * @return the bridge contract version implemented by the loaded library,
     *         {@code major << 16 | minor}, or 0 if the library predates this
     *         query. Compare with {@link org.ffmpeg.consts.BridgeApi#BRIDGE_API_VERSION}.
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
     * @param out   at least {@link org.ffmpeg.consts.PacketBudget#PACKET_BUDGET_INFO_SLOTS} elements; written
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

    public native int[] packetGetSideDataTypes(long pkt);

    public native String packetSideDataName(int type);

    public native int streamGetSideDataSize(long fmt, int index, int type);

    public native int streamCopySideDataExact(long fmt, int index, int type,
                                              byte[] out, int offset, int capacity);

    public native int[] streamGetSideDataTypes(long fmt, int index);

    public native int streamGetDoviConfig(long fmt, int index, long[] out);

    public native int streamGetMasteringDisplayMetadata(long fmt, int index, long[] out);

    public native int streamGetContentLightMetadata(long fmt, int index, long[] out);

    public native int streamGetDisplayMatrix(long fmt, int index, long[] out);

    public native int frameGetSideDataSize(long frame, int type);

    public native int frameCopySideDataExact(long frame, int type,
                                             byte[] out, int offset, int capacity);

    public native int[] frameGetSideDataTypes(long frame);

    public native String frameSideDataName(int type);

    public native int frameGetMasteringDisplayMetadata(long frame, long[] out);

    public native int frameGetContentLightMetadata(long frame, long[] out);

    public native int frameGetDisplayMatrix(long frame, long[] out);

    /* ================================================================== */
    /* AVDictionary (key/value options)                                    */
    /* ================================================================== */

    /**
     * Allocate an empty options dictionary. The returned handle is an opaque
     * token (type + slot + generation), not a pointer, and must be released
     * with {@link #dictFree}.
     *
     * <p>2.0: the handle carries a generation, so a dictionary that reuses a
     * freed slot can never be reached through the old handle. Every method
     * that takes a dictionary handle holds it and its lock for the duration of
     * the call, including the ones that pass it to FFmpeg as options.
     *
     * @return a dictionary handle (0 on failure)
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

    /**
     * Release the dictionary and its contents.
     *
     * <p>2.0: this revokes the handle; the dictionary is destroyed when the
     * last call using it has returned. Freeing twice, or freeing 0, is a no-op
     * and cannot affect a newer dictionary that reused the slot.
     */
    public native void dictFree(long box);

    /** Parse a "key=val,key2=val2" string into the dictionary. */
    public native int dictParseString(long box, String str, String keyValSep, String pairsSep, int flags);

    /** Serialize the dictionary to a {@code "key=val,key2=val2"} string. */
    public native String dictGetString(long box, String keyValSep, String pairsSep);

    /**
     * Iterate dictionary entries.
     *
     * <p>Pass 0 for the first call: it creates an iteration cursor and returns
     * its handle. Pass that handle back to advance, and so on; 0 means the end
     * of the dictionary.
     *
     * <p>The returned handle is a <em>cursor</em>, not a pointer into the
     * dictionary. The key and value are copied out while the dictionary is
     * locked, so a concurrent {@link #dictSet} cannot free them underneath the
     * reader. Any change to the dictionary retires the cursor: the next call
     * reports the end rather than skipping or repeating entries.
     *
     * <p>Ending an iteration (the call that returns 0) releases the cursor.
     * Use {@link #dictCursorFree} to abandon one early. Cursor handles are
     * limited by the bridge's slot table, so leaking them eventually makes new
     * iterations fail instead of growing without bound.
     *
     * @return the cursor handle, or 0 at end-of-dictionary
     */
    public native long dictIterate(long box, long prevHandle);

    /** @return the key of a dictionary cursor (from {@link #dictIterate}). */
    public native String dictEntryGetKey(long entryHandle);

    /** @return the value of a dictionary cursor (from {@link #dictIterate}). */
    public native String dictEntryGetValue(long entryHandle);

    /**
     * Release an iteration cursor without finishing the iteration.
     *
     * <p>Optional: the call that reports end-of-dictionary releases it, and
     * {@link #dictFree} stops the dictionary it is bound to. Passing 0, or a
     * cursor that is already released, is a no-op.
     */
    public native void dictCursorFree(long entryHandle);

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

    /**
     * Close and free the format context. The handle becomes invalid.
     *
     * <p>This runs the managed close protocol: new operations are refused,
     * blocked I/O is interrupted, and the context is destroyed only after the
     * operations already in flight have returned. It therefore blocks for up
     * to a few seconds; do not call it on a thread that must stay responsive.
     *
     * @return {@link org.ffmpeg.consts.FormatCloseStatus#FORMAT_CLOSE_OK} only when the native context is gone.
     *         {@link org.ffmpeg.consts.FormatCloseStatus#FORMAT_CLOSE_UNCONFIRMED} means it may still exist and
     *         an {@link IllegalStateException} with code
     *         {@code ffmpeg.format.close.unconfirmed} has been thrown: the last
     *         operation will finish the teardown, and the slot is never reused.
     *         {@link org.ffmpeg.consts.FormatCloseStatus#FORMAT_CLOSE_INVALID} means the handle was not a format
     *         context. Closing 0, or closing twice, is an idempotent no-op.
     */
    public native int formatCloseInput(long ctx);

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

    /**
     * Free a format context created for muxing (after writeTrailer).
     *
     * <p>Same protocol and return values as {@link #formatCloseInput}: the
     * destructor is chosen from how the context was created, so calling this on
     * an input context does not free it the output way.
     */
    public native int formatFreeContext(long ctx);

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

    /** @return stream {@code index} duration (in time base units), or {@link org.ffmpeg.consts.AvTimeBase#AV_NOPTS_VALUE}. */
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
     * <p>Gate on {@link org.ffmpeg.consts.BridgeApi#CAP_VIDEO_CODEC_COMPONENT_NAME}. The read does not
     * modify decoder state and may run on any thread, but the context must
     * still be valid: do not call this after {@link #codecFreeContext}, and
     * do not free the context concurrently. Every call returns a new String.
     *
     * @param codecCtx an opened codec context handle
     * @return the actual MediaCodec component name, or {@code null}
     */
    public native String videoCodecComponentName(long codecCtx);

    /**
     * Read a per-context decoder counter snapshot (schema v1).
     *
     * <p>Schema v1 fills {@link org.ffmpeg.consts.VideoDecodeStats#VIDEO_DECODE_STATS_SLOTS} slots, indexed by
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
     * <p>{@link org.ffmpeg.consts.VideoDecodeStats#VIDEO_DECODE_STATS_CURRENT_BITRATE} is the codec context's
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
     * <p>Gate on {@link org.ffmpeg.consts.BridgeApi#CAP_VIDEO_DECODE_STATS}.
     *
     * @param codecCtx codec context handle
     * @param out      destination array, at least
     *                 {@link org.ffmpeg.consts.VideoDecodeStats#VIDEO_DECODE_STATS_SLOTS} elements
     * @param len      number of slots the caller offers; must be at least
     *                 {@link org.ffmpeg.consts.VideoDecodeStats#VIDEO_DECODE_STATS_SLOTS}
     * @return 0 on success, or a negative AVERROR as described above
     */
     public native int videoDecodeStats(long codecCtx, long[] out, int len);

    /**
     * Create an async decoder adapter around an opened decoder context.
     *
     * <p>FFmpeg's {@code *_mediacodec} decoders fetch buffers synchronously
     * inside libavcodec; that cannot be changed from Java. This adapter moves
     * the bridge's send/receive loop onto a native worker thread and delivers
     * each decoded frame through {@link AsyncDecoderCallback}, so Kotlin no
     * longer has to poll {@link #codecReceiveFrame}. Frame handles delivered
     * this way are ordinary {@code AVFrame} handles: release them with
     * {@link #frameFree} and use the usual {@code mediasurface*} protocol for
     * Surface output.
     *
     * <p>The caller keeps ownership of {@code codecCtx} and must not use the
     * synchronous {@link #codecSendPacket}/{@link #codecReceiveFrame} entry
     * points on it while the adapter is running. Free the adapter before
     * {@link #codecFreeContext}.
     *
     * <p>Gate on {@link org.ffmpeg.consts.BridgeApi#CAP_ASYNC_DECODER_ADAPTER}.
     *
     * @param codecCtx an opened decoder context handle
     * @param queueCapacity bounded packet queue depth; {@code < 1} uses 16
     * @return an opaque adapter token, or 0 on failure
     */
    public native long asyncDecoderCreate(long codecCtx, int queueCapacity);

    /** Register the callback; only valid before {@link #asyncDecoderStart}. */
    public native int asyncDecoderSetCallback(long adapter, AsyncDecoderCallback callback);

    /** Start the native worker; a callback must already be registered. */
    public native int asyncDecoderStart(long adapter);

    /**
     * Queue one packet. The packet is copied, so the caller keeps ownership.
     * Pass {@code pkt==0} for end of stream: frames are drained and
     * {@link AsyncDecoderCallback#onEof} is delivered. Returns AVERROR(EAGAIN)
     * when the bounded queue is full.
     */
    public native int asyncDecoderQueuePacket(long adapter, long pkt);

    /** Ask the worker to flush; never blocks on the worker. */
    public native int asyncDecoderFlush(long adapter);

    /** Stop the worker and join it. Idempotent. */
    public native int asyncDecoderStop(long adapter);

    /**
     * Read the {@link org.ffmpeg.consts.AsyncDecoderStats} schema v1 snapshot.
     * Nothing is written when the array is too short.
     */
    public native int asyncDecoderStats(long adapter, long[] out);

    /** Stop the worker, revoke the token and release native state. */
    public native void asyncDecoderFree(long adapter);

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
     * {@link org.ffmpeg.consts.AvPixelFormat#AV_PIX_FMT_MEDIACODEC} never, the decoder's Surface branch is
     * skipped, and frames come back as {@link org.ffmpeg.consts.AvPixelFormat#AV_PIX_FMT_NV12} (23) —
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
     * returns {@link org.ffmpeg.consts.AvPixelFormat#AV_PIX_FMT_MEDIACODEC} and decoded frames carry an
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

    /** @return presentation timestamp, or {@link org.ffmpeg.consts.AvTimeBase#AV_NOPTS_VALUE}. */
    public native long packetGetPts(long pkt);

    /** @return decoding timestamp, or {@link org.ffmpeg.consts.AvTimeBase#AV_NOPTS_VALUE}. */
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
     * {@code AV_PKT_DATA_*} (see {@code org.ffmpeg.consts.AvPacketSideData}).
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

    /** @return presentation timestamp, or {@link org.ffmpeg.consts.AvTimeBase#AV_NOPTS_VALUE}. */
    public native long frameGetPts(long frame);

    /** @return best-effort timestamp, or {@link org.ffmpeg.consts.AvTimeBase#AV_NOPTS_VALUE}. */
    public native long frameGetBestEffortTimestamp(long frame);

    /** @return packet DTS propagated to the frame, or {@link org.ffmpeg.consts.AvTimeBase#AV_NOPTS_VALUE}. */
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
     * @see org.ffmpeg.consts.BridgeApi#CAP_FRAME_LAYOUT_NORMALIZE
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
    /* {@link #frameGetFormat} == {@link org.ffmpeg.consts.AvPixelFormat#AV_PIX_FMT_MEDIACODEC}.       */
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
    /* instead of {@link org.ffmpeg.consts.AvPixelFormat#AV_PIX_FMT_MEDIACODEC}.                       */
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
     *         until it returns {@link org.ffmpeg.consts.OutputSessionState#OUTPUT_SESSION_DETACHED}.
     */
    public native int windowRendererRevoke(long handle);

    /**
     * Attempt to complete teardown without waiting.
     *
     * @return an {@code OUTPUT_SESSION_*} state.
     *         {@link org.ffmpeg.consts.OutputSessionState#OUTPUT_SESSION_DETACHED} means all three of: the session
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
     * <p>Revokes the handle so no new operation can start, then drops the
     * creator reference. The object itself is destroyed by whichever release
     * drops the last reference: this call when nothing is in flight, otherwise
     * the last admitted operation. The slot becomes reusable once that has
     * happened, so create/free cycles are not limited to a fixed table size.
     *
     * <p>Calling Free twice, with 0, or with a handle that was already freed is
     * a no-op.
     *
     * <p>Calling Free without revoking first is supported (it revokes
     * internally), but it gives you no chance to observe
     * {@link org.ffmpeg.consts.OutputSessionState#OUTPUT_SESSION_DETACHED}; prefer the full protocol.
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
    /*   interruptFlagFree(flag);    // safe before or after the close */
    /*                                                                 */
    /* 2.0: a context that polls the flag holds a reference to it, so  */
    /* interruptFlagFree no longer has to wait for every context to be */
    /* closed. The object is freed when the last context releases it.  */
    /* A flag cannot be swapped for a different one once bound.        */
    /* -------------------------------------------------------------- */

    /**
     * Allocate a cleared interrupt flag.
     *
     * @return an opaque flag handle, or 0 on allocation failure. Release with
     *         {@link #interruptFlagFree}.
     */
    public native long interruptFlagCreate();

    /**
     * Install {@code flag} as the context's {@code interrupt_callback}, so
     * blocking operations on it abort with {@code AVERROR_EXIT} once the flag
     * is raised.
     *
     * <p>Binding the same flag again is a no-op. Binding a <em>different</em>
     * flag to a context that already has one is refused with {@code EPERM} and
     * an {@link IllegalStateException} ({@code ffmpeg.handle.interrupt_bound}):
     * libavformat may already hold the old callback data, so the swap cannot be
     * made safe in place. Reopen the context instead.
     *
     * @return 0 on success, or a negative AVERROR
     */
    public native int formatSetInterruptFlag(long formatCtx, long flag);

    /**
     * Raise the flag: in-flight and subsequent blocking I/O on any context
     * holding it fails with {@code AVERROR_EXIT}. Callable from any thread and
     * idempotent. Never blocks, never waits for a context lock: a close that
     * waited here would deadlock with the read it is trying to unblock.
     */
    public native void interruptFlagAbort(long flag);

    /**
     * Release this caller's reference to the flag.
     *
     * <p>2.0: a context that polls the flag holds its own reference, so this
     * may be called before the context is closed. The flag object is freed when
     * the last context releases it, which is what keeps libavformat from
     * calling into freed memory. Passing 0, or freeing twice, is a no-op.
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
     * means the tail is complete and {@link org.ffmpeg.consts.PcmSessionStatus#PCM_DRAINED} is reported with
     * {@code status[0] == 0}.
     *
     * <p>Small blocks are the intended use: the flush is resumable, so
     * {@code outCount} does not have to hold the whole tail, and a large
     * backlog never prevents draining. An unconfigured or already-finished
     * session reports {@link org.ffmpeg.consts.PcmSessionStatus#PCM_DRAINED} without touching the resampler.
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
     * <p>Also the only way to feed input again after {@link org.ffmpeg.consts.PcmSessionStatus#PCM_DRAINED}.
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

    /** Map a fourcc tag to a codec id using a tag-table handle. @return {@code AV_CODEC_ID_*} or {@link org.ffmpeg.consts.AvCodecId#AV_CODEC_ID_NONE}. */
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
     * @see org.ffmpeg.consts.AvCodecConfig#AV_CODEC_CONFIG_PIX_FORMAT
     */
    public native int[] codecGetSupportedConfigs(long codecCtx, long codec, int config, int flags);

    /**
     * Find the pixel format in {@code pixFmtList} that loses the least quality
     * converting from {@code srcPixFmt}.
     * @param pixFmtList list of candidate {@code AV_PIX_FMT_*} values (all elements used; do not add a terminator)
     * @param lossOut optional single-element int[] to receive the loss bitmask
     * @return the best {@code AV_PIX_FMT_*}, or {@link org.ffmpeg.consts.AvPixelFormat#AV_PIX_FMT_NONE}
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
     * {@code AV_PROFILE_*} value such as {@link org.ffmpeg.consts.AvProfile#AV_PROFILE_DTS_HD_MA}), or
     * {@link org.ffmpeg.consts.AvProfile#AV_PROFILE_UNKNOWN}. Needed to tell DTS-HD/E-AC3 extension
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
    /** @return subtitle pts (in AV_TIME_BASE), or {@link org.ffmpeg.consts.AvTimeBase#AV_NOPTS_VALUE}. */
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
     * separate {@code FFmpeg} tag. Gate on {@link org.ffmpeg.consts.BridgeApi#CAP_LIBASS_FONT_LOG}.
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
     * 2.1: same as {@link #assSetFontsDir(long, String)} but reports whether
     * the directory could be handed to libass.
     * @return 0 on success, negative AVERROR for a bad handle or path
     */
    public native int assSetFontsDirChecked(long lib, String dir);

    /**
     * Enable font matching after the embedded fonts are registered.
     * @param provider {@code ASS_FONT_SELECT_*}
     * @return 0 on success, negative AVERROR
     */
    public native int assSetFonts(long rend, String defaultFont, String defaultFamily, int provider);

    /**
     * 2.1: fontconfig-backed variant of {@link #assSetFonts}. {@code configPath}
     * is an absolute path to a fonts.conf file (assets must be copied to
     * filesDir first). Passing it makes libass scan the configured font
     * directories immediately, so missing-glyph fallback can select any
     * installed face instead of only the default family.
     *
     * <p>Gate on {@link org.ffmpeg.consts.BridgeApi#CAP_ASS_FONTCONFIG}.
     * @return 0 on success, negative AVERROR
     */
    public native int assSetFontsWithConfig(long rend, String defaultFont,
                                             String defaultFamily, int provider,
                                             String configPath);

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

}
