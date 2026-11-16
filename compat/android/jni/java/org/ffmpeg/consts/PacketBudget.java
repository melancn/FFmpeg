package org.ffmpeg.consts;

/** Fixed layout of the {@code packetGetBudgetInfo} output array. */
public final class PacketBudget {
    private PacketBudget() {
    }

    /** Number of elements {@code packetGetBudgetInfo} requires. */
    public static final int PACKET_BUDGET_INFO_SLOTS = 6;

    /** {@code AVPacket.size}. */
    public static final int PACKET_BUDGET_PAYLOAD_BYTES = 0;
    /** Capacity of the referenced payload buffer. */
    public static final int PACKET_BUDGET_PAYLOAD_CAPACITY = 1;
    /** Sum of all side data content lengths, excluding padding. */
    public static final int PACKET_BUDGET_SIDE_DATA_BYTES = 2;
    /** Number of side data entries, of any type. */
    public static final int PACKET_BUDGET_SIDE_DATA_COUNT = 3;
    /** Capacity of the buffer referenced by {@code opaque_ref}. */
    public static final int PACKET_BUDGET_OPAQUE_REF_CAPACITY = 4;
    /** Conservative accounting charge for this packet. */
    public static final int PACKET_BUDGET_CHARGE_BYTES = 5;
}
