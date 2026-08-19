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

    /** Register a native log callback that forwards FFmpeg logs to Android Logcat. */
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

    private FFMpegNative() {
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
     * freshly allocated codec context, before opening it.
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
     * @return bytes copied (negative AVERROR on bad arguments)
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
     * Copy a side-data element of {@code type} into {@code out}. @return bytes
     * copied, or -1 if no such side data. Pass type = {@code AV_PKT_DATA_*}
     * (use the integer constants at the end of this class).
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
     * @return number of output samples produced, or negative AVERROR
     */
    public native int swrConvert(long ctx, byte[] out, int outCount,
                                 byte[] in, int inCount);

    /** Free the resampler. */
    public native void swrFree(long ctx);

    /** Convert a whole AVFrame to another AVFrame (allocates dst as needed). */
    public native int swrConvertFrame(long ctx, long outFrame, long inFrame);

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

    /** Channel-layout constants mirroring {@code AV_CH_LAYOUT_*}. */
    public static final long AV_CH_LAYOUT_MONO          = 4L;      /* FRONT_CENTER */
    public static final long AV_CH_LAYOUT_STEREO        = 3L;      /* FL|FR */
    public static final long AV_CH_LAYOUT_2POINT1        = 11L;     /* STEREO|LFE */
    public static final long AV_CH_LAYOUT_SURROUND        = 7L;      /* STEREO|FC */
    public static final long AV_CH_LAYOUT_3POINT1        = 15L;     /* SURROUND|LFE */
    public static final long AV_CH_LAYOUT_2_1             = 259L;    /* STEREO|BACK_CENTER */
    public static final long AV_CH_LAYOUT_4POINT0         = 263L;    /* SURROUND|BACK_CENTER */
    public static final long AV_CH_LAYOUT_4POINT1         = 271L;    /* 4POINT0|LFE */
    public static final long AV_CH_LAYOUT_2_2             = 1539L;   /* STEREO|SL|SR */
    public static final long AV_CH_LAYOUT_QUAD            = 51L;     /* STEREO|BL|BR */
    public static final long AV_CH_LAYOUT_5POINT0         = 1543L;   /* SURROUND|SL|SR */
    public static final long AV_CH_LAYOUT_5POINT1         = 1551L;   /* 5POINT0|LFE */
    public static final long AV_CH_LAYOUT_5POINT0_BACK    = 55L;     /* SURROUND|BL|BR */
    public static final long AV_CH_LAYOUT_5POINT1_BACK    = 63L;     /* 5POINT0_BACK|LFE */
    public static final long AV_CH_LAYOUT_6POINT0         = 1799L;   /* 5POINT0|BACK_CENTER */
    public static final long AV_CH_LAYOUT_6POINT1         = 1807L;   /* 5POINT1|BACK_CENTER */
    public static final long AV_CH_LAYOUT_7POINT0         = 1591L;   /* 5POINT0|BL|BR */
    public static final long AV_CH_LAYOUT_7POINT1         = 1599L;   /* 5POINT1|BL|BR */

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

    /** Codec id constant, mirrors {@code AV_CODEC_ID_NONE}. */
    public static final int AV_CODEC_ID_NONE = 0;

    /** Common video codec id constants mirroring {@code enum AVCodecID}. */
    public static final int AV_CODEC_ID_MPEG1VIDEO = 2;
    public static final int AV_CODEC_ID_MPEG2VIDEO = 3;
    public static final int AV_CODEC_ID_MPEG4     = 13;
    public static final int AV_CODEC_ID_H264       = 28;
    public static final int AV_CODEC_ID_VP8        = 140;
    public static final int AV_CODEC_ID_VP9        = 167;
    public static final int AV_CODEC_ID_HEVC       = 173;
    public static final int AV_CODEC_ID_AV1        = 223;

    /** Common audio codec id constants mirroring {@code enum AVCodecID}. */
    public static final int AV_CODEC_ID_PCM_S16LE = 0x10000;  // = 65536
    public static final int AV_CODEC_ID_AMR_NB     = 0x12000; // = 73728
    public static final int AV_CODEC_ID_AMR_WB     = 73729;
    public static final int AV_CODEC_ID_MP3        = 0x15001; // = 86017
    public static final int AV_CODEC_ID_AAC        = 0x15002; // = 86018
    public static final int AV_CODEC_ID_AC3        = 0x15003; // = 86019
    public static final int AV_CODEC_ID_DTS        = 0x15004; // = 86020
    public static final int AV_CODEC_ID_VORBIS     = 0x15005; // = 86021
    public static final int AV_CODEC_ID_FLAC       = 0x1500c; // = 86028
    public static final int AV_CODEC_ID_ALAC       = 0x15010; // = 86032
    public static final int AV_CODEC_ID_EAC3       = 0x15038; // = 86056
    public static final int AV_CODEC_ID_OPUS       = 0x1504c; // = 86076

    /** @deprecated alias kept for parity with {@code AV_CODEC_ID_H265}. */
    public static final int AV_CODEC_ID_H265 = AV_CODEC_ID_HEVC;

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
