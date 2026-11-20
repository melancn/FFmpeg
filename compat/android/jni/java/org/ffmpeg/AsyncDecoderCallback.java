package org.ffmpeg;

/**
 * Callback for {@code FFMpegNative.asyncDecoder*}.
 *
 * <p>All methods are invoked on the adapter's native worker thread, not on
 * the thread that queued the packet. Implementations must not block and must
 * not call back into the same adapter from a callback.
 */
public interface AsyncDecoderCallback {
    /**
     * One decoded frame is ready. The handle is an ordinary {@code AVFrame}
     * handle: the receiver owns it and must release it with
     * {@code FFMpegNative.frameFree}. For Surface output use the usual
     * {@code mediasurfaceGetBuffer} / release sequence before freeing it.
     */
    void onFrame(long frameHandle);

    /** A hard decode failure. {@code error} is a negative AVERROR. */
    void onError(int error, String message);

    /** End of stream was reached and all decoded frames were delivered. */
    void onEof();
}
