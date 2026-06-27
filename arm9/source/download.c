/**
 * @file download.c
 * @brief Coordinates the ARM9-side DS Download Play download state machine.
 */
#include "download_internal.h"

Download g_download;
VerifyStatus g_verifyStatus;
u32 g_verifyContentId;

u16 s_debug_last_received_log;
u16 s_debug_last_final_missing;
u16 s_debug_last_final_received;
u16 s_debug_last_correction_missing;
u16 s_debug_last_correction_received;
u8 s_debug_duplicate_mismatches;
u32 s_next_missing_cursor;
u32 s_last_signal_update_frame;

#define SIGNAL_UPDATE_MIN_FRAMES 15u
#define USER_ABORT_GRACE_SECONDS 3u

/**
 * @brief Returns a monotonic-enough wall-clock timestamp for timeout logic.
 *
 * @return Seconds since the platform epoch, or 0 when the RTC call fails.
 */
u32 download_now_seconds(void) {
    time_t t = time(NULL);
    return t > 0 ? (u32)t : 0;
}

/**
 * @brief Computes elapsed seconds from a stored timestamp without unsigned underflow.
 *
 * @param start Previously captured timestamp from `download_now_seconds`.
 * @param elapsed Receives zero on invalid input or the elapsed seconds on success.
 * @return true when both timestamps are valid and monotonic for this calculation.
 */
static bool download_elapsed_seconds(u32 start, u32 *elapsed) {
    if (!elapsed) return false;
    *elapsed = 0;
    u32 now = download_now_seconds();
    if (!now || !start || now < start) return false;
    *elapsed = now - start;
    return true;
}

/**
 * @brief Fills the reply parameters with the next firmware username fragment.
 */
static void fill_username_snippet(ReplyParams *p) {
    const u32 gid = g_download.slot ? g_download.slot->game_id : 0;
    const u8 file_no = g_download.slot ? g_download.slot->file_no : 0;
    build_username_snippet(p, gid, file_no, &g_download.user);
}

/**
 * @brief Copies firmware nickname, favorite color and player slot into state.
 *
 * We probably have to use player number 1 for the DS Download Play username
 * reply; this loader mirrors that value so parent title state machines accept
 * the association.
 */
static void load_firmware_user_info(UserInfo *user) {
    memset(user, 0, sizeof(*user));

    const PERSONAL_DATA *pd = PersonalData;
    unsigned len = pd->nameLen;
    if (len > USER_NAME_CHARS) len = USER_NAME_CHARS;

    user->favorite_color = pd->theme & 0x0f;
    user->name_len = (u8)len;
    user->player_no = 1;
    for (unsigned i = 0; i < len; i++) user->name[i] = (u16)pd->name[i];
    for (unsigned i = len; i < USER_NAME_CHARS; i++) user->name[i] = 0;
}

/**
 * @brief Queues one ARM7 reply command for the active parent association.
 *
 * Username replies are expanded from firmware/user state; all other reply
 * types carry packet counters only.
 */
void download_send_reply(u8 type, u16 next_packet, u16 total_packets) {
    if (g_download.user_abort_requested) return;

    ReplyParams p;
    memset(&p, 0, sizeof(p));
    p.reply_type = type;
    p.user_snippet_no = g_download.name_snippet;
    p.next_packet = next_packet;
    p.total_packets = total_packets;
    p.parent_reply_time = 0;
    if (type == REPLY_USERNAME) fill_username_snippet(&p);
    ipc_send_command(ARM7_CMD_QUEUE_REPLY, &p, sizeof(p));
}

/**
 * @brief Sends the one-shot child-cancel command used before timeout saving.
 */
static void request_child_cancel(void) {
    if (!g_download.active || g_download.child_cancel_sent) return;
    g_download.child_cancel_sent = true;
    debug_log("child cancel request");
    ipc_send_command(ARM7_CMD_CHILD_CANCEL, NULL, 0);
}

/**
 * @brief Returns the current parent packet size inferred from beacon/RSA state.
 */
unsigned download_packet_size(void) {
    if (g_download.parent_packet_max_bytes) return g_download.parent_packet_max_bytes;
    if (g_download.slot && g_download.slot->parent_packet_max_bytes) return g_download.slot->parent_packet_max_bytes;
    if (g_download.max_payload) return g_download.max_payload + 6u;
    return 0;
}

/**
 * @brief Tests whether a bounded byte range contains only zero padding.
 */
static bool bytes_all_zero(const u8 *data, unsigned len) {
    if (!data) return true;
    for (unsigned i = 0; i < len; i++) {
        if (data[i] != 0) return false;
    }
    return true;
}

/**
 * @brief Logs the optional RSA user-parameter block when a title provides one.
 */
void download_log_user_parameters_if_present(u32 id) {
    const u8 *p = g_download.rsa.download_parameter;
    if (bytes_all_zero(p, sizeof(g_download.rsa.download_parameter))) return;

    ui_log("[%08lx] User parameters present: %02x %02x %02x %02x …",
            (unsigned long)id,
            (unsigned)p[0], (unsigned)p[1], (unsigned)p[2], (unsigned)p[3]);
}

/**
 * @brief Emits the user-visible completion or failure message for one attempt.
 */
static void log_download_result(bool success, const char *detail) {
    u32 id = g_download.id;

    if (success) {
        if (g_download.pcap_error) {
            ui_log("[%08lx] Done with PCAP warning. Saved dump files to:\n%s",
                    (unsigned long)id, g_download.output_base);
        } else {
            ui_log("[%08lx] Done! Saved dump files to:\n%s", (unsigned long)id, g_download.output_base);
        }
    } else {
        ui_log("[%08lx] An error occured while trying to download.", (unsigned long)id);
        ui_log("[%08lx] %s", (unsigned long)id, detail ? detail : "Unknown error.");
    }
}

/**
 * @brief Names common IEEE 802.11 authentication/association status values.
 */
static const char *connect_status_text(unsigned status) {
    switch (status) {
        case 0:  return "success";
        case 1:  return "no association response";
        case 10: return "capabilities not supported";
        case 11: return "reassociation denied";
        case 12: return "association denied";
        case 13: return "authentication algorithm not supported";
        case 14: return "unexpected authentication sequence";
        case 15: return "authentication challenge failed";
        case 16: return "authentication timeout";
        case 17: return "association denied, too many stations";
        case 18: return "basic rates not supported";
        default: return "unknown status";
    }
}

/**
 * @brief Formats the ARM7 MLME connection-failure code for UI and debug logs.
 */
static void format_connect_failure(char *out, size_t out_size, unsigned reason) {
    if (!out || !out_size) return;

    switch (reason) {
        case CONNECT_FAIL_JOIN_START:
            snprintf(out, out_size, "Connection failed: join could not start (reason %u).", reason);
            return;
        case CONNECT_FAIL_JOIN_TIMEOUT:
            snprintf(out, out_size, "Connection failed: join failed or timed out (reason %u).", reason);
            return;
        case CONNECT_FAIL_AUTH_START:
            snprintf(out, out_size, "Connection failed: authentication could not start (reason %u).", reason);
            return;
        case CONNECT_FAIL_ASSOC_START:
            snprintf(out, out_size, "Connection failed: association could not start (reason %u).", reason);
            return;
        default:
            break;
    }

    unsigned phase = reason & CONNECT_FAIL_PHASE_MASK;
    unsigned status = reason & CONNECT_FAIL_STATUS_MASK;
    if (phase == CONNECT_FAIL_AUTH_STATUS) {
        snprintf(out, out_size, "Connection failed: status %u, %s (reason %u).",
                 status, connect_status_text(status), reason);
    } else if (phase == CONNECT_FAIL_ASSOC_STATUS) {
        snprintf(out, out_size, "Connection failed: status %u, %s (reason %u).",
                 status, connect_status_text(status), reason);
    } else {
        snprintf(out, out_size, "Connection failed: unknown MLME reason %u.", reason);
    }
}

/**
 * @brief Maps a signal percentage into the four UI icon buckets.
 */
static unsigned signal_bucket(u8 percent) {
    if (percent >= SIGNAL_LEVEL_3_PERCENT) return 3;
    if (percent >= SIGNAL_LEVEL_2_PERCENT) return 2;
    if (percent >= SIGNAL_LEVEL_1_PERCENT) return 1;
    return 0;
}

/**
 * @brief Refreshes the active transfer's communication timeout baseline.
 */
void download_touch_communication(void) {
    if (g_download.active) g_download.last_comm_time = download_now_seconds();
}

/**
 * @brief Reports whether the parent accepted RSA but has not sent DATA yet.
 */
bool download_waiting_for_transfer_start(void) {
    return g_download.active &&
           g_runState == RUN_DOWNLOADING &&
           g_download.have_rsa &&
           !g_download.transfer_started &&
           !g_download.saved;
}

/**
 * @brief Returns the remaining transfer-start wait window in seconds.
 */
unsigned download_transfer_wait_seconds_remaining(void) {
    if (!download_waiting_for_transfer_start()) return 0;
    u32 elapsed = 0;
    if (!download_elapsed_seconds(g_download.transfer_wait_start_time, &elapsed)) return 0;
    if (elapsed >= TRANSFER_START_TIMEOUT_SECONDS) return 0;
    return TRANSFER_START_TIMEOUT_SECONDS - elapsed;
}

/**
 * @brief Reports whether all DATA packets arrived while FINAL is still absent.
 */
bool download_waiting_for_final(void) {
    return g_download.active &&
           g_runState == RUN_DOWNLOADING &&
           g_download.have_rsa &&
           g_download.total_packets &&
           g_download.received_packets >= g_download.total_packets &&
           !g_download.final_seen &&
           !g_download.saved;
}

/**
 * @brief Returns the remaining final-command wait window in seconds.
 */
unsigned download_final_wait_seconds_remaining(void) {
    if (!download_waiting_for_final()) return 0;
    u32 elapsed = 0;
    if (!download_elapsed_seconds(g_download.all_packets_time, &elapsed)) return 0;
    if (elapsed >= FINAL_WAIT_TIMEOUT_SECONDS) return 0;
    return FINAL_WAIT_TIMEOUT_SECONDS - elapsed;
}

/**
 * @brief Updates save/report progress and redraws the UI on visible changes.
 */
void download_update_save_progress(unsigned percent) {
    if (percent > 100) percent = 100;
    if (!g_download.active || (g_runState != RUN_SAVING && g_runState != RUN_CREATING_REPORT)) return;
    if (g_download.save_progress_valid && percent < g_download.save_percent) return;
    if (g_download.save_progress_valid && g_download.save_percent == percent) return;
    g_download.save_progress_valid = true;
    g_download.save_percent = (u8)percent;
    ui_draw_now();
}

/**
 * @brief Checks whether a raw 802.11 frame references the active parent BSSID.
 *
 * @return true when any address slot in the dot11 header matches the selected
 *         beacon's BSSID.
 */
bool download_frame_from_active_parent(const void *frame, unsigned len) {
    if (!g_download.active || !g_download.slot || !frame || len < DOT11_HDR_SIZE) return false;

    const Dot11Hdr *h = (const Dot11Hdr*)frame;
    const u8 *bssid = g_download.slot->bss.bssid;
    return memcmp(h->addr1, bssid, 6) == 0 ||
           memcmp(h->addr2, bssid, 6) == 0 ||
           memcmp(h->addr3, bssid, 6) == 0;
}

/**
 * @brief Accounts for ARM7 event-queue drops observed since association start.
 */
void download_note_ipc_dropped_events(u32 dropped_events) {
    if (!g_download.active) return;
    if (dropped_events < g_download.ipc_drop_baseline) {
        g_download.ipc_drop_baseline = dropped_events;
        g_download.ipc_drop_seen = dropped_events;
        return;
    }
    if (dropped_events > g_download.ipc_drop_seen) {
        g_download.stats.packets_dropped += dropped_events - g_download.ipc_drop_seen;
        g_download.ipc_drop_seen = dropped_events;
    }
}

/**
 * @brief Updates the cached signal sample used by the top-screen list.
 *
 * Repaints are throttled unless the bucket changes, avoiding redraw churn while
 * the ARM7 reports RSSI on many received frames.
 */
static void update_signal_display(u8 percent) {
    if (percent > 100) percent = 100;
    if (!g_download.active) return;
    bool was_valid = g_download.signal_valid;
    unsigned old_bucket = signal_bucket(g_download.signal_percent);
    unsigned new_bucket = signal_bucket(percent);
    if (was_valid && old_bucket == new_bucket &&
        (u32)(g_frameCounter - s_last_signal_update_frame) < SIGNAL_UPDATE_MIN_FRAMES) {
        return;
    }
    s_last_signal_update_frame = g_frameCounter;
    g_download.signal_percent = percent;
    g_download.signal_valid = true;
    if (!was_valid || old_bucket != new_bucket) ui_mark_dirty();
}

/**
 * @brief Updates the displayed signal percentage without raw RSSI stats.
 */
void download_update_signal(u8 percent) {
    update_signal_display(percent);
}

/**
 * @brief Updates signal display plus min/max/average raw RSSI statistics.
 */
void download_update_signal_raw(u8 percent, u8 raw_rssi) {
    if (percent > 100) percent = 100;
    if (!g_download.active) return;
    if (!g_download.signal_sample_count) {
        g_download.signal_min_percent = percent;
        g_download.signal_max_percent = percent;
        g_download.signal_raw_min = raw_rssi;
        g_download.signal_raw_max = raw_rssi;
    } else {
        if (percent < g_download.signal_min_percent) g_download.signal_min_percent = percent;
        if (percent > g_download.signal_max_percent) g_download.signal_max_percent = percent;
        if (raw_rssi < g_download.signal_raw_min) g_download.signal_raw_min = raw_rssi;
        if (raw_rssi > g_download.signal_raw_max) g_download.signal_raw_max = raw_rssi;
    }
    g_download.signal_sum_percent += percent;
    g_download.signal_raw_sum += raw_rssi;
    g_download.signal_sample_count++;
    update_signal_display(percent);
}

/**
 * @brief Releases all dynamic buffers and clears the active download state.
 */
void download_free(void) {
    download_free_buffers();
    memset(&g_download, 0, sizeof(g_download));
}

/**
 * @brief Returns the mutable scan-table slot behind the active snapshot.
 */
static ContentSlot *active_scan_slot(void) {
    return g_download.scan_slot ? g_download.scan_slot : g_download.slot;
}

/**
 * @brief Allows a slot to be retried after the next fresh beacon arrives.
 */
static void mark_retry_after_fresh_beacon(ContentSlot *slot) {
    if (!slot) return;
    slot->downloaded = false;
    slot->tried = false;
    slot->last_seen_frame = 0;
    slot->last_seen_time = 0;
}

/**
 * @brief Commits PCAP and debug-log diagnostics after a failed attempt.
 */
static void commit_attempt_diagnostics(void) {
    bool pcap_saved = false;
    if (!pcap_had_error() && pcap_close() && pcap_commit()) {
        pcap_saved = true;
    } else {
        pcap_discard();
    }

    debug_log("diagnostics pcap_saved=%u", pcap_saved ? 1u : 0u);
    if (!debug_close() || !debug_commit()) debug_discard();
}

/**
 * @brief Tears down transient outputs and returns ARM7/ARM9 state to scanning.
 */
static void return_to_scan(void) {
    debug_log("return_to_scan state=%u received=%u/%u saved=%u final_seen=%u",
               (unsigned)g_runState, g_download.received_packets,
               g_download.total_packets, g_download.saved ? 1u : 0u,
               g_download.final_seen ? 1u : 0u);
    commit_attempt_diagnostics();
    download_free();
    g_runState = RUN_SCANNING;
    set_current_wlan_channel(0);
    ipc_send_command(ARM7_CMD_RESET_TO_SCAN, NULL, 0);
    ui_mark_dirty();
}

/**
 * @brief Completes a user-requested cancellation after ARM7 had time to notify the parent.
 */
static void finish_user_abort(const char *source) {
    ui_log("[%08lx] Download cancelled by user.", (unsigned long)g_download.id);
    debug_log("user abort complete source=%s received=%u/%u",
               source ? source : "unknown",
               g_download.received_packets, g_download.total_packets);

    ContentSlot *slot = active_scan_slot();
    if (slot) {
        slot->tried = false;
        slot->last_seen_frame = 0;
        slot->last_seen_time = 0;
    }
    scan_pause_downloads_for_cooldown();
    return_to_scan();
}

/**
 * @brief Stores the report completion reason in bounded download state storage.
 */
static void store_completion_reason(const char *completion_reason) {
    const char *reason = (completion_reason && completion_reason[0])
        ? completion_reason
        : "Unknown";
    strncpy(g_download.completion_reason, reason, sizeof(g_download.completion_reason) - 1);
    g_download.completion_reason[sizeof(g_download.completion_reason) - 1] = 0;
}

/**
 * @brief Resets scan mode after a save attempt consumed the active state.
 */
static void reset_scan_after_save_attempt(void) {
    download_free();
    g_runState = RUN_SCANNING;
    set_current_wlan_channel(0);
    ipc_send_command(ARM7_CMD_RESET_TO_SCAN, NULL, 0);
}

/**
 * @brief Records RSA verification failure and preserves diagnostics when possible.
 */
static void handle_verify_failure(void) {
    g_verifyStatus = VERIFY_FAILED;
    ui_count_failure();
    log_download_result(false, "RSA signature verification failed.");
    debug_log("verify failed");
    commit_attempt_diagnostics();
    ContentSlot *slot = active_scan_slot();
    if (slot) {
        slot->downloaded = false;
        slot->tried = false;
    }
    reset_scan_after_save_attempt();
}

/**
 * @brief Checks the completed download, or marks verification skipped when no key is available.
 */
static bool verify_completed_download_or_skip(void) {
    g_runState = RUN_CHECKING_RSA_HASH;
    set_current_wlan_channel(0);
    g_verifyContentId = g_download.id;

    if (!verify_public_key_loaded()) {
        g_verifyStatus = VERIFY_SKIPPED;
        g_download.rsa_verification_skipped = true;
        ui_log("[%08lx] RSA signature verification skipped.",
                (unsigned long)g_download.id);
        debug_log("verify skipped; public key is missing");
        ui_draw_now();
        return true;
    }

    g_verifyStatus = VERIFY_PENDING;
    ui_log("[%08lx] Checking RSA signature to verify integrity…", (unsigned long)g_download.id);
    debug_log("verify start received=%u/%u", g_download.received_packets, g_download.total_packets);
    ui_draw_now();

    if (!verify_download(&g_download)) return false;

    g_verifyStatus = VERIFY_OK;
    g_download.rsa_hash_verified = true;
    ui_log("[%08lx] RSA signature verified valid. Saving files…", (unsigned long)g_download.id);
    debug_log("verify ok");
    return true;
}

/**
 * @brief Closes PCAP capture and enters the file-save UI state.
 */
static void begin_file_save(void) {
    if (!pcap_close() || pcap_had_error()) {
        g_download.pcap_error = true;
        debug_log("pcap close failed");
        pcap_discard();
    }
    g_runState = RUN_SAVING;
    g_download.save_progress_valid = true;
    g_download.save_percent = 0;
    debug_log("save start");
    ui_draw_now();
}

/**
 * @brief Commits the temporary PCAP after core dump files were saved.
 */
static void commit_pcap_after_save(void) {
    if (g_download.pcap_error) {
        pcap_discard();
        return;
    }
    if (!pcap_commit()) {
        g_download.pcap_error = true;
        debug_log("pcap commit failed");
    }
}

/**
 * @brief Closes and commits the debug log as the last output-set member.
 */
static void commit_debug_after_save(void) {
    download_update_save_progress(100);
    debug_log("save ok");
    if (!debug_close() || !debug_commit()) {
        debug_discard();
    }
}

/**
 * @brief Writes NDS/BCN/report files and commits auxiliary outputs.
 */
static bool save_completed_download(const char **save_error_detail) {
    begin_file_save();
    if (!file_save_download(&g_download)) {
        *save_error_detail = "Could not write output files.";
        return false;
    }
    commit_pcap_after_save();
    commit_debug_after_save();
    return true;
}

/**
 * @brief Verifies, saves, reports and resets after the transfer has completed.
 *
 * Failed attempts still try to keep PCAP and debug-log diagnostics so hardware
 * save failures can be inspected after reboot.
 */
void download_save_and_restart(const char *completion_reason) {
    if (g_download.saved) return;
    if (!g_download.beacon_pcap_written) {
        download_abort_and_retry("PCAP is missing one or more beacon frames.");
        return;
    }
    g_download.saved = true;
    g_download.completion_time = download_now_seconds();
    store_completion_reason(completion_reason);
    if (!verify_completed_download_or_skip()) {
        handle_verify_failure();
        return;
    }

    const char *save_error_detail = "Could not write output files.";
    bool ok = save_completed_download(&save_error_detail);
    if (ok) {
        log_download_result(true, NULL);
        scan_mark_downloaded(active_scan_slot());
    } else {
        ui_count_failure();
        log_download_result(false, save_error_detail);
        debug_log("save failed");
        commit_attempt_diagnostics();
        mark_retry_after_fresh_beacon(active_scan_slot());
    }
    reset_scan_after_save_attempt();
}

/**
 * @brief Copies a scan slot into download-owned state for one attempt.
 */
static ContentSlot *download_init_attempt_from_slot(ContentSlot *scan_slot) {
    memset(&g_download, 0, sizeof(g_download));
    g_download.scan_slot = scan_slot;
    g_download.slot_snapshot = *scan_slot;
    g_download.slot = &g_download.slot_snapshot;

    ContentSlot *slot = g_download.slot;
    g_download.id = slot->id;
    g_download.parent_packet_max_bytes = slot->parent_packet_max_bytes;
    g_download.child_packet_max_bytes = slot->child_packet_max_bytes;
    g_download.expected_file_no = slot->file_no;
    g_download.start_temporary_group_id = slot->temporary_group_id;
    g_download.signal_valid = false;
    g_download.start_time = download_now_seconds();
    set_current_wlan_channel(slot->bss.channel);
    load_firmware_user_info(&g_download.user);
    return slot;
}

/**
 * @brief Reserves or falls back to a writable output base for the attempt.
 */
static bool download_reserve_output_base(const ContentSlot *slot) {
    if (file_reserve_output_base(g_download.output_base, sizeof(g_download.output_base), slot->title)) {
        return true;
    }
    return file_make_output_fallback_base(g_download.output_base, sizeof(g_download.output_base), NULL) &&
           file_output_base_writable(g_download.output_base);
}

/**
 * @brief Resets diagnostic cursors shared by DATA and FINAL handling.
 */
static void download_reset_debug_cursors(void) {
    s_debug_last_received_log = 0;
    s_debug_last_final_missing = 0xffff;
    s_debug_last_final_received = 0xffff;
    s_debug_last_correction_missing = 0xffff;
    s_debug_last_correction_received = 0xffff;
    s_next_missing_cursor = 0xffffffffu;
}

/**
 * @brief Opens debug diagnostics and records immutable slot metadata.
 */
static void download_open_attempt_log(const ContentSlot *slot) {
    if (!debug_open(g_download.output_base)) {
        ui_log("[%08lx] Could not open debug log.", (unsigned long)slot->id);
    }
    debug_log("start id=%08lx game_group_id=%08lx temporary_group_id=%04x session=%u file_no=%u players=%u user_tag=%u parent_max=%u child_max=%u",
               (unsigned long)slot->id, (unsigned long)slot->game_group_id,
               slot->temporary_group_id, slot->session_id, g_download.expected_file_no,
               slot->connected_count, g_download.user.player_no,
               slot->parent_packet_max_bytes, slot->child_packet_max_bytes);
}

/**
 * @brief Updates the visible verification target for this content slot.
 */
static void download_prepare_verify_status(const ContentSlot *slot) {
    if (g_verifyStatus != VERIFY_FAILED || g_verifyContentId != slot->id) {
        g_verifyStatus = VERIFY_PENDING;
        g_verifyContentId = slot->id;
    }
}

/**
 * @brief Writes the mandatory beacon PCAP diagnostics before association starts.
 */
static bool download_write_beacon_diagnostics(ContentSlot *scan_slot, const ContentSlot *slot) {
    if (pcap_open(g_download.output_base) && pcap_write_beacon_set(slot)) {
        g_download.beacon_pcap_written = true;
        return true;
    }

    ui_count_failure();
    log_download_result(false, "Could not create PCAP with all beacon frames.");
    debug_log("pcap beacon setup failed");
    commit_attempt_diagnostics();
    mark_retry_after_fresh_beacon(scan_slot);
    download_free();
    ui_mark_dirty();
    return false;
}

/**
 * @brief Builds the ARM7 association command from the selected content slot.
 */
static void download_build_connect_params(ConnectParams *cp, const ContentSlot *slot) {
    memset(cp, 0, sizeof(*cp));
    cp->bss = slot->bss;
    cp->game_group_id = slot->game_group_id;
    cp->game_id = slot->game_id;
    cp->temporary_group_id = slot->temporary_group_id;
    cp->parent_packet_max_bytes = slot->parent_packet_max_bytes;
    cp->child_packet_max_bytes = slot->child_packet_max_bytes;
    cp->session_id = slot->session_id;
    cp->file_no = slot->file_no;
    cp->user = g_download.user;
}

/**
 * @brief Commits local state and sends the ARM7 connect command.
 */
static void download_start_association(const ContentSlot *slot, const ConnectParams *cp) {
    ipc_reset_event_queue();
    g_download.ipc_drop_baseline = ipc_dropped_events_snapshot();
    g_download.ipc_drop_seen = g_download.ipc_drop_baseline;
    g_download.active = true;

    g_runState = RUN_CONNECTING;
    ui_log("[%08lx] Connecting to server for file %u…", (unsigned long)slot->id, slot->file_no);
    ui_mark_dirty();
    ipc_send_command(ARM7_CMD_CONNECT, cp, sizeof(*cp));
}

/**
 * @brief Reserves outputs and starts an association/download attempt for a slot.
 *
 * The selected beacon slot supplies packet limits, BSS identity, handover data,
 * and initial PCAP beacon frames. Any failure before association resets the slot
 * so a fresh beacon can be retried without overwriting existing dump files.
 */
void download_start(ContentSlot *scan_slot) {
    if (!scan_slot || g_runState != RUN_SCANNING) return;

    ContentSlot *slot = download_init_attempt_from_slot(scan_slot);
    if (!download_reserve_output_base(slot)) return;

    download_reset_debug_cursors();
    download_open_attempt_log(slot);
    g_download.last_comm_time = download_now_seconds();
    download_prepare_verify_status(slot);
    scan_slot->tried = true;

    if (!download_write_beacon_diagnostics(scan_slot, slot)) return;

    ConnectParams cp;
    download_build_connect_params(&cp, slot);
    download_start_association(slot, &cp);
}

/**
 * @brief Enters download mode after ARM7 reports association success.
 */
void download_handle_connected(unsigned aid) {
    if (!g_download.active) return;
    g_runState = RUN_DOWNLOADING;
    g_download.assoc_aid = (u16)(aid & 0x07ffu);
    debug_log("connected aid=%u user_tag=%u", aid & 0x07ffu, g_download.user.player_no);
    ui_mark_dirty();
    download_touch_communication();
    g_download.name_snippet = 0;
}

/**
 * @brief Asks ARM7 to deauthenticate before the local attempt is torn down.
 */
void download_request_user_abort(void) {
    if (!g_download.active) return;
    if (g_runState != RUN_CONNECTING && g_runState != RUN_DOWNLOADING) return;
    if (g_download.user_abort_requested) return;

    g_download.user_abort_requested = true;
    g_download.user_abort_time = download_now_seconds();
    ui_log("[%08lx] Cancelling download...", (unsigned long)g_download.id);
    debug_log("user abort request state=%u received=%u/%u",
               (unsigned)g_runState,
               g_download.received_packets, g_download.total_packets);
    request_child_cancel();
    ui_mark_dirty();
}

/**
 * @brief Logs association failure, marks the slot retryable and resumes scan.
 */
void download_handle_connect_failed(unsigned reason) {
    if (g_download.user_abort_requested) {
        finish_user_abort("connect_failed");
        return;
    }

    if (g_download.active) ui_count_failure();
    char why[112];
    format_connect_failure(why, sizeof(why), reason);
    log_download_result(false, why);
    debug_log("connect failed reason=%u detail=%s", reason, why);
    mark_retry_after_fresh_beacon(active_scan_slot());
    return_to_scan();
}

/**
 * @brief Converts an ARM7 disconnect event into save, retry or ignored state.
 */
void download_handle_disconnected(void) {
    if (!g_download.active) return;
    if (g_download.user_abort_requested) {
        finish_user_abort("disconnect");
        return;
    }
    if (g_download.saved ||
        g_runState == RUN_CHECKING_RSA_HASH ||
        g_runState == RUN_SAVING ||
        g_runState == RUN_CREATING_REPORT) {
        debug_log("ignored disconnect state=%u saved=%u",
                   (unsigned)g_runState, g_download.saved ? 1u : 0u);
        return;
    }
    if (download_waiting_for_final()) {
        ui_log("[%08lx] Parent disconnected after transfer; saving.", (unsigned long)g_download.id);
        debug_log("disconnected after complete received=%u/%u",
                   g_download.received_packets, g_download.total_packets);
        download_save_and_restart("Parent disconnected after complete");
        return;
    }
    download_abort_and_retry("Disconnected.");
}

/**
 * @brief Aborts the current attempt and leaves the selected slot marked tried.
 */
void download_abort_and_scan(const char *why) {
    if (g_download.active) ui_count_failure();
    log_download_result(false, why ? why : "Unknown error.");
    debug_log("abort scan: %s", why ? why : "unknown");
    ContentSlot *slot = active_scan_slot();
    if (slot) slot->tried = true;
    return_to_scan();
}

/**
 * @brief Aborts the current attempt but clears slot freshness for retry.
 */
void download_abort_and_retry(const char *why) {
    if (g_download.active) ui_count_failure();
    log_download_result(false, why ? why : "Unknown error.");
    debug_log("abort retry: %s", why ? why : "unknown");
    mark_retry_after_fresh_beacon(active_scan_slot());
    return_to_scan();
}

/**
 * @brief Applies transfer-start, final-command and idle communication timeouts.
 */
void download_check_timeout(void) {
    if (!g_download.active) return;
    if (g_runState != RUN_CONNECTING && g_runState != RUN_DOWNLOADING) return;
    if (g_download.user_abort_requested) {
        u32 elapsed = 0;
        if (download_elapsed_seconds(g_download.user_abort_time, &elapsed) &&
            elapsed >= USER_ABORT_GRACE_SECONDS) {
            finish_user_abort("grace_timeout");
        }
        return;
    }
    if (download_waiting_for_final()) {
        u32 now = download_now_seconds();
        if (!now) return;
        if (!g_download.all_packets_time) g_download.all_packets_time = now;
        u32 elapsed = 0;
        if (!download_elapsed_seconds(g_download.all_packets_time, &elapsed)) return;
        if (elapsed < FINAL_WAIT_TIMEOUT_SECONDS) return;
        ui_log("[%08lx] Timed out; saving complete data.", (unsigned long)g_download.id);
        debug_log("final wait timeout received=%u/%u",
                   g_download.received_packets, g_download.total_packets);
        request_child_cancel();
        download_save_and_restart("Final wait timeout");
        return;
    }
    if (download_waiting_for_transfer_start()) {
        u32 elapsed = 0;
        if (!download_elapsed_seconds(g_download.transfer_wait_start_time, &elapsed)) return;
        if (elapsed < TRANSFER_START_TIMEOUT_SECONDS) return;
        download_abort_and_retry("Server did not start the transfer in time.");
        return;
    }
    u32 elapsed = 0;
    if (!download_elapsed_seconds(g_download.last_comm_time, &elapsed)) return;
    if (elapsed < COMM_TIMEOUT_SECONDS) return;
    download_abort_and_retry("Communication timed out.");
}
