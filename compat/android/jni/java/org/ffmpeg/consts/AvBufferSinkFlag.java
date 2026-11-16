package org.ffmpeg.consts;

/** Buffer-sink flags, {@code AV_BUFFERSINK_FLAG_*}. */
public final class AvBufferSinkFlag {
    private AvBufferSinkFlag() {
    }

    public static final int AV_BUFFERSINK_FLAG_PEEK = 1;
    public static final int AV_BUFFERSINK_FLAG_NO_REQUEST = 2;
}
