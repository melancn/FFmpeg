package org.ffmpeg.consts;

/** Seek flags mirroring {@code AVSEEK_FLAG_*}. */
public final class AvSeekFlag {
    private AvSeekFlag() {
    }

    public static final int AVSEEK_FLAG_BACKWARD = 1;
    public static final int AVSEEK_FLAG_BYTE = 2;
    public static final int AVSEEK_FLAG_ANY = 4;
    public static final int AVSEEK_FLAG_FRAME = 8;
}
