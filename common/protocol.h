#pragma once
#include "types.h"
#include "beacon_ie.h"

#define CONTENT_SLOT_COUNT 16
#define SNIPPET_COUNT      10
#define MAX_NIN_PAYLOAD    160
#define MAX_IEEE_FRAME     2048
#define MAX_BEACON_FRAME   512
#define PARENT_MAX_DEFAULT 0x01fe
#define CHILD_MAX_DEFAULT  0x0008
#define USER_NAME_CHARS    10
#define WM_BSS_DESC_SIZE   0x00c0u
#define HANDOVER_BSS_SIZE  0x003cu

#define CMD_IDLE         0
#define CMD_NAME_REQUEST 1
#define CMD_REJECT       2
#define CMD_RSA          3
#define CMD_DATA         4
#define CMD_FINAL        5
#define CMD_CANCEL       6

#define REPLY_DUMMY      0x00
#define REPLY_USERNAME   0x07
#define REPLY_RSA        0x08
#define REPLY_DATA       0x09
#define REPLY_GOT_ALL    0x0a
#define REPLY_FINAL      0x0b

#define SIG_ID0 0x61
#define SIG_ID1 0x63
#define SIG_ID2 0x01
#define SIG_ID3 0x00

/**
 * @brief One section descriptor embedded in the DS Download Play RSA frame.
 */
typedef struct PACKED_ATTR {
    u32 staging_addr;
    u32 load_addr;
    u32 size;
    u32 flags;
} DownloadSectionEntry;

/**
 * @brief DS Download Play RSA control frame describing boot sections.
 */
typedef struct PACKED_ATTR {
    u32 arm9_entrypoint;
    u32 arm7_entrypoint;
    u32 reserved_zero0;
    DownloadSectionEntry section[3];
    u8 signature_id[4];
    u8 signature[128];
    u8 signature_seed[4];
    u8 download_parameter[0x20];
} DownloadRsaFrame;

/**
 * @brief Minimal 802.11 MAC header used by raw frame parsing and PCAP output.
 */
typedef struct PACKED_ATTR {
    u16 frame_control;
    u16 duration;
    u8  addr1[6];
    u8  addr2[6];
    u8  addr3[6];
    u16 sequence_control;
} Dot11Hdr;

#define DOWNLOAD_RSA_FRAME_SIZE 0xe4
#define DOT11_HDR_SIZE 24

/**
 * @brief Normalized BSS descriptor shared between ARM7 scan code and ARM9 UI.
 */
typedef struct {
    u8 bssid[6];
    u16 ssid_len;
    char ssid[32];
    u16 ieee_caps;
    u16 ieee_basic_rates;
    u16 ieee_all_rates;
    u8 auth_type;
    u8 rssi;
    u8 channel;
    u8 _pad;
} ScanBssDesc;

/**
 * @brief ARM7 scan event containing beacon metadata and optional raw frame bytes.
 */
typedef struct {
    ScanBssDesc bss;
    u32 game_group_id;
    u32 game_id;
    u16 temporary_group_id;
    u16 parent_packet_max_bytes;
    u16 child_packet_max_bytes;
    u8 session_id;
    u8 connected_count;
    u8 snippet_no;
    u8 beacon_data_attr;
    u8 file_no;
    u8 vendor_ie_len;
    u8 vendor_ie_payload[MAX_NIN_PAYLOAD];
    u16 beacon_frame_len;
    u8 beacon_frame[MAX_BEACON_FRAME];
} Arm7BssEvent;

/**
 * @brief Firmware user profile subset sent in child username replies.
 */
typedef struct {
    u8 favorite_color;
    u8 name_len;
    u8 player_no;
    u8 _pad;
    u16 name[USER_NAME_CHARS];
} UserInfo;

/**
 * @brief Parent connection parameters copied from a selected content slot.
 */
typedef struct {
    ScanBssDesc bss;
    u32 game_group_id;
    u32 game_id;
    u16 temporary_group_id;
    u16 parent_packet_max_bytes;
    u16 child_packet_max_bytes;
    u8 session_id;
    u8 file_no;
    UserInfo user;
} ConnectParams;

/**
 * @brief Encoded child reply parameters queued from ARM9 to ARM7.
 */
typedef struct {
    u8 reply_type;
    u8 user_snippet_no;
    u16 next_packet;
    u16 total_packets;
    u16 parent_reply_time;
    u8 user_payload[6];
} ReplyParams;

/**
 * @brief Builds the six-byte username reply fragment requested by a parent.
 *
 * The reply format is split into five snippets: snippet 0 carries game id and
 * profile metadata, snippets 1..3 carry three UTF-16 code units each, and
 * snippet 4 carries the tenth UTF-16 unit plus player/file identifiers.
 *
 * @param p Reply object to mutate. Its `user_snippet_no` selects the fragment.
 * @param game_id DS Download Play game id advertised by the parent.
 * @param file_no File number currently being requested.
 * @param user Optional firmware user profile; NULL emits zero-filled fields.
 */
static inline void build_username_snippet(ReplyParams *p, u32 game_id, u8 file_no, const UserInfo *user) {
    for (unsigned i = 0; i < sizeof(p->user_payload); i++) p->user_payload[i] = 0;

    u8 color = 0;
    u8 name_len = 0;
    u8 player_no = 1;
    const u16 *name = NULL;

    if (user) {
        color = user->favorite_color & 0x0f;
        name_len = user->name_len <= USER_NAME_CHARS ? user->name_len : USER_NAME_CHARS;
        player_no = user->player_no ? user->player_no : 1;
        name = user->name;
    }

    switch (p->user_snippet_no) {
        case 0:
            stle32(p->user_payload + 0, game_id);
            p->user_payload[4] = color;
            p->user_payload[5] = name_len;
            break;
        case 1:
        case 2:
        case 3: {
            unsigned base = (unsigned)(p->user_snippet_no - 1) * 3;
            for (unsigned i = 0; i < 3; i++) {
                unsigned idx = base + i;
                stle16(p->user_payload + i * 2, (name && idx < name_len) ? name[idx] : 0);
            }
            break;
        }
        case 4:
            stle16(p->user_payload + 0, (name && name_len > 9) ? name[9] : 0);
            stle16(p->user_payload + 2, player_no);
            stle16(p->user_payload + 4, file_no);
            break;
        default:
            break;
    }
}

/**
 * @brief Sidecar BCN file payload used to preserve beacon and handover context.
 */
typedef struct {
    u32 id;
    ScanBssDesc bss;
    u32 game_group_id;
    u32 game_id;
    u16 temporary_group_id;
    u16 parent_packet_max_bytes;
    u16 child_packet_max_bytes;
    u8 session_id;
    u16 fragment_mask;
    u8 fragment_len[SNIPPET_COUNT];
    u8 fragments[SNIPPET_COUNT][BEACON_FIXED_FRAGMENT_STORAGE_BYTES];
    u8 nin_sample_len;
    u8 nin_sample[MAX_NIN_PAYLOAD];
} BcnFile;
