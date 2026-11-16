package org.ffmpeg.consts;

/** Pixel-format conversion loss flags, {@code FF_LOSS_*}. */
public final class AvLoss {
    private AvLoss() {
    }

    public static final int FF_LOSS_RESOLUTION = 1;
    public static final int FF_LOSS_DEPTH = 2;
    public static final int FF_LOSS_COLORSPACE = 4;
    public static final int FF_LOSS_ALPHA = 8;
    public static final int FF_LOSS_COLORQUANT = 16;
    public static final int FF_LOSS_CHROMA = 32;
    public static final int FF_LOSS_EXCESS_RESOLUTION = 64;
    public static final int FF_LOSS_EXCESS_DEPTH = 128;
}
