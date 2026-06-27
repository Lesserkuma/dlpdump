#pragma once

/**
 * @file handover.h
 * @brief Fixed-memory addresses and sidecar-layout constants for boot handover.
 *
 * The ARM9 boot path, ARM7 boot stub and saved BCN sidecar all depend on these
 * addresses. Literal values must be defined here only; legacy `BOOT_*` names
 * may alias these constants for source compatibility.
 */

#include "protocol.h"
#include "types.h"

#define HANDOVER_FIXED_BEACON_ADDR          0x023fe800u
#define HANDOVER_FIXED_CONTROL_ADDR         0x023fe840u
#define HANDOVER_ARM7_STATUS_ADDR           0x023fe940u
#define HANDOVER_RUNTIME_CONTROL_ADDR       0x023fea00u
#define HANDOVER_DOWNLOAD_PARAMETER_ADDR    0x027ffbe0u
#define HANDOVER_FIXED_HEADER_ADDR          0x027ffe00u
#define HANDOVER_ARM7_STUB_ADDR             0x0380f700u

#define HANDOVER_HEADER_SECTION_BYTES       0x160u
#define HANDOVER_DOWNLOAD_PARAMETER_BYTES   0x20u
#define HANDOVER_BCN_RESERVED_BYTES         0x04u
#define HANDOVER_BCN_CONTEXT_BYTES          (0x04u + HANDOVER_BCN_RESERVED_BYTES + HANDOVER_DOWNLOAD_PARAMETER_BYTES)

#define HANDOVER_BCN_GAME_GROUP_ID_OFF       0x00u
#define HANDOVER_BCN_RESERVED_OFF            0x04u
#define HANDOVER_BCN_DOWNLOAD_PARAMETER_OFF  0x08u

#define HANDOVER_BSS_DESC_SIZE_OFF          0x00u
#define HANDOVER_BSS_RSSI_OFF               0x02u
#define HANDOVER_BSS_BSSID_OFF              0x04u
#define HANDOVER_BSS_BSSID_BYTES            6u
#define HANDOVER_BSS_SSID_LEN_OFF           0x0au
#define HANDOVER_BSS_GAME_GROUP_ID_OFF      0x0cu
#define HANDOVER_BSS_CAPABILITIES_OFF       0x2cu
#define HANDOVER_BSS_BASIC_RATES_OFF        0x2eu
#define HANDOVER_BSS_ALL_RATES_OFF          0x30u
#define HANDOVER_BSS_BEACON_PERIOD_OFF      0x32u
#define HANDOVER_BSS_DTIM_PERIOD_OFF        0x34u
#define HANDOVER_BSS_CHANNEL_OFF            0x36u
#define HANDOVER_BSS_MAX_SSID_LEN           32u
#define HANDOVER_BSS_MAX_CHANNEL            14u

#define HANDOVER_ARM9_ENTRY_ADDR            0x027ffe24u
#define HANDOVER_ARM7_ENTRY_ADDR            0x027ffe34u
#define HANDOVER_PARENT_PARAM_MAGIC_ADDR    0x027ff814u
#define HANDOVER_BEACON_MODE_ADDR           0x027ffc40u
#define HANDOVER_BEACON_PAYLOAD_ADDR        0x027ffc42u
#define HANDOVER_BEACON_MODE_DOWNLOAD_PLAY  2u

#define HANDOVER_ARM7_STATUS_COPIED         0x44504137u /* DPA7 */
#define HANDOVER_ARM9_NTR_SWITCH            0x44504e54u /* DPNT */
#define HANDOVER_ARM7_STATUS_NTR_READY      0x44504e37u /* DPN7 */
#define HANDOVER_ARM7_STATUS_LAUNCH         0x44504c41u /* DPLA */
#define HANDOVER_ARM9_RELEASE               0x44504254u /* DPBT */
