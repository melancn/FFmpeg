package org.ffmpeg.consts;

/** Side-data parameter change flags, {@code AV_SIDE_DATA_PARAM_CHANGE_*}. */
public final class AvSideDataParamChange {
    private AvSideDataParamChange() {
    }

    public static final int AV_SIDE_DATA_PARAM_CHANGE_SAMPLE_RATE = 0x0004;
    public static final int AV_SIDE_DATA_PARAM_CHANGE_DIMENSIONS = 0x0008;
}
