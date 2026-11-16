package org.ffmpeg.consts;

/** AVIO flags mirroring {@code AVIO_FLAG_*}. */
public final class AvIoFlag {
    private AvIoFlag() {
    }

    public static final int AVIO_FLAG_READ = 1;
    public static final int AVIO_FLAG_WRITE = 2;
    public static final int AVIO_FLAG_NONBLOCK = 8;
    public static final int AVIO_FLAG_DIRECT = 0x8000;
}
