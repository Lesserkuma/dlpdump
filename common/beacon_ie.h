#pragma once

#include "types.h"

/**
 * @file beacon_ie.h
 * @brief Nintendo DS Download Play vendor-IE constants and small parser helpers.
 */

typedef enum {
    BEACON_DATA_ATTR_FIXED_NORMAL = 0u,
    BEACON_DATA_ATTR_FIXED_NO_ICON = 1u,
    BEACON_DATA_ATTR_VOLATILE = 2u,
} BeaconDataAttr;

/**
 * @brief Parsed view of one Nintendo DS Download Play vendor-IE payload.
 */
typedef struct {
    const u8 *payload;
    unsigned payload_len;
    BeaconDataAttr data_attr;
    bool checksum_ok;
    bool has_icon;
    bool fixed_fragment_valid;
    const u8 *fragment;
    unsigned fragment_len;
    unsigned fragment_index;
    unsigned fragment_total;
    unsigned fragment_data_len;
    const u8 *volatile_payload;
    unsigned volatile_payload_len;
} BeaconIeView;

/**
 * @brief Layout description for fixed metadata with or without an icon block.
 */
typedef struct {
    bool has_icon;
    unsigned color_off;
    unsigned host_name_off;
    unsigned max_players_off;
    unsigned title_off;
    unsigned description_off;
    unsigned required_len;
    unsigned fragment_count;
} FixedMetadataLayout;

#define BEACON_VENDOR_OUI_BYTES        4u
#define BEACON_VENDOR_IE_STEPPING_OFF  0x00u
#define BEACON_VENDOR_IE_LCD_SYNC_OFF  0x02u
#define BEACON_VENDOR_IE_FIXED_ID_OFF  0x04u
#define BEACON_VENDOR_IE_GGID_OFF      0x08u
#define BEACON_VENDOR_IE_TGID_OFF      0x0cu
#define BEACON_VENDOR_IE_DATA_LEN_OFF  0x0eu
#define BEACON_VENDOR_IE_TYPE_OFF      0x0fu
#define BEACON_VENDOR_IE_PARENT_MAX_OFF 0x10u
#define BEACON_VENDOR_IE_CHILD_MAX_OFF 0x12u
#define BEACON_VENDOR_IE_MB_GAME_ID_OFF 0x14u
#define BEACON_VENDOR_IE_FILE_SESSION_OFF 0x18u
#define BEACON_VENDOR_IE_CONNECTED_OFF 0x1au
#define BEACON_VENDOR_IE_SNIPPET_OFF   0x1bu
#define BEACON_VENDOR_IE_CHECKSUM_OFF  0x1cu
#define BEACON_VENDOR_IE_CHECKSUM_LEN  0x68u
#define BEACON_VENDOR_IE_MIN_BYTES     (BEACON_VENDOR_IE_CHECKSUM_OFF + BEACON_VENDOR_IE_CHECKSUM_LEN)
#define BEACON_VENDOR_IE_FRAGMENT_OFF  0x1eu
#define BEACON_VENDOR_IE_RETAIL_TYPE   0x0bu
#define BEACON_FRAGMENT_HEADER_BYTES   4u
#define BEACON_FIXED_FRAGMENT_BYTES    0x62u
#define BEACON_FIXED_FRAGMENT_STORAGE_BYTES (BEACON_FRAGMENT_HEADER_BYTES + BEACON_FIXED_FRAGMENT_BYTES)
#define BEACON_FIXED_MAX_FRAGMENTS     9u
#define BEACON_FIXED_INFO_MAX          (BEACON_FIXED_MAX_FRAGMENTS * BEACON_FIXED_FRAGMENT_BYTES)
#define BEACON_FIXED_ICON_PALETTE_OFF  0x000u
#define BEACON_FIXED_ICON_TILES_OFF    0x020u
#define BEACON_FIXED_ICON_END          0x220u
#define BEACON_FIXED_COLOR_OFF         0x220u
#define BEACON_FIXED_HOST_NAME_OFF     0x222u
#define BEACON_FIXED_MAX_PLAYERS_OFF   0x236u
#define BEACON_FIXED_TITLE_OFF         0x238u
#define BEACON_FIXED_DESCRIPTION_OFF   0x298u
#define BEACON_FIXED_END               0x358u
#define BEACON_FIXED_NO_ICON_COLOR_OFF 0x000u
#define BEACON_FIXED_NO_ICON_HOST_NAME_OFF 0x002u
#define BEACON_FIXED_NO_ICON_MAX_PLAYERS_OFF 0x016u
#define BEACON_FIXED_NO_ICON_TITLE_OFF 0x018u
#define BEACON_FIXED_NO_ICON_DESCRIPTION_OFF 0x078u
#define BEACON_FIXED_NO_ICON_END       0x138u
#define BEACON_FIXED_NO_ICON_MAX_FRAGMENTS 4u
#define BEACON_VOLATILE_BASE_OFF       0x14u
#define BEACON_VOLATILE_ACTIVE_MASK_OFF 0x0cu
#define BEACON_VOLATILE_REMOVE_MASK_OFF 0x0eu
#define BEACON_VOLATILE_MEMBER_OFF     0x10u
#define BEACON_VOLATILE_MEMBER_COUNT   4u

/**
 * @brief Checks whether a bounded vendor-IE view contains a field.
 *
 * @param payload Vendor-IE payload after the 4-byte OUI/type prefix.
 * @param payload_len Payload length in bytes.
 * @param offset Field offset within payload.
 * @param size Field size in bytes.
 * @return true when the field is fully present; otherwise false.
 */
static inline bool beacon_ie_has(const u8 *payload, unsigned payload_len,
                                    unsigned offset, unsigned size) {
    return payload && offset <= payload_len && size <= payload_len - offset;
}

/**
 * @brief Reads the data-attribute bits from the file/session byte.
 *
 * @param file_session Raw file/session byte from the vendor IE.
 * @return Data attribute encoded in the low two bits.
 */
static inline BeaconDataAttr beacon_data_attr_from_file_session(u8 file_session) {
    return (BeaconDataAttr)(file_session & 3u);
}

/**
 * @brief Computes the one's-complement checksum used by beacon vendor data.
 *
 * @param payload Bytes to checksum.
 * @param payload_len Number of payload bytes.
 * @return One's-complement checksum result.
 */
static inline u16 beacon_ie_checksum16(const u8 *payload, unsigned payload_len) {
    u32 sum = 0;
    while (payload_len > 1u) {
        sum += (u16)payload[0] | ((u16)payload[1] << 8);
        payload += 2;
        payload_len -= 2u;
    }
    if (payload_len) sum += *payload;
    while (sum >> 16) sum = (sum & 0xffffu) + (sum >> 16);
    return (u16)(sum ^ 0xffffu);
}

/**
 * @brief Checks whether the vendor-IE type byte marks DS Download Play data.
 *
 * @param payload Vendor-IE payload after the 4-byte OUI/type prefix.
 * @param payload_len Payload length in bytes.
 * @return true when the type low bits identify multiboot data.
 */
static inline bool beacon_ie_type_is_multiboot(const u8 *payload, unsigned payload_len) {
    return beacon_ie_has(payload, payload_len, BEACON_VENDOR_IE_TYPE_OFF, 1u) &&
           ((payload[BEACON_VENDOR_IE_TYPE_OFF] & 3u) == 3u);
}

/**
 * @brief Checks the bounded checksum region in a vendor-IE payload.
 *
 * @param payload Vendor-IE payload after the 4-byte OUI/type prefix.
 * @param payload_len Payload length in bytes.
 * @return true when the checksum field is present and valid.
 */
static inline bool beacon_ie_checksum_ok(const u8 *payload, unsigned payload_len) {
    return beacon_ie_has(payload, payload_len, BEACON_VENDOR_IE_CHECKSUM_OFF,
                            BEACON_VENDOR_IE_CHECKSUM_LEN) &&
           beacon_ie_checksum16(payload + BEACON_VENDOR_IE_CHECKSUM_OFF,
                                   BEACON_VENDOR_IE_CHECKSUM_LEN) == 0;
}

/**
 * @brief Parses a bounded Nintendo DS Download Play vendor-IE payload.
 *
 * `payload` starts after the 4-byte vendor OUI/type prefix. The returned view
 * borrows pointers into `payload`; callers must keep the source buffer alive
 * while using the view.
 *
 * @return true if the payload is structurally parseable as DS Download Play data.
 *         `out->checksum_ok` separately reports checksum validity.
 */
bool beacon_ie_parse(BeaconIeView *out, const u8 *payload, unsigned payload_len);

/**
 * @brief Returns the metadata layout used by a fixed beacon attribute.
 *
 * DS Download Play parents omit `MBIconInfo` for Fixed-No-Icon beacons and
 * start the transmitted byte stream at `MBGameInfoFixed.parent`.
 */
const FixedMetadataLayout *beacon_fixed_metadata_layout(BeaconDataAttr attr);

/**
 * @brief Normalizes the parent packet-size field from a beacon.
 *
 * Raw value 0 means "not specified" and maps to PARENT_MAX_DEFAULT. Non-zero
 * values that cannot hold the six-byte protocol overhead are rejected.
 */
bool beacon_parent_packet_max_normalize(u16 raw, u16 *out);
