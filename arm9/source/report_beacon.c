/**
 * @file report_beacon.c
 * @brief Writes beacon, Nintendo vendor-IE and fixed-metadata sections of dump reports.
 */
#include "report_internal.h"

/**
 * @brief Returns the UI/report label for a firmware favorite-color index.
 */
static const char *favorite_color_name(u8 color) {
    static const char *favorite_color_names[16] = {
        "Gray", "Brown", "Red", "Pink", "Orange", "Yellow", "Lime Green", "Green",
        "Dark Green", "Sea Green", "Turquoise", "Blue", "Dark Blue", "Purple", "Violet", "Magenta",
    };
    return color < 16 ? favorite_color_names[color] : NULL;
}

/**
 * @brief Computes the one-complement beacon checksum used in reports.
 */
u16 report_checksum16_ones_complement(const u8 *p, unsigned len) {
    u32 sum = 0;
    while (len > 1) {
        sum += (u16)p[0] | ((u16)p[1] << 8);
        p += 2;
        len -= 2;
    }
    if (len) sum += *p;
    while (sum >> 16) sum = (sum & 0xffffu) + (sum >> 16);
    return (u16)(sum ^ 0xffffu);
}

/**
 * @brief Extracts the beacon interval in 802.11 time units.
 */
u16 report_beacon_interval_tu(const ContentSlot *slot, bool *changed) {
    u16 first = 0;
    if (changed) *changed = false;
    if (!slot) return 0;
    for (unsigned i = 0; i < SNIPPET_COUNT; i++) {
        if (slot->beacon_frame_len[i] < DOT11_HDR_SIZE + 12u) continue;
        u16 v = le16(slot->beacon_frames[i] + DOT11_HDR_SIZE + 8u);
        if (!first) first = v;
        else if (changed && v != first) *changed = true;
    }
    return first;
}

/**
 * @brief Finds the Nintendo vendor IE inside a raw beacon frame.
 */
static bool find_nintendo_beacon_ie_in_frame(const u8 *frame, unsigned len, u8 out[4],
                                             const u8 **nin, unsigned *vendor_ie_len) {
    static const u8 nintendo_vendor_ie_oui[4] = { 0x00, 0x09, 0xbf, 0x00 };
    if (!frame || len < DOT11_HDR_SIZE + 12u) return false;
    for (unsigned off = DOT11_HDR_SIZE + 12u; off + 2u <= len;) {
        u8 id = frame[off++];
        u8 ie_len = frame[off++];
        if (off + ie_len > len) break;
        if (id == 0xdd && ie_len >= 4 &&
            memcmp(frame + off, nintendo_vendor_ie_oui, sizeof(nintendo_vendor_ie_oui)) == 0) {
            if (out) memcpy(out, frame + off, 4);
            if (nin) *nin = frame + off + 4u;
            if (vendor_ie_len) *vendor_ie_len = ie_len - 4u;
            return true;
        }
        off += ie_len;
    }
    return false;
}

/**
 * @brief Selects the best Nintendo vendor IE source for a slot.
 */
static bool find_nintendo_beacon_ie(const ContentSlot *slot, u8 out[4],
                                    const u8 **nin, unsigned *vendor_ie_len) {
    if (!slot) return false;
    for (unsigned i = 0; i < SNIPPET_COUNT; i++) {
        const u8 *frame = slot->beacon_frames[i];
        unsigned len = slot->beacon_frame_len[i];
        if (find_nintendo_beacon_ie_in_frame(frame, len, out, nin, vendor_ie_len)) return true;
    }
    return false;
}

/**
 * @brief Returns whether a Nintendo vendor IE contains a byte range.
 */
static bool nin_has(const u8 *nin, unsigned vendor_ie_len, unsigned off, unsigned size) {
    return beacon_ie_has(nin, vendor_ie_len, off, size);
}

/**
 * @brief Returns the preferred Nintendo vendor IE payload for report fields.
 */
static bool slot_preferred_nin(const ContentSlot *slot, u8 oui[4],
                               const u8 **nin, unsigned *vendor_ie_len) {
    if (!slot || !nin || !vendor_ie_len) return false;
    bool have_oui = find_nintendo_beacon_ie(slot, oui, nin, vendor_ie_len);
    if (slot->nin_sample_len) {
        *nin = slot->nin_sample;
        *vendor_ie_len = slot->nin_sample_len;
    }
    return have_oui || slot->nin_sample_len != 0;
}

/**
 * @brief Returns whether a vendor IE advertises DS Download Play metadata.
 */
static bool beacon_is_download_play(const u8 *nin, unsigned vendor_ie_len) {
    BeaconIeView view;
    return beacon_ie_parse(&view, nin, vendor_ie_len);
}

/**
 * @brief Returns whether the Nintendo vendor IE checksum is valid.
 */
static bool beacon_nin_checksum_ok(const u8 *nin, unsigned vendor_ie_len) {
    BeaconIeView view;
    return beacon_ie_parse(&view, nin, vendor_ie_len) && view.checksum_ok;
}

/**
 * @brief Reports mismatch between Nintendo-IE and beacon GGID fields.
 */
bool report_beacon_game_group_ids_mismatch(const ContentSlot *slot,
                                              u32 *ie_game_group_id,
                                              u32 *beacon_game_group_id) {
    const u8 *nin = NULL;
    unsigned vendor_ie_len = 0;
    slot_preferred_nin(slot, NULL, &nin, &vendor_ie_len);
    if (!beacon_is_download_play(nin, vendor_ie_len)) return false;
    if (!nin_has(nin, vendor_ie_len, BEACON_VENDOR_IE_GGID_OFF, 4u) ||
        !nin_has(nin, vendor_ie_len, BEACON_VENDOR_IE_MB_GAME_ID_OFF, 4u)) {
        return false;
    }
    u32 ie_ggid = le32(nin + BEACON_VENDOR_IE_GGID_OFF);
    u32 beacon_ggid = le32(nin + BEACON_VENDOR_IE_MB_GAME_ID_OFF);
    if (ie_game_group_id) *ie_game_group_id = ie_ggid;
    if (beacon_game_group_id) *beacon_game_group_id = beacon_ggid;
    return ie_ggid != beacon_ggid;
}

/**
 * @brief Writes beacon checksum status lines to the report.
 */
int report_beacon_checksum_status(const ContentSlot *slot) {
    if (!slot) return -1;
    bool found = false;
    bool invalid = false;
    for (unsigned i = 0; i < SNIPPET_COUNT; i++) {
        const u8 *nin = NULL;
        unsigned vendor_ie_len = 0;
        if (!find_nintendo_beacon_ie_in_frame(slot->beacon_frames[i], slot->beacon_frame_len[i],
                                             NULL, &nin, &vendor_ie_len)) {
            continue;
        }
        if (!beacon_is_download_play(nin, vendor_ie_len)) continue;
        found = true;
        if (!beacon_nin_checksum_ok(nin, vendor_ie_len)) {
            invalid = true;
        }
    }
    if (!found) return -1;
    return invalid ? 0 : 1;
}

/**
 * @brief Returns the expected metadata-fragment count for a beacon attribute.
 */
static unsigned metadata_fragment_count(const ContentSlot *slot) {
    unsigned count = BEACON_FIXED_MAX_FRAGMENTS;
    if (slot && slot->fragment_len[0] >= BEACON_FRAGMENT_HEADER_BYTES &&
        slot->fragments[0][1] > 0 &&
        slot->fragments[0][1] <= BEACON_FIXED_MAX_FRAGMENTS) {
        count = slot->fragments[0][1];
    }
    return count;
}

/**
 * @brief Counts set bits in a 16-bit mask.
 */
static unsigned popcount16(u16 v) {
    unsigned n = 0;
    while (v) {
        n += v & 1u;
        v >>= 1;
    }
    return n;
}

/**
 * @brief Returns whether a fixed metadata fragment can be trusted.
 */
static bool fixed_data_fragment_valid(const u8 *nin, unsigned vendor_ie_len, unsigned total, unsigned *idx) {
    BeaconIeView view;
    if (!beacon_ie_parse(&view, nin, vendor_ie_len) || !view.fixed_fragment_valid) return false;
    if (view.data_attr != BEACON_DATA_ATTR_FIXED_NORMAL &&
        view.data_attr != BEACON_DATA_ATTR_FIXED_NO_ICON) {
        return false;
    }
    if (view.fragment_total != total) return false;

    if (idx) *idx = view.fragment_index;
    return true;
}

/**
 * @brief Formats metadata-fragment completeness for the report.
 */
static void beacon_data_snippets_status(const ContentSlot *slot, char *out, size_t out_size) {
    if (!out_size) return;
    unsigned total = metadata_fragment_count(slot);
    u16 valid_mask = 0;

    if (slot) {
        for (unsigned i = 0; i < SNIPPET_COUNT; i++) {
            const u8 *nin = NULL;
            unsigned vendor_ie_len = 0;
            if (!find_nintendo_beacon_ie_in_frame(slot->beacon_frames[i], slot->beacon_frame_len[i],
                                                 NULL, &nin, &vendor_ie_len)) {
                continue;
            }
            if (!beacon_is_download_play(nin, vendor_ie_len) || !beacon_nin_checksum_ok(nin, vendor_ie_len)) continue;

            unsigned idx = 0;
            if (fixed_data_fragment_valid(nin, vendor_ie_len, total, &idx)) {
                valid_mask |= (u16)(1u << idx);
            }
        }
    }

    u16 need = (u16)((1u << total) - 1u);
    unsigned found = popcount16(valid_mask & need);
    snprintf(out, out_size, "%s (%u/%u)", found == total ? "OK" : "Incomplete", found, total);
}

/**
 * @brief Cached pointers and parsed state used while writing the beacon section.
 */
typedef struct {
    const ContentSlot *slot;
    const FixedMetadataLayout *fixed_layout;
    const u8 *nin;
    unsigned vendor_ie_len;
    u8 oui[4];
    BeaconIeView beacon_view;
    bool have_oui;
    bool have_beacon_view;
} BeaconReportCtx;

/**
 * @brief Collects the slot and vendor-IE pointers used by beacon reporting.
 */
static void init_beacon_report_ctx(BeaconReportCtx *ctx, const ContentSlot *slot) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->slot = slot;
    ctx->fixed_layout =
        beacon_fixed_metadata_layout((BeaconDataAttr)slot->beacon_data_attr);
    ctx->have_oui = find_nintendo_beacon_ie(slot, ctx->oui, &ctx->nin, &ctx->vendor_ie_len);
    if (slot->nin_sample_len) {
        ctx->nin = slot->nin_sample;
        ctx->vendor_ie_len = slot->nin_sample_len;
    }
    ctx->have_beacon_view = beacon_ie_parse(&ctx->beacon_view, ctx->nin, ctx->vendor_ie_len);
}

/**
 * @brief Writes host and fixed-title metadata lines to the report.
 */
static void report_beacon_host_fields(FILE *f, const BeaconReportCtx *ctx,
                                      const u8 *fixed, unsigned fixed_len) {
    const ContentSlot *slot = ctx->slot;
    report_text_field(f, "Game Title:", slot->title);
    report_text_field(f, "Game Description:", slot->description);
    report_kv(f, "Host Username:", "%s", slot->host_name[0] ? slot->host_name : "N/A");
    if (ctx->fixed_layout && fixed_len > ctx->fixed_layout->color_off) {
        u8 color = fixed[ctx->fixed_layout->color_off] & 0x0f;
        const char *name = favorite_color_name(color);
        report_kv(f, "Host Favorite Color:", "%s", name ? name : "Unknown");
    } else {
        report_kv(f, "Host Favorite Color:", "N/A");
    }
    report_kv(f, "Host MAC Address:", "%02X:%02X:%02X:%02X:**:** (anonymized)",
               slot->bss.bssid[0], slot->bss.bssid[1], slot->bss.bssid[2], slot->bss.bssid[3]);
}

/**
 * @brief Writes beacon identity fields to the report.
 */
static void report_beacon_identity_fields(FILE *f, const BeaconReportCtx *ctx) {
    const ContentSlot *slot = ctx->slot;
    u16 interval = report_beacon_interval_tu(slot, NULL);
    char snippet_status[32];
    beacon_data_snippets_status(slot, snippet_status, sizeof(snippet_status));
    report_kv(f, "Beacon Data Snippets:", "%s", snippet_status);
    if (interval) {
        u32 ms_x1000 = (u32)interval * 1024u;
        report_kv(f, "Beacon Interval:", "%u TU / %lu.%03lu ms (0x%X)",
                  interval, (unsigned long)(ms_x1000 / 1000u),
                  (unsigned long)(ms_x1000 % 1000u), interval);
    } else {
        report_kv(f, "Beacon Interval:", "N/A");
    }
    if (ctx->have_oui) {
        report_kv(f, "Nintendo Beacon OUI:", "%02X-%02X-%02X-%02X",
                     ctx->oui[0], ctx->oui[1], ctx->oui[2], ctx->oui[3]);
    } else {
        report_kv(f, "Nintendo Beacon OUI:", "N/A");
    }
}

/**
 * @brief Writes Nintendo vendor IE fields to the report.
 */
static void report_beacon_vendor_fields(FILE *f, const BeaconReportCtx *ctx) {
    const ContentSlot *slot = ctx->slot;
    const u8 *nin = ctx->nin;
    unsigned vendor_ie_len = ctx->vendor_ie_len;
    if (nin_has(nin, vendor_ie_len, BEACON_VENDOR_IE_STEPPING_OFF, 2u)) report_kv(f, "Stepping Offset:", "0x%X", le16(nin + BEACON_VENDOR_IE_STEPPING_OFF));
    else report_kv(f, "Stepping Offset:", "N/A");
    if (nin_has(nin, vendor_ie_len, BEACON_VENDOR_IE_LCD_SYNC_OFF, 2u)) report_kv(f, "LCD Video Sync:", "0x%X", le16(nin + BEACON_VENDOR_IE_LCD_SYNC_OFF));
    else report_kv(f, "LCD Video Sync:", "N/A");
    if (nin_has(nin, vendor_ie_len, BEACON_VENDOR_IE_FIXED_ID_OFF, 4u)) report_kv(f, "Fixed ID:", "0x%08lX", (unsigned long)le32(nin + BEACON_VENDOR_IE_FIXED_ID_OFF));
    else report_kv(f, "Fixed ID:", "N/A");
    if (nin_has(nin, vendor_ie_len, BEACON_VENDOR_IE_GGID_OFF, 4u)) report_kv(f, "Game Group ID:", "0x%08lX", (unsigned long)le32(nin + BEACON_VENDOR_IE_GGID_OFF));
    else report_kv(f, "Game Group ID:", "0x%08lX", (unsigned long)slot->game_group_id);
    if (nin_has(nin, vendor_ie_len, BEACON_VENDOR_IE_TGID_OFF, 2u)) report_kv(f, "Temporary Group ID:", "0x%04X", le16(nin + BEACON_VENDOR_IE_TGID_OFF));
    else report_kv(f, "Temporary Group ID:", "0x%04X", slot->temporary_group_id);
    if (nin_has(nin, vendor_ie_len, BEACON_VENDOR_IE_DATA_LEN_OFF, 1u)) report_kv(f, "Data Length:", "0x%X", nin[BEACON_VENDOR_IE_DATA_LEN_OFF]);
    else report_kv(f, "Data Length:", "N/A");
    if (nin_has(nin, vendor_ie_len, BEACON_VENDOR_IE_TYPE_OFF, 1u)) report_kv(f, "Beacon Type:", "0x%X", nin[BEACON_VENDOR_IE_TYPE_OFF]);
    else report_kv(f, "Beacon Type:", "N/A");
}

/**
 * @brief Writes beacon packet-size and snippet fields to the report.
 */
static void report_beacon_packet_fields(FILE *f, const BeaconReportCtx *ctx) {
    const ContentSlot *slot = ctx->slot;
    const u8 *nin = ctx->nin;
    unsigned vendor_ie_len = ctx->vendor_ie_len;
    if (nin_has(nin, vendor_ie_len, BEACON_VENDOR_IE_PARENT_MAX_OFF, 2u)) report_kv(f, "Max Packet Size (P):", "%u bytes", le16(nin + BEACON_VENDOR_IE_PARENT_MAX_OFF));
    else report_kv(f, "Max Packet Size (P):", "%u bytes", slot->parent_packet_max_bytes ? slot->parent_packet_max_bytes : PARENT_MAX_DEFAULT);
    if (nin_has(nin, vendor_ie_len, BEACON_VENDOR_IE_CHILD_MAX_OFF, 2u)) report_kv(f, "Max Packet Size (C):", "%u bytes", le16(nin + BEACON_VENDOR_IE_CHILD_MAX_OFF));
    else report_kv(f, "Max Packet Size (C):", "%u bytes", slot->child_packet_max_bytes ? slot->child_packet_max_bytes : CHILD_MAX_DEFAULT);
    if (nin_has(nin, vendor_ie_len, BEACON_VENDOR_IE_FILE_SESSION_OFF, 2u)) {
        report_kv(f, "File No.:", "%u", nin[BEACON_VENDOR_IE_FILE_SESSION_OFF] >> 2);
        report_kv(f, "Session ID:", "%u", nin[BEACON_VENDOR_IE_FILE_SESSION_OFF + 1u]);
    } else {
        report_kv(f, "File No.:", "%u", slot->file_no);
        report_kv(f, "Session ID:", "%u", slot->session_id);
    }
}

/**
 * @brief Writes player-count fields to the report.
 */
static void report_beacon_player_fields(FILE *f, const BeaconReportCtx *ctx,
                                        const u8 *fixed, unsigned fixed_len) {
    const ContentSlot *slot = ctx->slot;
    u16 active_mask = slot->member_active_mask;
    unsigned connected = slot->connected_count ? slot->connected_count : popcount16(active_mask);
    report_kv(f, "Connected Players:", "%u", connected);
    report_kv(f, "Member Active Mask:", "0x%04X", active_mask);
    if (ctx->fixed_layout && fixed_len > ctx->fixed_layout->max_players_off) report_kv(f, "Max Players:", "%u", fixed[ctx->fixed_layout->max_players_off]);
    else report_kv(f, "Max Players:", "%u", slot->max_players ? slot->max_players : 16);
}

/**
 * @brief Writes the full beacon section of the text report.
 */
void report_beacon(FILE *f, const Download *dl, const u8 *fixed, unsigned fixed_len) {
    BeaconReportCtx ctx;
    init_beacon_report_ctx(&ctx, dl->slot);
    report_beacon_host_fields(f, &ctx, fixed, fixed_len);
    report_beacon_identity_fields(f, &ctx);
    report_beacon_vendor_fields(f, &ctx);
    report_beacon_packet_fields(f, &ctx);
    report_beacon_player_fields(f, &ctx, fixed, fixed_len);
}
