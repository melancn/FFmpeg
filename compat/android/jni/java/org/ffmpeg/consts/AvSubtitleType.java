package org.ffmpeg.consts;

/** Subtitle-rectangle type constants mirroring {@code enum AVSubtitleType}. */
public final class AvSubtitleType {
    private AvSubtitleType() {
    }

    public static final int SUBTITLE_NONE = 0;
    public static final int SUBTITLE_BITMAP = 1;
    public static final int SUBTITLE_TEXT = 2;
    public static final int SUBTITLE_ASS = 3;
}
