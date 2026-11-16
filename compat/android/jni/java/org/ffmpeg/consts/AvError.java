package org.ffmpeg.consts;

/** FFmpeg error codes mirroring {@code AVERROR_*}. */
public final class AvError {
    private AvError() {
    }

    public static final int AVERROR_BSF_NOT_FOUND = -1179861752;
    public static final int AVERROR_BUG = -558323010;
    public static final int AVERROR_BUFFER_TOO_SMALL = -1397118274;
    public static final int AVERROR_DECODER_NOT_FOUND = -1128613112;
    public static final int AVERROR_DEMUXER_NOT_FOUND = -1296385272;
    public static final int AVERROR_ENCODER_NOT_FOUND = -1129203192;
    public static final int AVERROR_EOF = -541478725;
    public static final int AVERROR_EXIT = -1414092869;
    public static final int AVERROR_EXTERNAL = -542398533;
    public static final int AVERROR_FILTER_NOT_FOUND = -1279870712;
    public static final int AVERROR_INVALIDDATA = -1094995529;
    public static final int AVERROR_MUXER_NOT_FOUND = -1481985528;
    public static final int AVERROR_OPTION_NOT_FOUND = -1414549496;
    public static final int AVERROR_PATCHWELCOME = -1163346256;
    public static final int AVERROR_PROTOCOL_NOT_FOUND = -1330794744;
    public static final int AVERROR_STREAM_NOT_FOUND = -1381258232;
    public static final int AVERROR_BUG2 = -541545794;
    public static final int AVERROR_UNKNOWN = -1313558101;
    public static final int AVERROR_EXPERIMENTAL = -733130664;
    public static final int AVERROR_INPUT_CHANGED = -1668179713;
    public static final int AVERROR_OUTPUT_CHANGED = -1668179714;
    public static final int AVERROR_HTTP_BAD_REQUEST = -808465656;
    public static final int AVERROR_HTTP_UNAUTHORIZED = -825242872;
    public static final int AVERROR_HTTP_FORBIDDEN = -858797304;
    public static final int AVERROR_HTTP_NOT_FOUND = -875574520;
    public static final int AVERROR_HTTP_TOO_MANY_REQUESTS = -959591672;
    public static final int AVERROR_HTTP_OTHER_4XX = -1482175736;
    public static final int AVERROR_HTTP_SERVER_ERROR = -1482175992;

    public static final int AVERROR_E2BIG = -7;
    public static final int AVERROR_ENOENT = -2;
    public static final int AVERROR_EIO = -5;
    public static final int AVERROR_EAGAIN = -11;
    public static final int AVERROR_ENOMEM = -12;
    public static final int AVERROR_EINVAL = -22;
    public static final int AVERROR_ENOSPC = -28;
    public static final int AVERROR_ENOSYS = -38;
    public static final int AVERROR_EOVERFLOW = -75;
}
