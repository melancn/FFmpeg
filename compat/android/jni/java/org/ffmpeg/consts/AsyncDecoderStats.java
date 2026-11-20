package org.ffmpeg.consts;

/** Schema v1 layout of the {@code asyncDecoderStats} output array. */
public final class AsyncDecoderStats {
    private AsyncDecoderStats() {
    }

    /** Value of the schema-version slot. */
    public static final int ASYNC_DECODER_SCHEMA_VERSION = 1;

    /** Elements required in {@code out}. */
    public static final int ASYNC_DECODER_STATS_SLOTS = 7;

    /** Always {@link #ASYNC_DECODER_SCHEMA_VERSION} for this schema. */
    public static final int ASYNC_DECODER_SCHEMA_VERSION_INDEX = 0;
    /** 0 created, 1 running, 2 stopped. */
    public static final int ASYNC_DECODER_STATE = 1;
    /** Packets accepted into the bounded queue. */
    public static final int ASYNC_DECODER_QUEUED = 2;
    /** Frames published through {@code onFrame}. */
    public static final int ASYNC_DECODER_FRAMES = 3;
    /** Hard failures reported through {@code onError}. */
    public static final int ASYNC_DECODER_ERRORS = 4;
    /** End-of-stream notifications delivered through {@code onEof}. */
    public static final int ASYNC_DECODER_EOFS = 5;
    /** Flush requests handed to the worker. */
    public static final int ASYNC_DECODER_FLUSHES = 6;
}
