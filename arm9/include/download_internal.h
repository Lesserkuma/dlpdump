#ifndef DOWNLOAD_INTERNAL_H
#define DOWNLOAD_INTERNAL_H

#include "state.h"
#include "boot.h"
#include "debug.h"
#include "download.h"
#include "file.h"
#include "ipc_arm9.h"
#include "meta.h"
#include "pcap.h"
#include "path.h"
#include "report.h"
#include "scan.h"
#include "system.h"
#include "ui.h"
#include "verify.h"
#include "memory_map.h"
#include "dlp_wire.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

extern u16 s_debug_last_received_log;
extern u16 s_debug_last_final_missing;
extern u16 s_debug_last_final_received;
extern u16 s_debug_last_correction_missing;
extern u16 s_debug_last_correction_received;
extern u8 s_debug_duplicate_mismatches;
extern u32 s_next_missing_cursor;
extern u32 s_last_signal_update_frame;

/** @brief Returns the current timeout timestamp in seconds, or 0 on RTC failure. */
u32 download_now_seconds(void);

/** @brief Queues an ARM7 reply frame for username, RSA, data or final state. */
void download_send_reply(u8 type, u16 next_packet, u16 total_packets);

/** @brief Returns the effective parent packet size for UI/debug output. */
unsigned download_packet_size(void);

/** @brief Logs the RSA user-parameter block if it contains non-zero data. */
void download_log_user_parameters_if_present(u32 id);

/** @brief Frees section buffers and packet bitsets owned by a download object. */
void download_free_allocations(Download *download);

/** @brief Clears transfer-only fields while preserving non-transfer context. */
void download_reset_transfer_fields(Download *download);

/** @brief Frees buffers in `g_download` and clears transfer counters. */
void download_free_buffers(void);

/** @brief Runs verify/save/report completion, then returns to scan mode. */
void download_save_and_restart(const char *completion_reason);

/** @brief Parses and validates the parent RSA control block. */
bool download_parse_rsa_frame(const u8 *data, unsigned len);

/** @brief Finds the next missing DATA packet number for correction replies. */
u16 download_next_missing(void);

/** @brief Applies one parent DATA command to the active download buffer. */
void download_handle_data(const u8 *msg, unsigned len, u32 event_id, u32 timestamp_us, u16 rx_status);

/** @brief Extracts the DS Download Play host command from a raw 802.11 payload. */
bool download_extract_host_msg(const u8 *payload, unsigned len, const u8 **msg, unsigned *msg_len);

#endif
