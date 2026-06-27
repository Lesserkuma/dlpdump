/**
 * @file scan_meta.c
 * @brief Builds scan content IDs, repeat-download history and member names.
 */
#include "scan_internal.h"
#include "../../common/hash_common.h"
#include "../../common/handover_bss.h"

/**
 * @brief Adds one byte value to a scan metadata hash.
 */
static u32 hash_u8(u8 v, u32 h) {
    return fnv1a32_update(h, &v, sizeof(v));
}

/**
 * @brief Adds a 32-bit little-endian value to a scan metadata hash.
 */
static u32 hash_u32_le(u32 v, u32 h) {
    const u8 bytes[4] = {
        (u8)v,
        (u8)(v >> 8),
        (u8)(v >> 16),
        (u8)(v >> 24),
    };
    return fnv1a32_update(h, bytes, sizeof(bytes));
}

/**
 * @brief Adds a bounded UCS-2 text field to a scan metadata hash.
 */
static u32 hash_ucs2_text_field(const u8 *fixed, unsigned fixed_len, unsigned off, unsigned max_units, u32 h) {
    unsigned available = 0;
    if (fixed && off < fixed_len) {
        available = (fixed_len - off) / 2u;
        if (available > max_units) available = max_units;
    }

    unsigned units = 0;
    while (units < available) {
        u16 c = le16(fixed + off + units * 2u);
        if (!c || c == 0xffff) break;
        units++;
    }

    h = hash_u32_le(units, h);
    if (units) h = fnv1a32_update(h, fixed + off, units * 2u);
    return h;
}

/**
 * @brief Builds a provisional content ID before fixed metadata is complete.
 */
u32 scan_make_provisional_id(const Arm7BssEvent *ev) {
    u32 h = FNV1A32_BASIS;
    h = fnv1a32_update(h, ev->bss.bssid, HANDOVER_BSS_BSSID_BYTES);
    h = hash_u32_le(ev->game_group_id, h);
    h = hash_u32_le(ev->game_id, h);
    h = hash_u8(ev->file_no, h);
    return h ? h : 1;
}

/**
 * @brief Builds the stable content ID for a completed scan slot.
 */
u32 scan_make_content_id(const ContentSlot *s, const u8 *fixed, unsigned fixed_len) {
    const FixedMetadataLayout *layout =
        beacon_fixed_metadata_layout((BeaconDataAttr)s->beacon_data_attr);
    unsigned title_off = layout ? layout->title_off : BEACON_FIXED_TITLE_OFF;
    unsigned description_off = layout ? layout->description_off : BEACON_FIXED_DESCRIPTION_OFF;
    u32 h = FNV1A32_BASIS;
    h = fnv1a32_update(h, s->bss.bssid, HANDOVER_BSS_BSSID_BYTES);
    h = hash_u32_le(s->game_group_id, h);
    h = hash_u32_le(s->game_id, h);
    h = hash_u8(s->file_no, h);
    h = hash_ucs2_text_field(fixed, fixed_len, title_off, TITLE_CHARS, h);
    h = hash_ucs2_text_field(fixed, fixed_len, description_off, DESCRIPTION_CHARS, h);
    return h ? h : 1;
}

/**
 * @brief Returns whether a content ID is already in the download history.
 */
static bool content_id_downloaded(u32 id) {
    for (unsigned i = 0; i < s_downloaded_id_count; i++) {
        if (s_downloaded_ids[i] == id) return true;
    }
    return false;
}

/**
 * @brief Records a completed content ID in the repeat-download history.
 */
void scan_remember_downloaded_id(u32 id) {
    if (!id || content_id_downloaded(id)) return;
    if (s_downloaded_id_count < DOWNLOADED_ID_CAPACITY) {
        s_downloaded_ids[s_downloaded_id_count++] = id;
        return;
    }
    memmove(s_downloaded_ids, s_downloaded_ids + 1, sizeof(s_downloaded_ids[0]) * (DOWNLOADED_ID_CAPACITY - 1u));
    s_downloaded_ids[DOWNLOADED_ID_CAPACITY - 1u] = id;
}

/**
 * @brief Replaces a provisional slot ID with its final metadata-based ID.
 */
void scan_set_final_content_id(ContentSlot *s, u32 id) {
    if (!s) return;
    if (!id) id = 1;
    if (s->id != id) s->tried = false;
    s->id = id;
    s->downloaded = content_id_downloaded(id);
}

/**
 * @brief Returns the current scan timestamp in seconds.
 */
u32 scan_now_seconds(void) {
    time_t t = time(NULL);
    return t > 0 ? (u32)t : 0;
}

/**
 * @brief Returns whether repeat-download suppression is still active.
 */
bool scan_repeat_download_block_active(void) {
    u32 now = scan_now_seconds();
    return now && s_repeat_download_block_until && now < s_repeat_download_block_until;
}

/**
 * @brief Returns whether automatic download starts are paused after cancellation.
 */
bool scan_download_start_block_active(void) {
    u32 now = scan_now_seconds();
    return now && s_download_start_block_until && now < s_download_start_block_until;
}

/**
 * @brief Generates the next pseudo-random scan-slot pick value.
 */
u32 scan_next_pick_random(void) {
    u32 x = s_pick_rng;
    if (!x) {
        x = scan_now_seconds();
        x ^= g_frameCounter * 747796405u;
        if (!x) x = 0x6d2b79f5u;
    }
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_pick_rng = x ? x : 0x6d2b79f5u;
    return s_pick_rng;
}

/**
 * @brief Decodes one member-name metadata fragment into UTF-8.
 */
void scan_decode_member_name(const u8 *frag, char *dst, size_t dst_size) {
    if (!dst_size) return;
    dst[0] = 0;
    if (!frag) return;

    unsigned units = frag[1];
    if (units > USER_NAME_CHARS) units = USER_NAME_CHARS;
    size_t out = 0;
    for (unsigned i = 0; i < units; i++) {
        u16 c = le16(frag + 2 + i * 2);
        if (!c || c == 0xffff) break;
        if (c < 0x20 || (c >= 0xd800 && c <= 0xdfff)) c = '?';
        (void)text_append_codepoint(dst, dst_size, &out, c);
    }
    dst[out] = 0;
}

/**
 * @brief Returns the expected metadata-fragment count for a beacon attribute.
 */
static unsigned metadata_fragment_count(const ContentSlot *s) {
    unsigned count = BEACON_FIXED_MAX_FRAGMENTS;
    if (s && s->fragment_len[0] >= BEACON_FRAGMENT_HEADER_BYTES &&
        s->fragments[0][1] > 0 &&
        s->fragments[0][1] <= BEACON_FIXED_MAX_FRAGMENTS) {
        count = s->fragments[0][1];
    }
    return count;
}

/**
 * @brief Returns whether fixed and member-name metadata are complete.
 */
bool scan_metadata_complete(const ContentSlot *s) {
    unsigned count = metadata_fragment_count(s);
    if (!count || count > BEACON_FIXED_MAX_FRAGMENTS) return false;
    u16 need = (u16)((1u << count) - 1u);
    if ((s->fragment_mask & need) != need) return false;
    for (unsigned i = 0; i < count; i++) {
        if (s->fragment_len[i] < BEACON_FRAGMENT_HEADER_BYTES) return false;
        unsigned len = s->fragments[i][2];
        unsigned stored = s->fragment_len[i] - BEACON_FRAGMENT_HEADER_BYTES;
        if (len > BEACON_FIXED_FRAGMENT_BYTES || len > stored) return false;
        if (i + 1 < count && len != BEACON_FIXED_FRAGMENT_BYTES) return false;
        if (i + 1 == count && len == 0) return false;
    }
    return true;
}

/**
 * @brief Updates the BCN handover prefix from the latest beacon metadata.
 */
void scan_update_handover_prefix(ContentSlot *s, const Arm7BssEvent *ev) {
    if (!s || !ev) return;

    u8 out[HANDOVER_BSS_SIZE];
    u16 capabilities = ev->bss.ieee_caps;
    u16 beacon_period = 0;
    u16 dtim_period = 0;
    u16 channel = ev->bss.channel;

    if (ev->beacon_frame_len >= DOT11_HDR_SIZE + BEACON_BODY_SIZE) {
        const u8 *frame = ev->beacon_frame;
        unsigned len = ev->beacon_frame_len;
        const u8 *body = frame + DOT11_HDR_SIZE;

        beacon_period = le16(body + 8);
        capabilities = le16(body + 10);

        for (unsigned off = DOT11_HDR_SIZE + BEACON_BODY_SIZE; off + 2 <= len;) {
            u8 id = frame[off++];
            u8 ie_len = frame[off++];
            if (off + ie_len > len) break;

            switch (id) {
                case IE_DS_PARAM_SET:
                    if (ie_len >= 1) channel = frame[off];
                    break;
                case IE_TIM:
                    if (ie_len >= 2) dtim_period = frame[off + 1];
                    break;
                default:
                    break;
            }

            off += ie_len;
        }
    }

    handover_bss_write(out, ev->bss.rssi, ev->bss.bssid,
                          ev->game_group_id, ev->temporary_group_id,
                          capabilities, ev->bss.ieee_basic_rates,
                          ev->bss.ieee_all_rates, beacon_period,
                          dtim_period, channel);

    memcpy(s->handover, out, sizeof(s->handover));
    s->handover_valid = true;
}
