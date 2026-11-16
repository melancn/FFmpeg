package org.ffmpeg.consts;

/** Packet flag bits mirroring {@code AV_PKT_FLAG_*}. */
public final class AvPacketFlag {
    private AvPacketFlag() {
    }

    public static final int AV_PKT_FLAG_KEY = 0x0001;
    public static final int AV_PKT_FLAG_CORRUPT = 0x0002;
    public static final int AV_PKT_FLAG_DISCARD = 0x0004;
    public static final int AV_PKT_FLAG_TRUSTED = 0x0008;
    public static final int AV_PKT_FLAG_DISPOSABLE = 0x0010;
}
