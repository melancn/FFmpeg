package org.ffmpeg.consts;

/** Discard levels, {@code enum AVDiscard}. */
public final class AvDiscard {
    private AvDiscard() {
    }

    public static final int AVDISCARD_NONE = -16;
    public static final int AVDISCARD_DEFAULT = 0;
    public static final int AVDISCARD_NONREF = 8;
    public static final int AVDISCARD_BIDIR = 16;
    public static final int AVDISCARD_NONINTRA = 24;
    public static final int AVDISCARD_NONKEY = 32;
    public static final int AVDISCARD_ALL = 48;
}
