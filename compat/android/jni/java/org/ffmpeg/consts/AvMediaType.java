package org.ffmpeg.consts;

/** Mirrors {@code enum AVMediaType} from {@code libavutil/avutil.h}. */
public final class AvMediaType {
    private AvMediaType() {
    }

    public static final int AVMEDIA_TYPE_UNKNOWN = -1;
    public static final int AVMEDIA_TYPE_VIDEO = 0;
    public static final int AVMEDIA_TYPE_AUDIO = 1;
    public static final int AVMEDIA_TYPE_DATA = 2;
    public static final int AVMEDIA_TYPE_SUBTITLE = 3;
    public static final int AVMEDIA_TYPE_ATTACHMENT = 4;
}
