package org.ffmpeg.consts;

/** Hardware frame mapping flags, {@code AV_HWFRAME_MAP_*}. */
public final class AvHwFrameMap {
    private AvHwFrameMap() {
    }

    public static final int AV_HWFRAME_MAP_READ = 1;
    public static final int AV_HWFRAME_MAP_WRITE = 2;
    public static final int AV_HWFRAME_MAP_OVERWRITE = 4;
    public static final int AV_HWFRAME_MAP_DIRECT = 8;
}
