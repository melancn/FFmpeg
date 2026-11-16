package org.ffmpeg.consts;

/** Audio service types, {@code enum AVAudioServiceType}. */
public final class AvAudioServiceType {
    private AvAudioServiceType() {
    }

    public static final int AV_AUDIO_SERVICE_TYPE_MAIN = 0;
    public static final int AV_AUDIO_SERVICE_TYPE_EFFECTS = 1;
    public static final int AV_AUDIO_SERVICE_TYPE_VISUALLY_IMPAIRED = 2;
    public static final int AV_AUDIO_SERVICE_TYPE_HEARING_IMPAIRED = 3;
    public static final int AV_AUDIO_SERVICE_TYPE_DIALOGUE = 4;
    public static final int AV_AUDIO_SERVICE_TYPE_COMMENTARY = 5;
    public static final int AV_AUDIO_SERVICE_TYPE_EMERGENCY = 6;
    public static final int AV_AUDIO_SERVICE_TYPE_VOICE_OVER = 7;
    public static final int AV_AUDIO_SERVICE_TYPE_KARAOKE = 8;
    public static final int AV_AUDIO_SERVICE_TYPE_NB = 9;
}
