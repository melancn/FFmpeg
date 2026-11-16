package org.ffmpeg.consts;

/** Return values of {@code formatCloseInput} / {@code formatFreeContext}. */
public final class FormatCloseStatus {
    private FormatCloseStatus() {
    }

    /** The native context no longer exists. */
    public static final int FORMAT_CLOSE_OK = 0;
    /** The close could not be proven. */
    public static final int FORMAT_CLOSE_UNCONFIRMED = -1;
    /** The handle is not a format context. */
    public static final int FORMAT_CLOSE_INVALID = -2;
}
