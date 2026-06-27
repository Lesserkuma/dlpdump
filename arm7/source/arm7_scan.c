/**
 * @file arm7_scan.c
 * @brief Converts Calico scan callbacks into DS Download Play parent events.
 */
#include "arm7_internal.h"

/**
 * @brief Clears the temporary focused-channel scan accelerator.
 */
void scan_focus_clear(void) {
    memset(&g_scan_focus, 0, sizeof(g_scan_focus));
    g_scan_focus_channel = 0;
}

/**
 * @brief Returns the calibrated scan-channel mask, preferring DS primary channels.
 */
static u32 arm7_available_channel_mask(void) {
    u32 enabled = mwlGetCalibData()->enabled_ch_mask;
    if (!enabled) enabled = SCAN_ALL_CHANNELS_MASK;

    u32 primary = enabled & SCAN_PRIMARY_CHANNELS_MASK;
    return primary ? primary : enabled;
}

/**
 * @brief Picks the next enabled channel after the current scan channel.
 */
static u8 arm7_next_scan_channel(u32 mask) {
    if (!mask) return 0;

    u8 ch = g_scan_current_channel;
    if (ch < 1 || ch > 13) ch = 0;

    for (unsigned i = 0; i < 13; i++) {
        ch = (ch >= 13) ? 1 : (u8)(ch + 1);
        if (mask & (1u << ch)) return ch;
    }

    return 0;
}

/**
 * @brief Publishes a channel-change event when the scan dwell channel changes.
 */
static void arm7_publish_scan_channel(u8 channel) {
    if (channel == g_scan_current_channel) return;
    g_scan_current_channel = channel;
    arm7_push_event(EVENT_SCAN_CHANNEL, channel, NULL, 0);
}

/**
 * @brief Computes the one-channel mask for the next Calico scan dwell.
 */
static u32 arm7_channel_mask(void) {
    u8 channel = 0;

    if (g_scan_focus.used && g_scan_focus_channel >= 1 && g_scan_focus_channel <= 13) {
        if ((u32)(arm7_timestamp_us() - g_scan_focus.focus_start_us) > SCAN_FOCUS_TIMEOUT_US) {
            scan_focus_clear();
        } else {
            channel = g_scan_focus_channel;
        }
    }

    if (!channel) channel = arm7_next_scan_channel(arm7_available_channel_mask());
    if (!channel) return 0;

    arm7_publish_scan_channel(channel);
    return 1u << channel;
}

/**
 * @brief Converts Calico RSSI units into a 0..100 signal percentage.
 */
u8 arm7_signal_percent_from_rssi(u8 rssi) {
    unsigned pct = ((unsigned)rssi * 100u + 10u) / 21u;
    return pct > 100u ? 100u : (u8)pct;
}

/**
 * @brief Checks whether a raw 802.11 frame belongs to the active parent BSSID.
 */
bool arm7_frame_from_active_parent(const void *frame, unsigned len) {
    if (len < DOT11_HDR_SIZE) return false;

    const u8 *bssid = g_active_parent.bss.bssid;
    bool have_bssid = false;
    for (unsigned i = 0; i < 6; i++) {
        if (bssid[i]) {
            have_bssid = true;
            break;
        }
    }
    if (!have_bssid) return false;

    const Dot11Hdr *h = (const Dot11Hdr*)frame;
    return memcmp(h->addr1, bssid, 6) == 0 ||
           memcmp(h->addr2, bssid, 6) == 0 ||
           memcmp(h->addr3, bssid, 6) == 0;
}

/**
 * @brief Converts a Calico BSS descriptor into the shared ARM9 event form.
 */
static void copy_bss_to_common(ScanBssDesc *dst, const WlanBssDesc *src) {
    memset(dst, 0, sizeof(*dst));
    memcpy(dst->bssid, src->bssid, 6);
    dst->ssid_len = src->ssid_len <= 32 ? src->ssid_len : 32;
    memcpy(dst->ssid, src->ssid, dst->ssid_len);
    dst->ieee_caps = src->ieee_caps;
    dst->ieee_basic_rates = src->ieee_basic_rates;
    dst->ieee_all_rates = src->ieee_all_rates;
    dst->auth_type = (u8)src->auth_type;
    dst->rssi = src->rssi;
    dst->channel = src->channel;
}

/**
 * @brief Compares two focused-scan keys including BSSID and beacon identity.
 */
static bool scan_focus_same_key(const Arm7ScanFocus *a, const Arm7ScanFocus *b) {
    return a->game_group_id == b->game_group_id &&
           a->game_id == b->game_id &&
           a->temporary_group_id == b->temporary_group_id &&
           a->session_id == b->session_id &&
           a->file_no == b->file_no &&
           memcmp(a->bssid, b->bssid, 6) == 0;
}

/**
 * @brief Checks whether the current beacon identity was already completed.
 */
static bool scan_focus_already_done(const Arm7ScanFocus *key) {
    for (unsigned i = 0; i < SCAN_FOCUS_DONE_COUNT; i++) {
        if (g_scan_focus_done[i].used && scan_focus_same_key(&g_scan_focus_done[i], key)) return true;
    }
    return false;
}

/**
 * @brief Records a completed focus key in the small circular dedupe cache.
 */
static void scan_focus_remember_done(const Arm7ScanFocus *key) {
    Arm7ScanFocus *dst = &g_scan_focus_done[g_scan_focus_done_next++ % SCAN_FOCUS_DONE_COUNT];
    *dst = *key;
    dst->used = true;
}

/**
 * @brief Returns the metadata fragment completion mask for a beacon layout.
 */
static u16 scan_focus_complete_mask_for_attr(BeaconDataAttr attr) {
    const FixedMetadataLayout *layout = beacon_fixed_metadata_layout(attr);
    unsigned count = layout ? layout->fragment_count : SNIPPET_COUNT;
    return (u16)((1u << count) - 1u);
}

/**
 * @brief Focuses ARM7 scanning on a parent until all metadata fragments arrive.
 *
 * The focus key prevents repeatedly locking onto the same completed beacon set
 * while still shortening discovery time for incomplete DS Download Play slots.
 */
static void scan_focus_update(const WlanBssDesc *bss, const u8 *vendor_ie_payload) {
    if (!bss || !vendor_ie_payload) return;
    if (bss->channel < 1 || bss->channel > 13) return;
    u32 now_us = arm7_timestamp_us();
    if (g_scan_focus.used &&
        (u32)(now_us - g_scan_focus.focus_start_us) > SCAN_FOCUS_TIMEOUT_US) {
        scan_focus_clear();
    }

    BeaconDataAttr attr =
        beacon_data_attr_from_file_session(vendor_ie_payload[BEACON_VENDOR_IE_FILE_SESSION_OFF]);
    u8 snippet_no = vendor_ie_payload[BEACON_VENDOR_IE_SNIPPET_OFF];
    if (snippet_no >= SNIPPET_COUNT) return;

    Arm7ScanFocus key;
    memset(&key, 0, sizeof(key));
    key.used = true;
    memcpy(key.bssid, bss->bssid, 6);
    key.game_group_id = le32(vendor_ie_payload + BEACON_VENDOR_IE_GGID_OFF);
    key.temporary_group_id = le16(vendor_ie_payload + BEACON_VENDOR_IE_TGID_OFF);
    key.game_id = le32(vendor_ie_payload + BEACON_VENDOR_IE_MB_GAME_ID_OFF);
    key.session_id = vendor_ie_payload[BEACON_VENDOR_IE_FILE_SESSION_OFF + 1u];
    key.file_no = vendor_ie_payload[BEACON_VENDOR_IE_FILE_SESSION_OFF] >> 2;
    key.snippet_mask = (u16)(1u << snippet_no);
    key.focus_start_us = now_us;

    if (scan_focus_already_done(&key)) return;

    if (!g_scan_focus.used || !scan_focus_same_key(&g_scan_focus, &key)) {
        g_scan_focus = key;
        g_scan_focus_channel = bss->channel;
    } else {
        g_scan_focus.snippet_mask |= key.snippet_mask;
    }

    u16 complete_mask = scan_focus_complete_mask_for_attr(attr);
    if ((g_scan_focus.snippet_mask & complete_mask) == complete_mask) {
        scan_focus_remember_done(&g_scan_focus);
        memset(&g_scan_focus, 0, sizeof(g_scan_focus));
        g_scan_focus_channel = 0;
    }
}

/**
 * @brief Calico scan callback retained so MLME scan state stays installed.
 */
void arm7_on_bss_info(WlanBssDesc *bss, WlanBssExtra *extra) {
    (void)bss;
    (void)extra;
    /* The modified RX task sends the richer Nintendo-IE event; keep this callback
       installed so Calico's MLME scan path remains active. */
}

/**
 * @brief Chooses the next channel after Calico reports scan dwell completion.
 */
u32 arm7_on_scan_end(void) {
    if (!g_scan_enabled) return 0;
    return arm7_channel_mask();
}

/**
 * @brief Starts or restarts Calico local-guest scanning for DS Download Play parents.
 */
void arm7_start_scan(void) {
    g_scan_enabled = true;
    g_assoc_aid = 0;
    scan_focus_clear();
    g_scan_current_channel = 0;
    arm7_clear_pending_reply();
    mwlDevStop();
    mwlDevSetMode(MwlMode_LocalGuest);
    arm7_set_power_state(1);

    WlanBssScanFilter f;
    memset(&f, 0, sizeof(f));
    f.channel_mask = arm7_channel_mask();
    memset(f.target_bssid, 0xff, sizeof(f.target_bssid));

    if (!mwlMlmeScan(&f, 105)) {
        arm7_push_event(EVENT_ERROR, 0, "Could not start scan.", sizeof("Could not start scan."));
    } else {
    }
}

/**
 * @brief Publishes one Nintendo vendor-IE beacon observation to ARM9.
 *
 * Beacons are accepted during normal scanning and while connected to the active
 * parent. The payload is bounded, converted into the common event format, and
 * optionally accompanied by the full raw beacon frame for PCAP/BCN output.
 */
void arm7_bss_info(const WlanBssDesc *bss, const u8 *vendor_ie_payload, unsigned vendor_ie_len, const void *frame, unsigned frame_len) {
    bool active_parent = !g_scan_enabled && frame && arm7_frame_from_active_parent(frame, frame_len);
    if (!g_scan_enabled && !active_parent) return;
    if (!bss || !vendor_ie_payload ||
        !beacon_ie_has(vendor_ie_payload, vendor_ie_len,
                          BEACON_VENDOR_IE_SNIPPET_OFF, 1u)) {
        return;
    }
    if (vendor_ie_len > MAX_NIN_PAYLOAD) vendor_ie_len = MAX_NIN_PAYLOAD;

    /* Calico passes the WlanIeNin body (without the 00 09 BF 00 vendor prefix).
       Hardware captures use 0x0b here, while the local test parent uses 0x03;
       both mark multiboot data with the low bits set. */
    if (!beacon_ie_type_is_multiboot(vendor_ie_payload, vendor_ie_len)) return;
    if (g_scan_enabled) scan_focus_update(bss, vendor_ie_payload);

    Arm7BssEvent ev;
    memset(&ev, 0, sizeof(ev));
    copy_bss_to_common(&ev.bss, bss);
    ev.game_group_id = le32(vendor_ie_payload + BEACON_VENDOR_IE_GGID_OFF);
    ev.temporary_group_id = le16(vendor_ie_payload + BEACON_VENDOR_IE_TGID_OFF);
    ev.parent_packet_max_bytes = le16(vendor_ie_payload + BEACON_VENDOR_IE_PARENT_MAX_OFF);
    ev.child_packet_max_bytes = le16(vendor_ie_payload + BEACON_VENDOR_IE_CHILD_MAX_OFF);
    ev.game_id = le32(vendor_ie_payload + BEACON_VENDOR_IE_MB_GAME_ID_OFF);
    ev.beacon_data_attr = vendor_ie_payload[BEACON_VENDOR_IE_FILE_SESSION_OFF] & 3u;
    ev.file_no = vendor_ie_payload[BEACON_VENDOR_IE_FILE_SESSION_OFF] >> 2;
    ev.session_id = vendor_ie_payload[BEACON_VENDOR_IE_FILE_SESSION_OFF + 1u];
    ev.connected_count = vendor_ie_payload[BEACON_VENDOR_IE_CONNECTED_OFF];
    ev.snippet_no = vendor_ie_payload[BEACON_VENDOR_IE_SNIPPET_OFF];
    ev.vendor_ie_len = (u8)vendor_ie_len;
    memcpy(ev.vendor_ie_payload, vendor_ie_payload, vendor_ie_len);
    if (frame && frame_len >= DOT11_HDR_SIZE && frame_len <= MAX_BEACON_FRAME) {
        ev.beacon_frame_len = (u16)frame_len;
        memcpy(ev.beacon_frame, frame, frame_len);
    }
    arm7_push_event(EVENT_SCAN_BSS, 0, &ev, sizeof(ev));
}
