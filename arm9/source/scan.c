/**
 * @file scan.c
 * @brief Maintains visible DS Download Play content slots from beacon events.
 */
#include "scan_internal.h"

ContentSlot g_slots[CONTENT_SLOT_COUNT];

u32 s_downloaded_ids[DOWNLOADED_ID_CAPACITY];
unsigned s_downloaded_id_count;
u32 s_repeat_download_block_until;
u32 s_download_start_block_until;
u32 s_pick_rng;

/**
 * @brief Returns whether the slot has every beacon frame needed for its layout.
 */
bool scan_beacon_frames_complete(const ContentSlot *s) {
    if (!s) return false;
    const FixedMetadataLayout *layout =
        beacon_fixed_metadata_layout((BeaconDataAttr)s->beacon_data_attr);
    unsigned count = layout ? layout->fragment_count : SNIPPET_COUNT;
    const u16 need = (u16)((1u << count) - 1u);
    if ((s->beacon_frame_mask & need) != need) return false;
    for (unsigned i = 0; i < count; i++) {
        if (s->beacon_frame_len[i] < DOT11_HDR_SIZE) return false;
    }
    return true;
}

/**
 * @brief Clears stored raw beacon frames for a scan slot.
 */
static void clear_beacon_frames(ContentSlot *s) {
    s->beacon_frame_mask = 0;
    memset(s->beacon_frame_len, 0, sizeof(s->beacon_frame_len));
    memset(s->beacon_frame_ts_us, 0, sizeof(s->beacon_frame_ts_us));
}

/**
 * @brief Clears stored member-name metadata fragments for a scan slot.
 */
static void clear_member_fragments(ContentSlot *s) {
    s->member_active_mask = 0;
    s->member_name_mask = 0;
    s->volatile_counter = 0;
    memset(s->member_fragments, 0, sizeof(s->member_fragments));
    memset(s->member_names, 0, sizeof(s->member_names));
}

/**
 * @brief Clears decoded metadata while preserving slot allocation.
 */
static void reset_slot_metadata(ContentSlot *s) {
    memset(s->fragment_len, 0, sizeof(s->fragment_len));
    memset(s->fragments, 0, sizeof(s->fragments));
    s->fragment_mask = 0;
    clear_beacon_frames(s);
    s->complete = false;
    s->info_valid = false;
    s->icon_valid = false;
    s->max_players = 0;
    s->connected_count = 0;
    clear_member_fragments(s);
}

/**
 * @brief Stores one bounded raw beacon frame in a slot by snippet number.
 */
static void store_beacon_frame(ContentSlot *s, const Arm7BssEvent *ev, u32 timestamp_us) {
    if (!s || !ev || ev->snippet_no >= SNIPPET_COUNT) return;
    if (ev->beacon_frame_len < DOT11_HDR_SIZE || ev->beacon_frame_len > MAX_BEACON_FRAME) return;
    unsigned idx = ev->snippet_no;
    s->beacon_frame_len[idx] = ev->beacon_frame_len;
    s->beacon_frame_ts_us[idx] = timestamp_us;
    memcpy(s->beacon_frames[idx], ev->beacon_frame, ev->beacon_frame_len);
    s->beacon_frame_mask |= (u16)(1u << idx);
}

/**
 * @brief Decodes fixed metadata into the scan slot display fields.
 */
static bool decode_slot_info(ContentSlot *s) {
    u8 fixed[BEACON_FIXED_INFO_MAX];
    unsigned fixed_len = 0;
    s->host_name[0] = 0; s->title[0] = 0; s->description[0] = 0;
    s->icon_valid = false; s->info_valid = false;
    if (!meta_build_fixed_info(s, fixed, &fixed_len)) return false;
    const FixedMetadataLayout *layout =
        beacon_fixed_metadata_layout((BeaconDataAttr)s->beacon_data_attr);
    if (layout && fixed_len > layout->max_players_off) {
        u8 max_players = fixed[layout->max_players_off];
        s->max_players = (max_players >= 1 && max_players <= 16) ? max_players : 16;
    }
    s->info_valid = meta_decode_fixed_info((BeaconDataAttr)s->beacon_data_attr,
                                          fixed, fixed_len,
                                          s->host_name, sizeof(s->host_name),
                                          s->title, sizeof(s->title),
                                          s->description, sizeof(s->description),
                                          s->icon, &s->icon_valid);
    if (s->info_valid) scan_set_final_content_id(s, scan_make_content_id(s, fixed, fixed_len));
    return s->info_valid;
}

/**
 * @brief Writes a debug summary for a newly updated scan slot.
 */
static void log_slot_info(ContentSlot *s) {
    if (!s->info_valid) {
        ui_log("[%08lx] Beacon metadata decode failed.", (unsigned long)s->id);
        return;
    }
    ui_log("[%08lx] Found \"%s\".", (unsigned long)s->id, s->title[0] ? s->title : "Untitled");
    ui_log("[%08lx] GGID: %08lx, TGID: %04x, File: %d",
            (unsigned long)s->id,
            (unsigned long)s->game_group_id,
            s->temporary_group_id,
            s->file_no);
    if (!g_repeatDownloads && s->downloaded) {
        ui_log("[%08lx] Content already downloaded.", (unsigned long)s->id);
    }
}

/**
 * @brief Clears all scan slots and repeat-download history.
 */
void scan_reset(void) {
    memset(g_slots, 0, sizeof(g_slots));
    memset(s_downloaded_ids, 0, sizeof(s_downloaded_ids));
    s_downloaded_id_count = 0;
    s_repeat_download_block_until = 0;
    s_download_start_block_until = 0;
    s_pick_rng = 0;
}

/**
 * @brief Finds an existing scan slot by identity or allocates an empty one.
 */
static ContentSlot *find_or_alloc(const Arm7BssEvent *ev) {
    ContentSlot *free_slot = NULL;
    for (unsigned i = 0; i < CONTENT_SLOT_COUNT; i++) {
        ContentSlot *s = &g_slots[i];
        if (!s->used) {
            if (!free_slot) free_slot = s;
            continue;
        }
        if (s->game_group_id == ev->game_group_id && s->game_id == ev->game_id &&
            s->file_no == ev->file_no &&
            memcmp(s->bss.bssid, ev->bss.bssid, 6) == 0) return s;
    }
    return free_slot;
}

/**
 * @brief Merges volatile beacon member-name fragments into a scan slot.
 *
 * The volatile counter and active/remove masks decide when cached fragments are
 * stale. Updated fragments are decoded into UTF-8 names used by the UI report,
 * while inactive members are cleared immediately to avoid stale player lists.
 */
static void update_member_fragments(ContentSlot *s, const Arm7BssEvent *ev) {
    if (!s || !ev || ev->vendor_ie_len < BEACON_VOLATILE_BASE_OFF + BEACON_VOLATILE_MEMBER_OFF) return;
    const u8 *cur = ev->vendor_ie_payload + BEACON_VOLATILE_BASE_OFF;
    u8 old_counter = s->volatile_counter;
    u8 new_counter = ev->connected_count;
    u16 active_mask = 0;
    u16 remove_mask = 0;

    if (ev->vendor_ie_len >= BEACON_VOLATILE_BASE_OFF + BEACON_VOLATILE_ACTIVE_MASK_OFF + 2) {
        active_mask = le16(cur + BEACON_VOLATILE_ACTIVE_MASK_OFF);
    }
    if (ev->vendor_ie_len >= BEACON_VOLATILE_BASE_OFF + BEACON_VOLATILE_REMOVE_MASK_OFF + 2) {
        remove_mask = le16(cur + BEACON_VOLATILE_REMOVE_MASK_OFF);
    }

    if (old_counter && new_counter == old_counter && active_mask == s->member_active_mask) return;

    if (new_counter == 0) {
        clear_member_fragments(s);
    } else if (old_counter && new_counter != old_counter && new_counter != (u8)(old_counter + 1u)) {
        clear_member_fragments(s);
    } else if (new_counter == (u8)(old_counter + 1u) && remove_mask) {
        for (unsigned idx = 1; idx <= MEMBER_SLOT_COUNT; idx++) {
            if (remove_mask & (1u << idx)) {
                unsigned slot = idx - 1u;
                s->member_name_mask &= (u16)~(1u << slot);
                memset(s->member_fragments[slot], 0, sizeof(s->member_fragments[slot]));
                s->member_names[slot][0] = 0;
            }
        }
    }

    s->volatile_counter = new_counter;
    s->member_active_mask = active_mask;
    s->connected_count = (u8)__builtin_popcount((unsigned)active_mask);
    for (unsigned idx = 1; idx <= MEMBER_SLOT_COUNT; idx++) {
        if (!(active_mask & (1u << idx))) {
            unsigned slot = idx - 1u;
            s->member_name_mask &= (u16)~(1u << slot);
            memset(s->member_fragments[slot], 0, sizeof(s->member_fragments[slot]));
            s->member_names[slot][0] = 0;
        }
    }

    unsigned need = BEACON_VOLATILE_BASE_OFF + BEACON_VOLATILE_MEMBER_OFF +
                    BEACON_VOLATILE_MEMBER_COUNT * MEMBER_FRAGMENT_BYTES;
    if (ev->vendor_ie_len < need) return;

    const u8 *entry = cur + BEACON_VOLATILE_MEMBER_OFF;
    for (unsigned i = 0; i < BEACON_VOLATILE_MEMBER_COUNT; i++, entry += MEMBER_FRAGMENT_BYTES) {
        unsigned idx = entry[0] >> 4;
        if (idx == 0 || idx > MEMBER_SLOT_COUNT) continue;
        unsigned slot = idx - 1u;
        memcpy(s->member_fragments[slot], entry, MEMBER_FRAGMENT_BYTES);
        scan_decode_member_name(entry, s->member_names[slot], sizeof(s->member_names[slot]));
        if (s->member_names[slot][0]) {
            s->member_name_mask |= (u16)(1u << slot);
        } else {
            s->member_name_mask &= (u16)~(1u << slot);
        }
    }
}

/**
 * @brief Parsed fixed-metadata fragment ready to merge into a content slot.
 */
typedef struct {
    const u8 *data;
    unsigned len;
    unsigned index;
    unsigned total;
} ScanFixedFragment;

/** @brief Validates and parses the common envelope required before a BSS event can update a slot. */
static bool scan_event_has_valid_beacon(const Arm7BssEvent *ev, BeaconIeView *view) {
    if (!ev || ev->vendor_ie_len < BEACON_VENDOR_IE_MIN_BYTES ||
        ev->snippet_no >= SNIPPET_COUNT) {
        return false;
    }
    return beacon_ie_parse(view, ev->vendor_ie_payload, ev->vendor_ie_len) &&
           view->checksum_ok;
}

/** @brief Returns whether a beacon attribute describes the fixed metadata payload. */
static bool attr_has_fixed_metadata(BeaconDataAttr attr) {
    return attr == BEACON_DATA_ATTR_FIXED_NORMAL ||
           attr == BEACON_DATA_ATTR_FIXED_NO_ICON;
}

/** @brief Initializes a newly allocated scan slot with its stable identity. */
static void init_slot_for_event(ContentSlot *s, BeaconDataAttr attr, u32 id) {
    memset(s, 0, sizeof(*s));
    s->used = true;
    s->id = id;
    if (attr_has_fixed_metadata(attr)) s->beacon_data_attr = (u8)attr;
    s->downloaded = false;
}

/** @brief Checks whether a reused slot needs its metadata reset for a new session. */
static bool slot_session_changed(const ContentSlot *s, const Arm7BssEvent *ev) {
    return s->session_id != ev->session_id ||
           s->temporary_group_id != ev->temporary_group_id;
}

/** @brief Copies event identity, packet limits, radio data and timestamps into a slot. */
static void update_slot_identity(ContentSlot *s, const Arm7BssEvent *ev,
                                 BeaconDataAttr attr, u16 parent_packet_max_bytes) {
    s->game_group_id = ev->game_group_id;
    s->game_id = ev->game_id;
    s->temporary_group_id = ev->temporary_group_id;
    s->parent_packet_max_bytes = parent_packet_max_bytes;
    s->child_packet_max_bytes = ev->child_packet_max_bytes ? ev->child_packet_max_bytes : CHILD_MAX_DEFAULT;
    s->session_id = ev->session_id;
    s->file_no = ev->file_no;
    if (attr_has_fixed_metadata(attr)) s->beacon_data_attr = (u8)attr;
    s->bss = ev->bss;
    s->last_seen_frame = g_frameCounter;
    s->last_seen_time = scan_now_seconds();
}

/** @brief Stores the latest Nintendo vendor IE bytes used by reports and BCN output. */
static void store_nintendo_ie_sample(ContentSlot *s, const Arm7BssEvent *ev) {
    s->nin_sample_len = ev->vendor_ie_len;
    if (s->nin_sample_len > MAX_NIN_PAYLOAD) s->nin_sample_len = MAX_NIN_PAYLOAD;
    memcpy(s->nin_sample, ev->vendor_ie_payload, s->nin_sample_len);
}

/** @brief Adapts the shared beacon parser's fixed-fragment view to scan storage. */
static bool parse_fixed_fragment(const BeaconIeView *view, ScanFixedFragment *out) {
    if (!view || !out || !view->fixed_fragment_valid ||
        view->fragment_len > BEACON_FIXED_FRAGMENT_STORAGE_BYTES) {
        return false;
    }

    out->data = view->fragment;
    out->len = view->fragment_len;
    out->index = view->fragment_index;
    out->total = view->fragment_total;
    return true;
}

/** @brief Checks whether an incoming fragment conflicts with the known fragment count. */
static bool fragment_count_conflicts(const ContentSlot *s, const ScanFixedFragment *frag) {
    if (frag->index == 0) return false;
    return (s->fragment_mask & 1u) &&
           s->fragment_len[0] >= BEACON_FRAGMENT_HEADER_BYTES &&
           s->fragments[0][1] != frag->total;
}

/** @brief Resets stale fixed-fragment metadata and preserves the just-seen beacon frame. */
static void reset_fragments_for_event(ContentSlot *s, const Arm7BssEvent *ev,
                                      u32 timestamp_us, bool *was_complete) {
    reset_slot_metadata(s);
    store_beacon_frame(s, ev, timestamp_us);
    *was_complete = false;
}

/** @brief Clears stale fixed fragments when fragment zero announces a new count or body. */
static void reset_stale_fixed_fragments(ContentSlot *s, const ScanFixedFragment *frag,
                                        const Arm7BssEvent *ev, u32 timestamp_us,
                                        bool *was_complete) {
    if (frag->index != 0) return;

    bool reset_fragments = false;
    for (unsigned i = 0; i < SNIPPET_COUNT; i++) {
        if ((s->fragment_mask & (1u << i)) &&
            s->fragment_len[i] >= BEACON_FRAGMENT_HEADER_BYTES &&
            s->fragments[i][1] != frag->total) {
            reset_fragments = true;
            break;
        }
    }
    if (reset_fragments) {
        reset_fragments_for_event(s, ev, timestamp_us, was_complete);
    }

    bool fragment_zero_changed =
        s->fragment_len[0] != (u8)frag->len ||
        memcmp(s->fragments[0], frag->data, frag->len) != 0;
    if (fragment_zero_changed && (s->fragment_mask & 1u)) {
        reset_fragments_for_event(s, ev, timestamp_us, was_complete);
    }
}

/** @brief Stores a fixed-beacon fragment if the slot does not have it yet. */
static bool store_fixed_fragment(ContentSlot *s, const ScanFixedFragment *frag) {
    bool stored = (s->fragment_mask & (1u << frag->index)) != 0;
    if (stored) return false;

    s->fragment_len[frag->index] = (u8)frag->len;
    memcpy(s->fragments[frag->index], frag->data, frag->len);
    s->fragment_mask |= (1u << frag->index);
    return true;
}

/** @brief Updates completion state and logs newly decoded fixed metadata. */
static void finish_fixed_slot_update(ContentSlot *s, bool was_complete, bool changed) {
    s->complete = scan_metadata_complete(s);
    if (s->complete && (!was_complete || changed)) {
        decode_slot_info(s);
        if (!was_complete) log_slot_info(s);
    }
}

/** @brief Merges one fixed beacon fragment and refreshes decoded slot metadata. */
static bool update_slot_from_fixed_beacon(ContentSlot *s, const Arm7BssEvent *ev,
                                          const BeaconIeView *view,
                                          u32 timestamp_us, bool *was_complete,
                                          bool *changed) {
    ScanFixedFragment frag;
    if (!parse_fixed_fragment(view, &frag)) return false;
    if (fragment_count_conflicts(s, &frag)) return false;

    reset_stale_fixed_fragments(s, &frag, ev, timestamp_us, was_complete);
    *changed = store_fixed_fragment(s, &frag);
    finish_fixed_slot_update(s, *was_complete, *changed);
    return true;
}

/**
 * @brief Merges one ARM7 beacon event into the ARM9 scan-slot table.
 */
void scan_handle_bss(const Arm7BssEvent *ev, u32 timestamp_us) {
    BeaconIeView view;
    u16 parent_packet_max_bytes = 0;
    if (!scan_event_has_valid_beacon(ev, &view)) return;
    if (!beacon_parent_packet_max_normalize(ev->parent_packet_max_bytes,
                                               &parent_packet_max_bytes)) {
        return;
    }
    set_current_wlan_channel(ev->bss.channel);

    u32 id = scan_make_provisional_id(ev);
    ContentSlot *s = find_or_alloc(ev);
    if (!s) return;
    bool new_slot = !s->used;
    if (!s->used) {
        init_slot_for_event(s, view.data_attr, id);
        ui_mark_dirty();
    }
    if (!new_slot && slot_session_changed(s, ev)) reset_slot_metadata(s);
    update_slot_identity(s, ev, view.data_attr, parent_packet_max_bytes);
    bool was_complete = s->complete;
    store_beacon_frame(s, ev, timestamp_us);
    scan_update_handover_prefix(s, ev);

    if (view.data_attr == BEACON_DATA_ATTR_VOLATILE) {
        update_member_fragments(s, ev);
        store_nintendo_ie_sample(s, ev);
        ui_mark_dirty();
        return;
    }

    if (view.data_attr != BEACON_DATA_ATTR_FIXED_NORMAL &&
        view.data_attr != BEACON_DATA_ATTR_FIXED_NO_ICON) {
        if (new_slot) ui_mark_dirty();
        return;
    }

    bool changed = false;
    if (!update_slot_from_fixed_beacon(s, ev, &view, timestamp_us, &was_complete, &changed)) return;
    store_nintendo_ie_sample(s, ev);
    if (new_slot || (s->complete && (!was_complete || changed))) ui_mark_dirty();
}

/**
 * @brief Returns whether a slot was observed in the recent scan window.
 */
static bool slot_recently_seen(const ContentSlot *s) {
    if (!s || !s->last_seen_time) return false;
    u32 now = scan_now_seconds();
    return now && (u32)(now - s->last_seen_time) <= COMM_TIMEOUT_SECONDS;
}

/**
 * @brief Returns whether a scan slot has enough metadata to start download.
 */
static bool slot_ready_for_download(const ContentSlot *s) {
    return s && s->used && s->complete && scan_beacon_frames_complete(s) && slot_recently_seen(s);
}

/**
 * @brief Selects the next eligible scan slot for download.
 */
ContentSlot *scan_pick_next(void) {
    if (scan_download_start_block_active()) return NULL;

    bool block_repeats = g_repeatDownloads && scan_repeat_download_block_active();
    ContentSlot *repeat_choice = NULL;
    unsigned repeat_count = 0;

    for (unsigned i = 0; i < CONTENT_SLOT_COUNT; i++) {
        ContentSlot *s = &g_slots[i];
        if (!slot_ready_for_download(s)) continue;

        if (!g_repeatDownloads) {
            if (s->downloaded) continue;
            if (s->tried) continue;
            return s;
        }

        if (!s->downloaded) return s;
        if (block_repeats) continue;

        repeat_count++;
        if ((scan_next_pick_random() % repeat_count) == 0) {
            repeat_choice = s;
        }
    }
    return repeat_choice;
}

/**
 * @brief Suppresses automatic downloads while scan state refreshes after cancel.
 */
void scan_pause_downloads_for_cooldown(void) {
    u32 now = scan_now_seconds();
    if (now) s_download_start_block_until = now + REPEAT_DOWNLOAD_COOLDOWN_SECONDS;
}

/**
 * @brief Finds a scan slot by stable content ID.
 */
ContentSlot *scan_find_by_id(u32 id) {
    for (unsigned i = 0; i < CONTENT_SLOT_COUNT; i++) {
        ContentSlot *s = &g_slots[i];
        if (s->used && s->id == id) return s;
    }
    return NULL;
}

/**
 * @brief Marks a slot and content ID as already downloaded.
 */
void scan_mark_downloaded(ContentSlot *s) {
    if (s) {
        scan_remember_downloaded_id(s->id);
        s->downloaded = true;
        s->tried = true;
        u32 now = scan_now_seconds();
        if (now) s_repeat_download_block_until = now + REPEAT_DOWNLOAD_COOLDOWN_SECONDS;
        ui_record_save_success(s);
    }
}
