#pragma once

/**
 * @file handover_bss.h
 * @brief Shared helpers for the fixed Download Play WM-BSS handover block.
 */

#include "endian.h"
#include "handover.h"

#include <string.h>

/**
 * @brief Builds the six-byte multiboot association SSID inside a 32-byte field.
 */
static inline void handover_bss_write_assoc_ssid(u8 *ssid,
                                                    u32 game_group_id,
                                                    u16 temporary_group_id) {
    memset(ssid, 0, HANDOVER_BSS_MAX_SSID_LEN);
    ssid[0] = (u8)game_group_id;
    ssid[1] = (u8)(game_group_id >> 8);
    ssid[2] = (u8)(game_group_id >> 16);
    ssid[3] = (u8)(game_group_id >> 24);
    ssid[4] = (u8)temporary_group_id;
    ssid[5] = (u8)(temporary_group_id >> 8);
}

/**
 * @brief Writes a complete WM-BSS handover block from parsed scan metadata.
 */
static inline void handover_bss_write(u8 out[HANDOVER_BSS_SIZE],
                                         u16 rssi,
                                         const u8 *bssid,
                                         u32 game_group_id,
                                         u16 temporary_group_id,
                                         u16 capabilities,
                                         u16 basic_rates,
                                         u16 all_rates,
                                         u16 beacon_period,
                                         u16 dtim_period,
                                         u16 channel) {
    memset(out, 0, HANDOVER_BSS_SIZE);
    stle16(out + HANDOVER_BSS_DESC_SIZE_OFF, WM_BSS_DESC_SIZE);
    stle16(out + HANDOVER_BSS_RSSI_OFF, rssi);
    if (bssid) memcpy(out + HANDOVER_BSS_BSSID_OFF, bssid, HANDOVER_BSS_BSSID_BYTES);
    stle16(out + HANDOVER_BSS_SSID_LEN_OFF, HANDOVER_BSS_MAX_SSID_LEN);
    handover_bss_write_assoc_ssid(out + HANDOVER_BSS_GAME_GROUP_ID_OFF,
                                     game_group_id, temporary_group_id);
    stle16(out + HANDOVER_BSS_CAPABILITIES_OFF, capabilities);
    stle16(out + HANDOVER_BSS_BASIC_RATES_OFF, basic_rates);
    stle16(out + HANDOVER_BSS_ALL_RATES_OFF, all_rates);
    stle16(out + HANDOVER_BSS_BEACON_PERIOD_OFF, beacon_period);
    stle16(out + HANDOVER_BSS_DTIM_PERIOD_OFF, dtim_period);
    stle16(out + HANDOVER_BSS_CHANNEL_OFF, channel);
}

/**
 * @brief Writes the minimal sidecar-restored WM-BSS handover block.
 */
static inline void handover_bss_write_minimal(u8 out[HANDOVER_BSS_SIZE],
                                                 u32 game_group_id) {
    memset(out, 0, HANDOVER_BSS_SIZE);
    stle16(out + HANDOVER_BSS_DESC_SIZE_OFF, WM_BSS_DESC_SIZE);
    stle16(out + HANDOVER_BSS_SSID_LEN_OFF, HANDOVER_BSS_MAX_SSID_LEN);
    stle32(out + HANDOVER_BSS_GAME_GROUP_ID_OFF, game_group_id);
}

/**
 * @brief Validates the fixed fields needed by the boot handover path.
 */
static inline bool handover_bss_valid(const u8 handover[HANDOVER_BSS_SIZE]) {
    return handover &&
           le16(handover + HANDOVER_BSS_DESC_SIZE_OFF) == WM_BSS_DESC_SIZE &&
           le16(handover + HANDOVER_BSS_SSID_LEN_OFF) <= HANDOVER_BSS_MAX_SSID_LEN &&
           le16(handover + HANDOVER_BSS_CHANNEL_OFF) <= HANDOVER_BSS_MAX_CHANNEL;
}

/**
 * @brief Extracts the game-group ID from a validated WM-BSS handover block.
 */
static inline u32 handover_bss_game_group_id(const u8 handover[HANDOVER_BSS_SIZE]) {
    return handover_bss_valid(handover)
        ? le32(handover + HANDOVER_BSS_GAME_GROUP_ID_OFF)
        : 0;
}
