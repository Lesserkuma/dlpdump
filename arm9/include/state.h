#pragma once

/**
 * @file state.h
 * @brief Shared ARM9 application state, constants and data structures.
 */
#include "../../common/ipc.h"
#include "../../common/protocol.h"
#include "config.h"
#include "hash.h"

#include <nds.h>
#include <fat.h>
#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BEACON_ICON_W          32
#define BEACON_ICON_H          32
#define BEACON_ICON_PIXELS     (BEACON_ICON_W * BEACON_ICON_H)
#define HOST_NAME_CHARS        10
#define TITLE_CHARS            48
#define DESCRIPTION_CHARS      96
#define TEXT_UTF8_BYTES(chars) ((chars) * 4 + 1)
#define OUTPUT_BASE_BYTES      192
#define MEMBER_SLOT_COUNT      15
#define MEMBER_FRAGMENT_BYTES  0x16
#define SIGNAL_LEVEL_1_PERCENT 48
#define SIGNAL_LEVEL_2_PERCENT 76
#define SIGNAL_LEVEL_3_PERCENT 100

typedef enum {
    RUN_SCANNING = 0,
    RUN_CONNECTING,
    RUN_DOWNLOADING,
    RUN_CHECKING_RSA_HASH,
    RUN_SAVING,
    RUN_CREATING_REPORT,
} RunState;

typedef enum {
    VERIFY_IDLE = 0,
    VERIFY_PENDING,
    VERIFY_OK,
    VERIFY_FAILED,
    VERIFY_SKIPPED,
} VerifyStatus;

/**
 * @brief One discovered DS Download Play parent and its collected metadata.
 */
typedef struct {
    bool used;
    bool complete;
    bool tried;
    bool downloaded;
    bool info_valid;
    u32 id;
    u32 game_group_id;
    u32 game_id;
    u16 temporary_group_id;
    u16 parent_packet_max_bytes;
    u16 child_packet_max_bytes;
    u8 session_id;
    u8 file_no;
    u8 connected_count;
    u8 max_players;
    u8 volatile_counter;
    u8 beacon_data_attr;
    ScanBssDesc bss;
    bool handover_valid;
    u8 handover[HANDOVER_BSS_SIZE];
    u16 fragment_mask;
    u8 fragment_len[SNIPPET_COUNT];
    u8 fragments[SNIPPET_COUNT][BEACON_FIXED_FRAGMENT_STORAGE_BYTES];
    u16 beacon_frame_mask;
    u16 beacon_frame_len[SNIPPET_COUNT];
    u32 beacon_frame_ts_us[SNIPPET_COUNT];
    u8 beacon_frames[SNIPPET_COUNT][MAX_BEACON_FRAME];
    u8 nin_sample_len;
    u8 nin_sample[MAX_NIN_PAYLOAD];
    u16 member_active_mask;
    u16 member_name_mask;
    u8 member_fragments[MEMBER_SLOT_COUNT][MEMBER_FRAGMENT_BYTES];
    char member_names[MEMBER_SLOT_COUNT][TEXT_UTF8_BYTES(USER_NAME_CHARS)];
    char host_name[TEXT_UTF8_BYTES(HOST_NAME_CHARS)];
    char title[TEXT_UTF8_BYTES(TITLE_CHARS)];
    char description[TEXT_UTF8_BYTES(DESCRIPTION_CHARS)];
    bool icon_valid;
    u16 icon[BEACON_ICON_PIXELS];
    u32 last_seen_frame;
    u32 last_seen_time;
} ContentSlot;

/**
 * @brief One downloadable section from the RSA control frame plus payload bytes.
 */
typedef struct {
    u32 staging_addr;
    u32 load_addr;
    u32 size;
    u32 flags;
    u8 *data;
} Section;

/**
 * @brief Protocol counters accumulated during one child download session.
 */
typedef struct {
    u32 commands_rejected;
    u32 packets_dropped;
    u32 bad_file_no_packets;
    u32 out_of_range_data;
    u32 short_data_packets;
    u32 malformed_commands;
    u32 duplicate_data;
    u32 duplicate_payload;
    u32 duplicate_different;
    u32 name_requests_seen;
    u32 rsa_packets_seen;
    u32 rsa_repeats;
    u32 data_packets_seen;
    u32 unique_data_packets;
    u32 expected_data_packets;
    u32 final_commands_seen;
    u32 got_all_replies_sent;
    u32 correction_replies_sent;
    u32 final_replies_sent;
} ProtocolStats;

/**
 * @brief Mutable ARM9 state for the currently active download transaction.
 */
typedef struct {
    bool active;
    bool have_rsa;
    bool rsa_ack_deferred;
    bool transfer_started;
    bool saved;
    bool final_seen;
    bool child_cancel_sent;
    bool user_abort_requested;
    bool pcap_error;
    bool rsa_hash_verified;
    bool rsa_verification_skipped;
    ContentSlot *slot;
    ContentSlot *scan_slot;
    ContentSlot slot_snapshot;
    u32 id;
    u16 parent_packet_max_bytes;
    u16 child_packet_max_bytes;
    u16 max_payload;
    u16 expected_file_no;
    u16 assoc_aid;
    u16 total_packets;
    u16 received_packets;
    bool signal_valid;
    u8 signal_percent;
    u8 signal_min_percent;
    u8 signal_max_percent;
    u32 signal_sum_percent;
    u32 signal_sample_count;
    u8 signal_raw_min;
    u8 signal_raw_max;
    u32 signal_raw_sum;
    bool save_progress_valid;
    u8 save_percent;
    u8 name_snippet;
    UserInfo user;
    bool beacon_pcap_written;
    u16 start_temporary_group_id;
    u32 ipc_drop_baseline;
    u32 ipc_drop_seen;
    u32 completion_time;
    char completion_reason[48];
    ProtocolStats stats;
    char output_base[OUTPUT_BASE_BYTES];
    u32 last_comm_time;
    u32 user_abort_time;
    u32 start_time;
    u32 data_start_time;
    u32 transfer_wait_start_time;
    u32 all_packets_time;
    u8 *received_bits;
    DownloadRsaFrame rsa;
    Section sec[3];
} Download;

/**
 * @brief ARM9 global state objects shared across application modules.
 */
extern IpcShared g_ipc;
extern RunState g_runState;
extern u32 g_frameCounter;
extern ContentSlot g_slots[CONTENT_SLOT_COUNT];
extern Download g_download;
extern VerifyStatus g_verifyStatus;
extern u32 g_verifyContentId;
extern volatile bool g_arm7BootReady;
extern u8 g_currentWlanChannel;
extern bool g_repeatDownloads;
