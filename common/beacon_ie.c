/**
 * @file beacon_ie.c
 * @brief Parses Nintendo DS Download Play beacon vendor-IE payloads.
 */
#include "beacon_ie.h"
#include "protocol.h"

#include <string.h>

static const FixedMetadataLayout s_fixed_normal_layout = {
    true,
    BEACON_FIXED_COLOR_OFF,
    BEACON_FIXED_HOST_NAME_OFF,
    BEACON_FIXED_MAX_PLAYERS_OFF,
    BEACON_FIXED_TITLE_OFF,
    BEACON_FIXED_DESCRIPTION_OFF,
    BEACON_FIXED_END,
    BEACON_FIXED_MAX_FRAGMENTS,
};

static const FixedMetadataLayout s_fixed_no_icon_layout = {
    false,
    BEACON_FIXED_NO_ICON_COLOR_OFF,
    BEACON_FIXED_NO_ICON_HOST_NAME_OFF,
    BEACON_FIXED_NO_ICON_MAX_PLAYERS_OFF,
    BEACON_FIXED_NO_ICON_TITLE_OFF,
    BEACON_FIXED_NO_ICON_DESCRIPTION_OFF,
    BEACON_FIXED_NO_ICON_END,
    BEACON_FIXED_NO_ICON_MAX_FRAGMENTS,
};

/**
 * @brief Returns the fixed-metadata byte layout for a beacon attribute.
 */
const FixedMetadataLayout *beacon_fixed_metadata_layout(BeaconDataAttr attr) {
    switch (attr) {
        case BEACON_DATA_ATTR_FIXED_NORMAL:
            return &s_fixed_normal_layout;
        case BEACON_DATA_ATTR_FIXED_NO_ICON:
            return &s_fixed_no_icon_layout;
        default:
            return NULL;
    }
}

/** @brief Checks whether a beacon data attribute carries fixed metadata. */
static bool attr_is_fixed(BeaconDataAttr attr) {
    return attr == BEACON_DATA_ATTR_FIXED_NORMAL ||
           attr == BEACON_DATA_ATTR_FIXED_NO_ICON;
}

/** @brief Parses and bounds-checks one fixed beacon fragment. */
static void parse_fixed_fragment(BeaconIeView *view) {
    const FixedMetadataLayout *layout = beacon_fixed_metadata_layout(view->data_attr);
    if (!layout) return;
    unsigned flen = view->payload_len > BEACON_VENDOR_IE_FRAGMENT_OFF
        ? view->payload_len - BEACON_VENDOR_IE_FRAGMENT_OFF
        : 0;
    if (flen < BEACON_FRAGMENT_HEADER_BYTES) return;

    const u8 *frag = view->payload + BEACON_VENDOR_IE_FRAGMENT_OFF;
    unsigned frag_idx = frag[0];
    unsigned frag_total = frag[1];
    unsigned data_len = frag[2];

    if (frag_idx >= SNIPPET_COUNT || frag_total == 0 ||
        frag_total > layout->fragment_count ||
        frag_idx >= frag_total) {
        return;
    }
    if (data_len > BEACON_FIXED_FRAGMENT_BYTES ||
        data_len > flen - BEACON_FRAGMENT_HEADER_BYTES) {
        return;
    }
    if (frag_total != layout->fragment_count) return;
    if (frag_idx + 1u < frag_total && data_len != BEACON_FIXED_FRAGMENT_BYTES) return;
    if (frag_idx + 1u == frag_total && data_len == 0) return;

    view->fixed_fragment_valid = true;
    view->fragment = frag;
    view->fragment_len = BEACON_FRAGMENT_HEADER_BYTES + data_len;
    view->fragment_index = frag_idx;
    view->fragment_total = frag_total;
    view->fragment_data_len = data_len;
}

/** @brief Parses the bounded Nintendo DS Download Play vendor-IE payload. */
bool beacon_ie_parse(BeaconIeView *out, const u8 *payload, unsigned payload_len) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!payload || payload_len < BEACON_VENDOR_IE_MIN_BYTES) return false;
    if (!beacon_ie_type_is_multiboot(payload, payload_len)) return false;
    if (!beacon_ie_has(payload, payload_len, BEACON_VENDOR_IE_FILE_SESSION_OFF, 1u)) return false;

    out->payload = payload;
    out->payload_len = payload_len;
    out->data_attr = beacon_data_attr_from_file_session(payload[BEACON_VENDOR_IE_FILE_SESSION_OFF]);
    out->checksum_ok = beacon_ie_checksum_ok(payload, payload_len);
    out->has_icon = out->data_attr == BEACON_DATA_ATTR_FIXED_NORMAL;

    if (attr_is_fixed(out->data_attr)) {
        parse_fixed_fragment(out);
    } else if (out->data_attr == BEACON_DATA_ATTR_VOLATILE &&
               beacon_ie_has(payload, payload_len, BEACON_VOLATILE_BASE_OFF, 1u)) {
        out->volatile_payload = payload + BEACON_VOLATILE_BASE_OFF;
        out->volatile_payload_len = payload_len - BEACON_VOLATILE_BASE_OFF;
    }
    return true;
}

/** @brief Normalizes the parent packet-size field advertised by a beacon. */
bool beacon_parent_packet_max_normalize(u16 raw, u16 *out) {
    if (!out) return false;
    if (raw == 0) {
        *out = PARENT_MAX_DEFAULT;
        return true;
    }
    if (raw <= 6u || raw > PARENT_MAX_DEFAULT) return false;
    *out = raw;
    return true;
}
