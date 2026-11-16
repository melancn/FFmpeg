package org.ffmpeg.consts;

/** Dolby Vision compression values, {@code AV_DOVI_COMPRESSION_*}. */
public final class AvDoviCompression {
    private AvDoviCompression() {
    }

    public static final int AV_DOVI_COMPRESSION_NONE = 0;
    public static final int AV_DOVI_COMPRESSION_LIMITED = 1;
    public static final int AV_DOVI_COMPRESSION_RESERVED = 2;
    public static final int AV_DOVI_COMPRESSION_EXTENDED = 3;
}
