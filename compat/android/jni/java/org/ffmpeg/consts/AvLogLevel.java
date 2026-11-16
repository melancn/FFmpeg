package org.ffmpeg.consts;

/** Log levels mirroring {@code AV_LOG_*}. */
public final class AvLogLevel {
    private AvLogLevel() {
    }

    public static final int AV_LOG_QUIET = -8;
    public static final int AV_LOG_PANIC = 0;
    public static final int AV_LOG_FATAL = 8;
    public static final int AV_LOG_ERROR = 16;
    public static final int AV_LOG_WARNING = 24;
    public static final int AV_LOG_INFO = 32;
    public static final int AV_LOG_VERBOSE = 40;
    public static final int AV_LOG_DEBUG = 48;
    public static final int AV_LOG_TRACE = 56;
}
