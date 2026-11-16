package org.ffmpeg.consts;

/** AVOption search flags, {@code AV_OPT_SEARCH_*}. */
public final class AvOptionSearch {
    private AvOptionSearch() {
    }

    public static final int AV_OPT_SEARCH_CHILDREN = 1 << 0;
    public static final int AV_OPT_SEARCH_FAKE_OBJ = 1 << 1;
}
