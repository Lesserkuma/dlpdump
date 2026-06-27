#ifndef ARM7_INTERNAL_H
#define ARM7_INTERNAL_H

#include "../../common/ipc.h"
#include "../../common/beacon_ie.h"
#include "../../common/endian.h"
#include "mwl_private_common.h"

#include <nds.h>
#include <string.h>
#include <stdio.h>
#include <calico/dev/mwl.h>
#include <calico/nds/arm7/gpio.h>
#include <calico/nds/system.h>
#include <calico/nds/pm.h>
#include <calico/system/thread.h>

#define SCAN_ALL_CHANNELS_MASK     0x3ffeu
#define SCAN_PRIMARY_CHANNELS_MASK SCAN_ALL_CHANNELS_MASK
#define NETBUF_POOL_MEM_SZ         (8u * (sizeof(NetBuf) + 0x100u))
#define ASSOC_SSID_LEN             32u
#define RAW_EVENT_RESERVE          ((EVENT_RING_COUNT * 3u) / 4u)
#define HOST_DATA_EVENT_RESERVE    (EVENT_RING_COUNT / 8u)
#define FAST_MAX_PACKETS           0x4000u
#define SCAN_FOCUS_DONE_COUNT      8u
#define SCAN_FOCUS_COMPLETE_MASK   ((1u << SNIPPET_COUNT) - 1u)
#define SCAN_FOCUS_TIMEOUT_US      2000000u
#define WIFI_FC_DATA_CF_ACK_TODS_PM 0x1118u

/**
 * @brief Tracks one focused scan target until all metadata snippets are seen.
 */
typedef struct {
    bool used;
    u8 bssid[6];
    u32 game_group_id;
    u32 game_id;
    u16 temporary_group_id;
    u8 session_id;
    u8 file_no;
    u16 snippet_mask;
    u32 focus_start_us;
} Arm7ScanFocus;

/**
 * @brief ARM7 module globals exported between local translation units.
 */
extern IpcShared *g_ipc;
extern bool g_scan_enabled;
extern bool g_raw_capture_enabled;
extern bool g_netbuf_ready;
extern bool g_wifi_hw_started;
extern u16 g_assoc_aid;
extern ConnectParams g_active_parent;
extern u16 g_reply_buf_index;
extern u16 g_tx_seq;
extern u8 g_name_reply_step;
extern u8 g_scan_focus_channel;
extern u8 g_scan_current_channel;
extern ReplyParams g_pending_reply;
extern bool g_have_pending_reply;
extern u8 g_client_msg_tail[7];
extern bool g_have_client_msg_tail;
extern bool g_seen_rsa_cmd;
extern u16 g_fast_next_packet;
extern u16 g_fast_received_packets;
extern u16 g_fast_expected_total;
extern u16 g_fast_last_packet;
extern u16 g_fast_complete_next;
extern u16 g_fast_complete_total;
extern u8 g_fast_gotall_replies;
extern bool g_fast_data_complete;
extern u32 g_event_id;
extern u8 s_netbuf_pool_mem[NETBUF_POOL_MEM_SZ];
extern u8 s_fast_received_bits[(FAST_MAX_PACKETS + 7u) / 8u];
extern const u16 s_netbuf_tx_counts[5];
extern const u16 s_netbuf_rx_counts[5];
extern Arm7ScanFocus g_scan_focus;
extern Arm7ScanFocus g_scan_focus_done[SCAN_FOCUS_DONE_COUNT];
extern u8 g_scan_focus_done_next;

/**
 * @brief Linker-provided bounds for the ARM7 boot handover stub.
 */
extern const u8 arm7BootStubStart[];
extern const u8 arm7BootStubEnd[];

/**
 * @brief Function signature of the copied ARM7 boot handover stub.
 */
typedef void (*Arm7BootStubFn)(const void *control, volatile u32 *status);

/**
 * @brief Reads Calico's Wi-Fi microsecond counter as a 32-bit timestamp.
 */
static inline u32 arm7_timestamp_us(void) {
    return MWL_REG(W_US_COUNT0) | ((u32)MWL_REG(W_US_COUNT1) << 16);
}

/** @brief Pushes one bounded event into the ARM7-to-ARM9 ring buffer. */
bool arm7_push_event(u16 type, u16 flags, const void *data, u32 len);

/** @brief Pushes a raw 802.11 frame event when capture has buffer space. */
bool arm7_push_raw_event(u16 flags, const void *data, u32 len);

/** @brief Pushes a parent host-data frame event with ring-reserve protection. */
bool arm7_push_host_data_event(u16 flags, const void *data, u32 len);

/** @brief Sends a bounded diagnostic text event to ARM9. */
void arm7_log(const char *s);

/** @brief Clears the current metadata-focused scan target and history. */
void scan_focus_clear(void);

/** @brief Drops any pending child reply cached for retransmission. */
void arm7_clear_pending_reply(void);

/** @brief Applies Calico and hardware Wi-Fi power-save state. */
void arm7_set_power_state(u16 state);

/** @brief Converts raw Calico RSSI into the UI signal percentage scale. */
u8 arm7_signal_percent_from_rssi(u8 rssi);

/** @brief Returns whether an incoming frame belongs to the active parent BSSID. */
bool arm7_frame_from_active_parent(const void *frame, unsigned len);

/** @brief Advances the channel scanner after one Calico dwell completes. */
u32 arm7_on_scan_end(void);

/** @brief Converts one Calico BSS descriptor into a shared scan event. */
void arm7_on_bss_info(WlanBssDesc *bss, WlanBssExtra *extra);

/** @brief Starts or restarts ARM7-side DS Download Play parent scanning. */
void arm7_start_scan(void);

/** @brief Clears the Fast-ACK packet bitmap for a new transfer. */
void arm7_fast_reset_packets(u16 total_packets);

/** @brief Builds and transmits one child reply frame. */
void arm7_write_reply_frame(const ReplyParams *p);

/** @brief Retransmits the cached child reply after a host retry trigger. */
void arm7_rewrite_pending_reply(void);

/** @brief Applies Pico-Loader-derived TWL-to-NTR ARM7 compatibility setup. */
void arm7_prepare_ds_mode_for_boot(u32 game_code);

/** @brief Copies the ARM7 boot stub into fixed memory for final handoff. */
void arm7_prepare_boot_stub(bool switch_to_ntr, u32 game_code);

/** @brief Jumps into the prepared ARM7 boot stub and never returns on success. */
void arm7_boot_downloaded_program(void);

#endif
