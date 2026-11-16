package org.ffmpeg.consts;

/** Alpha-mode constants, {@code enum AVAlphaMode}. */
public final class AvAlphaMode {
    private AvAlphaMode() {
    }

    public static final int AVALPHA_MODE_UNSPECIFIED = 0;
    public static final int AVALPHA_MODE_PREMULTIPLIED = 1;
    public static final int AVALPHA_MODE_STRAIGHT = 2;
    public static final int AVALPHA_MODE_NB = 3;
}
