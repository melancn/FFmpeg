package org.ffmpeg.consts;

/** Buffer-source flags, {@code AV_BUFFERSRC_FLAG_*}. */
public final class AvBufferSrcFlag {
    private AvBufferSrcFlag() {
    }

    public static final int AV_BUFFERSRC_FLAG_NO_CHECK_FORMAT = 1;
    public static final int AV_BUFFERSRC_FLAG_PUSH = 4;
    public static final int AV_BUFFERSRC_FLAG_KEEP_REF = 8;
}
