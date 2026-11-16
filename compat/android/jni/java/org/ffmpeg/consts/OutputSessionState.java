package org.ffmpeg.consts;

/** Window-renderer output session states, {@code OUTPUT_SESSION_*}. */
public final class OutputSessionState {
    private OutputSessionState() {
    }

    /** Session accepts work. */
    public static final int OUTPUT_SESSION_ACTIVE = 0;
    /** Revoked: no new work is admitted; admitted work is draining. */
    public static final int OUTPUT_SESSION_REVOKING = 1;
    /** Revoked, no operation is admitted, and the window reference is released. */
    public static final int OUTPUT_SESSION_DETACHED = 2;
    /** Teardown could not be proven; the handle is unusable. */
    public static final int OUTPUT_SESSION_QUARANTINED = 3;
    /** Freed. */
    public static final int OUTPUT_SESSION_CLOSED = 4;
}
