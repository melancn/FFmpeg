package org.ffmpeg.consts;

/** Status codes returned by the bounded PCM resampling session. */
public final class PcmSessionStatus {
    private PcmSessionStatus() {
    }

    /** Output samples were written; the input frame has been consumed. */
    public static final int PCM_OUTPUT_READY = 0;

    /** All input was taken but nothing could be emitted yet. */
    public static final int PCM_INPUT_CONSUMED = 1;

    /** Retained for numeric compatibility only. */
    public static final int PCM_NEED_DRAIN = 2;

    /** The tail is complete. Only {@code pcmSessionDrain} reports this. */
    public static final int PCM_DRAINED = 3;

    /** The frame's format differs from the configured one and a tail remains. */
    public static final int PCM_FORMAT_CHANGE = 4;
}
