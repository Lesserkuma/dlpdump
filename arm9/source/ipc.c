/**
 * @file ipc.c
 * @brief Sends ARM9 commands to ARM7 and consumes the shared event ring.
 */
#include "state.h"
#include "boot.h"
#include "debug.h"
#include "download.h"
#include "file.h"
#include "ipc_arm9.h"
#include "meta.h"
#include "pcap.h"
#include "report.h"
#include "scan.h"
#include "system.h"
#include "ui.h"
#include "verify.h"

#include <string.h>

/**
 * @brief ARM9-wide state shared by UI, scan, download and boot modules.
 */
IpcShared g_ipc ALIGNED_ATTR(32);
RunState g_runState = RUN_SCANNING;
u32 g_frameCounter = 0;
volatile bool g_arm7BootReady = false;
u8 g_currentWlanChannel = 0;

/**
 * @brief IPC diagnostic counters used to rate-limit repeated warnings.
 */
static u32 s_last_dropped_events;
static u32 s_ipc_hash_mismatch_logs;

/**
 * @brief Stores the currently scanned WLAN channel for UI display.
 */
void set_current_wlan_channel(u8 channel) {
    if (channel > 13) channel = 0;
    if (g_currentWlanChannel == channel) return;
    g_currentWlanChannel = channel;
    ui_mark_dirty();
}

typedef enum {
    IPC_COPY_RETRY = 0,
    IPC_COPY_OK,
    IPC_COPY_DROP,
} IpcCopyResult;

/**
 * @brief Routes a stable ARM7 event into the ARM9 scanner, downloader, or UI.
 *
 * Raw-frame statistics and signal updates are limited to the active parent;
 * PCAP capture also keeps local TX frames unfiltered so failed association
 * attempts retain their Auth/Assoc diagnostics.
 */
static void handle_event(const Arm7Event *evt) {
    char text[EVENT_DATA_MAX + 1u];
    switch (evt->type) {
        case EVENT_ARM7_READY:
            ipc_send_command(ARM7_CMD_SCAN_START, NULL, 0);
            break;
        case EVENT_LOG:
            ui_log("ARM7: %s", ipc_event_text(evt, text, sizeof(text)));
            break;
        case EVENT_SCAN_BSS:
            if (evt->length >= sizeof(Arm7BssEvent)) {
                scan_handle_bss((const Arm7BssEvent*)evt->data, evt->timestamp_us);
            }
            break;
        case EVENT_SCAN_CHANNEL:
            if (g_runState == RUN_SCANNING) {
                set_current_wlan_channel((u8)(evt->flags & 0xffu));
            }
            break;
        case EVENT_CONNECTED:
            download_handle_connected(evt->flags);
            break;
        case EVENT_CONNECT_FAILED:
            download_handle_connect_failed(evt->flags);
            break;
        case EVENT_DISCONNECTED:
            download_handle_disconnected();
            break;
        case EVENT_HOST_CMD:
            if ((g_runState == RUN_DOWNLOADING || g_runState == RUN_CONNECTING) &&
                !g_download.user_abort_requested) {
                download_handle_host_payload(evt->data, evt->length,
                                            evt->event_id, evt->timestamp_us,
                                            evt->flags);
            }
            break;
        case EVENT_RAW_FRAME:
            if (g_runState == RUN_DOWNLOADING || g_runState == RUN_CONNECTING) {
                bool active_parent_frame = download_frame_from_active_parent(evt->data, evt->length);
                bool local_tx = (evt->flags & RAW_DIR_MASK) == RAW_TX;
                bool active_parent_rx = !local_tx && active_parent_frame;
                if (active_parent_frame) {
                    download_observe_raw_frame(evt->data, evt->length, evt->flags);
                }
                if (active_parent_rx) {
                    if (evt->flags & RAW_SIGNAL_VALID) {
                        u8 percent = (u8)((evt->flags & RAW_SIGNAL_MASK) >> RAW_SIGNAL_SHIFT);
                        u8 raw_rssi = (u8)((evt->flags & RAW_RSSI_MASK) >> RAW_RSSI_SHIFT);
                        download_update_signal_raw(percent, raw_rssi);
                    }
                }
                PcapWriteResult pcap_result = (active_parent_frame || local_tx)
                    ? pcap_write_raw(evt->timestamp_us, evt->data, evt->length)
                    : pcap_write(evt->timestamp_us, evt->data, evt->length);
                if (pcap_result == PCAP_WRITE_ERROR) {
                    g_download.pcap_error = true;
                }
            }
            break;
        case EVENT_ERROR:
            ui_log("ARM7 error! %s",
                    evt->length ? ipc_event_text(evt, text, sizeof(text)) : "Unknown error.");
            break;
        case EVENT_BOOT_READY:
            g_arm7BootReady = true;
            break;
        default:
            break;
    }
}

/**
 * @brief Copies one ARM7 event with sequence and payload-hash validation.
 *
 * The shared event is read using cache invalidation before and after payload
 * copy. Torn writes are retried, stable host-payload hash mismatches are
 * dropped and counted, and all accepted lengths are clamped to the IPC buffer.
 */
static IpcCopyResult ipc_copy_event(Arm7Event *dst, Arm7Event *evt) {
    const size_t header_len = offsetof(Arm7Event, data);
    bool stable_hash_mismatch = false;
    u32 bad_event_id = 0;
    u32 bad_length = 0;
    u32 bad_expected_hash = 0;
    u32 bad_got_hash = 0;

    for (unsigned attempt = 0; attempt < 8; attempt++) {
        DC_InvalidateRange(evt, header_len);
        u32 seq0 = evt->seq;
        if (seq0 & 1) continue;

        u16 type = evt->type;
        u16 flags = evt->flags;
        u32 timestamp_us = evt->timestamp_us;
        u32 event_id = evt->event_id;
        u32 data_hash = evt->data_hash;
        u32 length = evt->length;
        if (length > EVENT_DATA_MAX) length = EVENT_DATA_MAX;
        if (length) {
            DC_InvalidateRange(evt->data, length);
            memcpy(dst->data, evt->data, length);
        }

        DC_InvalidateRange(evt, header_len);
        u32 seq1 = evt->seq;
        if (seq0 == seq1 && !(seq1 & 1)) {
            dst->seq = seq1;
            dst->type = type;
            dst->flags = flags;
            dst->timestamp_us = timestamp_us;
            dst->event_id = event_id;
            dst->data_hash = data_hash;
            dst->length = length;
            if (type == EVENT_HOST_CMD && length) {
                u32 got_hash = ipc_hash_bytes(dst->data, length);
                if (data_hash != got_hash) {
                    stable_hash_mismatch = true;
                    bad_event_id = event_id;
                    bad_length = length;
                    bad_expected_hash = data_hash;
                    bad_got_hash = got_hash;
                    continue;
                }
            }
            return IPC_COPY_OK;
        }
    }

    if (stable_hash_mismatch) {
        if (s_ipc_hash_mismatch_logs < 16) {
            debug_log("ipc dropped corrupt host event ev=%lu len=%lu expected=%08lx got=%08lx",
                       (unsigned long)bad_event_id,
                       (unsigned long)bad_length,
                       (unsigned long)bad_expected_hash,
                       (unsigned long)bad_got_hash);
            s_ipc_hash_mismatch_logs++;
        }
        return IPC_COPY_DROP;
    }

    return IPC_COPY_RETRY;
}

/**
 * @brief Initializes shared ARM9/ARM7 IPC state and sends its address to ARM7.
 */
void ipc_init(void) {
    memset(&g_ipc, 0, sizeof(g_ipc));
    s_last_dropped_events = 0;
    s_ipc_hash_mismatch_logs = 0;
    g_arm7BootReady = false;
    g_ipc.magic = IPC_MAGIC;
    DC_FlushRange(&g_ipc, sizeof(g_ipc));

    pxiWaitRemote(PxiChannel_User0);
    pxiSend(PxiChannel_User0, ((u32)&g_ipc) >> 2);
    swiDelay(20000);
    ipc_send_command(ARM7_CMD_WIFI_INIT, NULL, 0);
}

/**
 * @brief Returns the ARM7 event-drop counter from shared IPC state.
 */
u32 ipc_dropped_events_snapshot(void) {
    DC_InvalidateRange((void*)&g_ipc.dropped_events, sizeof(g_ipc.dropped_events));
    s_last_dropped_events = g_ipc.dropped_events;
    return g_ipc.dropped_events;
}

/**
 * @brief Resets the shared ARM7 event ring before a new operation.
 */
void ipc_reset_event_queue(void) {
    DC_InvalidateRange((void*)&g_ipc.event_w, sizeof(g_ipc.event_w));
    DC_InvalidateRange((void*)&g_ipc.dropped_events, sizeof(g_ipc.dropped_events));
    g_ipc.event_r = g_ipc.event_w;
    DC_FlushRange((void*)&g_ipc.event_r, sizeof(g_ipc.event_r));
    s_last_dropped_events = g_ipc.dropped_events;
}

/**
 * @brief Copies one ARM9 command into shared IPC memory and kicks ARM7.
 */
void ipc_send_command(u32 id, const void *arg, size_t arg_len) {
    if (arg_len > sizeof(g_ipc.command.arg)) arg_len = sizeof(g_ipc.command.arg);
    g_ipc.command.seq++;
    g_ipc.command.id = id;
    memset((void*)&g_ipc.command.arg, 0, sizeof(g_ipc.command.arg));
    if (arg && arg_len) memcpy((void*)&g_ipc.command.arg, arg, arg_len);
    g_ipc.command.seq++;
    DC_FlushRange(&g_ipc.command, sizeof(g_ipc.command));
    pxiSend(PxiChannel_User0, PXI_CMD_KICK);
}

/**
 * @brief Drains complete ARM7 events from the shared IPC ring.
 */
void ipc_poll(void) {
    DC_InvalidateRange((void*)&g_ipc.dropped_events, sizeof(g_ipc.dropped_events));
    if (g_ipc.dropped_events != s_last_dropped_events) {
        debug_log("ipc dropped_events=%lu delta=%lu",
                   (unsigned long)g_ipc.dropped_events,
                   (unsigned long)(g_ipc.dropped_events - s_last_dropped_events));
        download_note_ipc_dropped_events(g_ipc.dropped_events);
        s_last_dropped_events = g_ipc.dropped_events;
    }

    for (;;) {
        DC_InvalidateRange((void*)&g_ipc.event_w, sizeof(g_ipc.event_w));
        u32 r = g_ipc.event_r;
        u32 w = g_ipc.event_w;
        if (r == w) break;
        Arm7Event local;
        Arm7Event *evt = &g_ipc.events[r];
        IpcCopyResult copy_result = ipc_copy_event(&local, evt);
        if (copy_result == IPC_COPY_RETRY) break;
        g_ipc.event_r = (r + 1) % EVENT_RING_COUNT;
        DC_FlushRange((void*)&g_ipc.event_r, sizeof(g_ipc.event_r));
        if (copy_result == IPC_COPY_OK) {
            handle_event(&local);
        } else if (copy_result == IPC_COPY_DROP && g_download.active) {
            g_download.stats.packets_dropped++;
        }
    }
}
