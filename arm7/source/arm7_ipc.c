/**
 * @file arm7_ipc.c
 * @brief Publishes bounded ARM7 events into the shared IPC ring buffer.
 */
#include "arm7_internal.h"
#include "../../common/hash_common.h"

/**
 * @brief Masks ARM7 interrupts while preserving the previous IME value.
 */
static u32 arm7_enter_ipc_critical(void) {
    u32 ime = REG_IME;
    REG_IME = 0;
    armCompilerBarrier();
    return ime;
}

/**
 * @brief Restores the IME value saved before a shared IPC update.
 */
static void arm7_leave_ipc_critical(u32 ime) {
    armCompilerBarrier();
    REG_IME = ime;
}

/**
 * @brief Publishes one bounded ARM7 event into the shared ring buffer.
 *
 * The event is committed with an odd/even sequence transition while interrupts
 * are masked, so ARM9 never observes a partially copied payload. `reserve_slots`
 * lets high-rate raw/data paths avoid starving control events.
 */
static bool arm7_push_event_ex(u16 type, u16 flags, const void *data, u32 len,
                              unsigned reserve_slots, bool count_drop) {
    if (!g_ipc || g_ipc->magic != IPC_MAGIC) return false;
    len = ipc_event_data_length(data, len);

    /*
     * Publish the producer index only after the whole event is complete.
     * The previous code advanced event_w first, so ARM9 could see an odd/in-
     * progress head entry, give up for the frame, and let a real-hardware
     * burst fill the ring.  Keeping interrupts masked for this short copy
     * makes the single ARM7 producer non-reentrant and guarantees ARM9 only
     * sees fully published events.
     */
    u32 ime = arm7_enter_ipc_critical();
    u32 w = g_ipc->event_w;
    u32 r = g_ipc->event_r;
    u32 used = w >= r ? (w - r) : (EVENT_RING_COUNT - r + w);
    u32 free_slots = (EVENT_RING_COUNT - 1u) - used;
    if (!free_slots || free_slots <= reserve_slots) {
        if (count_drop && !free_slots) g_ipc->dropped_events++;
        arm7_leave_ipc_critical(ime);
        return false;
    }

    Arm7Event *evt = &g_ipc->events[w];
    u32 seq = evt->seq + 1u;
    if (!(seq & 1u)) seq++;
    evt->seq = seq;
    armCompilerBarrier();

    evt->type = type;
    evt->flags = flags;
    evt->timestamp_us = arm7_timestamp_us();
    evt->event_id = ++g_event_id;
    evt->data_hash = 0;
    evt->length = len;
    if (len && data) {
        if (type == EVENT_HOST_CMD) {
            memcpy(evt->data, data, len);
            evt->data_hash = fnv1a32(evt->data, len);
        } else {
            memcpy(evt->data, data, len);
        }
    }

    armCompilerBarrier();
    evt->seq = seq + 1u;
    armCompilerBarrier();
    g_ipc->event_w = (w + 1u) % EVENT_RING_COUNT;
    arm7_leave_ipc_critical(ime);
    return true;
}

/**
 * @brief Pushes a normal ARM7 event and counts full-ring drops.
 */
bool arm7_push_event(u16 type, u16 flags, const void *data, u32 len) {
    return arm7_push_event_ex(type, flags, data, len, 0, true);
}

/**
 * @brief Pushes a raw 802.11 frame event without consuming control reserves.
 */
bool arm7_push_raw_event(u16 flags, const void *data, u32 len) {
    return arm7_push_event_ex(EVENT_RAW_FRAME, flags, data, len,
                             RAW_EVENT_RESERVE, false);
}

/**
 * @brief Pushes a host command/data event while preserving queue headroom.
 *
 * A false return keeps the fast-reply state conservative so the parent retries
 * packets that ARM9 could not receive yet.
 */
bool arm7_push_host_data_event(u16 flags, const void *data, u32 len) {
    /*
     * Data packets are recoverable: if ARM9 is momentarily behind, do not
     * consume the final queue slots and do not count this as an IPC drop.
     * Returning false keeps the ARM7 fast ACK window from advancing, so the
     * parent resends the same/missing packet once ARM9 has drained events.
     */
    return arm7_push_event_ex(EVENT_HOST_CMD, flags, data, len,
                             HOST_DATA_EVENT_RESERVE, false);
}

/**
 * @brief Sends a bounded text log event to ARM9.
 */
void arm7_log(const char *s) {
    arm7_push_event(EVENT_LOG, 0, s, ipc_text_event_length(s));
}
