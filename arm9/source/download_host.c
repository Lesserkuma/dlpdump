/**
 * @file download_host.c
 * @brief Parses parent host commands and drives the download protocol phases.
 */
#include "download_internal.h"

/**
 * @brief Heuristically identifies a malformed host command envelope.
 */
static bool raw_payload_looks_like_host_envelope(const u8 *payload, unsigned len) {
    return dlp_host_envelope_looks_plausible(payload, len);
}

/**
 * @brief Extracts a child reply type from a transmitted raw payload.
 */
static bool extract_child_reply(const u8 *payload, unsigned len, u8 *reply_type) {
    if (!payload || len <= DLP_CHILD_REPLY_TYPE_OFF || !reply_type) return false;
    if (payload[DLP_CHILD_REPLY_FLAG_OFF] != DLP_CHILD_REPLY_FLAG_NORMAL &&
        payload[DLP_CHILD_REPLY_FLAG_OFF] != DLP_CHILD_REPLY_FLAG_ERROR) return false;
    *reply_type = payload[DLP_CHILD_REPLY_TYPE_OFF];
    return true;
}

/**
 * @brief Updates protocol statistics from captured parent and child frames.
 *
 * TX frames account for replies we sent; RX frames account for parent commands
 * even when the command is malformed and later discarded by the parser.
 */
void download_observe_raw_frame(const void *frame, unsigned len, u16 raw_flags) {
    if (!g_download.active || !frame || len <= DOT11_HDR_SIZE) return;
    const u8 *payload = (const u8*)frame + DOT11_HDR_SIZE;
    unsigned payload_len = len - DOT11_HDR_SIZE;

    if ((raw_flags & RAW_DIR_MASK) == RAW_TX) {
        u8 reply = 0;
        if (!extract_child_reply(payload, payload_len, &reply)) return;
        switch (reply) {
            case REPLY_DATA: g_download.stats.correction_replies_sent++; break;
            case REPLY_GOT_ALL: g_download.stats.got_all_replies_sent++; break;
            case REPLY_FINAL: g_download.stats.final_replies_sent++; break;
            default: break;
        }
        return;
    }

    const u8 *msg = NULL;
    unsigned msg_len = 0;
    if (!download_extract_host_msg(payload, payload_len, &msg, &msg_len)) {
        if (raw_payload_looks_like_host_envelope(payload, payload_len)) {
            g_download.stats.malformed_commands++;
        }
        return;
    }

    switch (msg[0]) {
        case 0: break;
        case CMD_NAME_REQUEST: g_download.stats.name_requests_seen++; break;
        case CMD_REJECT: g_download.stats.commands_rejected++; break;
        case CMD_RSA:
            g_download.stats.rsa_packets_seen++;
            if (g_download.stats.rsa_packets_seen > 1) g_download.stats.rsa_repeats++;
            break;
        case CMD_DATA: g_download.stats.data_packets_seen++; break;
        case CMD_FINAL: g_download.stats.final_commands_seen++; break;
        case CMD_CANCEL: break;
        default: g_download.stats.malformed_commands++; break;
    }
}

/**
 * @brief Extracts a bounded DS Download Play host command from an 802.11 payload.
 *
 * @return true only when the envelope is long enough, has the expected flag
 *         bits and contains a recognized command byte.
 */
bool download_extract_host_msg(const u8 *payload, unsigned len, const u8 **msg, unsigned *msg_len) {
    return dlp_host_envelope_parse(payload, len, msg, msg_len);
}

/** @brief Advances the five-part username reply sequence before RSA negotiation. */
static void handle_name_request(void) {
    if (g_download.have_rsa || g_download.received_packets) return;
    download_touch_communication();
    g_download.name_snippet = (g_download.name_snippet + 1) % DLP_USERNAME_SNIPPET_COUNT;
}

/** @brief Applies parent reject/cancel commands without discarding established data. */
static void handle_abort_command(u8 command) {
    download_touch_communication();
    if (!g_download.have_rsa && !g_download.received_packets) {
        download_abort_and_retry(command == CMD_CANCEL ?
                                "Parent cancelled the transfer." :
                                "Parent refused the entry request.");
        return;
    }
    debug_log(command == CMD_CANCEL ?
               "ignored late cancel received=%u/%u" :
               "ignored late type2 received=%u/%u",
               g_download.received_packets, g_download.total_packets);
}

/** @brief Acknowledges repeated RSA frames until the parent starts sending data. */
static bool handle_repeated_rsa(const u8 *msg, unsigned msg_len, u32 event_id,
                                u32 timestamp_us, u16 rx_status) {
    if (!g_download.have_rsa) return false;
    if (g_download.received_packets) return true;
    if (msg_len >= 1 + DOWNLOAD_RSA_FRAME_SIZE &&
        memcmp(&g_download.rsa, msg + 1, DOWNLOAD_RSA_FRAME_SIZE) == 0) {
        download_touch_communication();
        bool first_ack = g_download.rsa_ack_deferred;
        debug_log("rsa repeat%s ev=%lu ts=%lu st=%04x len=%u",
                   first_ack ? " first_ack" : "",
                   (unsigned long)event_id,
                   (unsigned long)timestamp_us,
                   rx_status, msg_len);
        g_download.rsa_ack_deferred = false;
        download_send_reply(REPLY_RSA, 0, g_download.total_packets);
    }
    return true;
}

/** @brief Parses the initial RSA frame and enters data-transfer mode. */
static void handle_rsa_command(const u8 *msg, unsigned msg_len, u32 event_id,
                               u32 timestamp_us, u16 rx_status) {
    if (handle_repeated_rsa(msg, msg_len, event_id, timestamp_us, rx_status)) return;
    if (msg_len >= 1 + DOWNLOAD_RSA_FRAME_SIZE &&
        download_parse_rsa_frame(msg + 1, msg_len - 1)) {
        download_touch_communication();
        debug_log("rsa cmd ev=%lu ts=%lu st=%04x len=%u",
                   (unsigned long)event_id,
                   (unsigned long)timestamp_us,
                   rx_status, msg_len);
        g_download.rsa_ack_deferred = true;
        download_log_user_parameters_if_present(g_download.id);
        ui_log("[%08lx] Downloading… (%u packets × %u bytes)",
                (unsigned long)g_download.id,
                g_download.total_packets,
                download_packet_size());
        return;
    }
    debug_log("bad rsa cmd ev=%lu ts=%lu st=%04x len=%u",
               (unsigned long)event_id,
               (unsigned long)timestamp_us,
               rx_status, msg_len);
    download_abort_and_scan("Invalid RSA control frame.");
}

/** @brief Completes a fully received transfer or asks the parent for the next gap. */
static void handle_final_command(void) {
    if (g_download.have_rsa) download_touch_communication();
    if (g_download.have_rsa && g_download.received_packets >= g_download.total_packets) {
        if (!g_download.stats.final_commands_seen) g_download.stats.final_commands_seen = 1;
        g_download.final_seen = true;
        download_send_reply(REPLY_FINAL, g_download.total_packets, g_download.total_packets);
        download_save_and_restart("Final command received");
        return;
    }
    if (g_download.have_rsa) {
        u16 missing = download_next_missing();
        if (missing != s_debug_last_final_missing ||
            g_download.received_packets != s_debug_last_final_received) {
            debug_log("final before complete missing=%u received=%u/%u",
                       missing, g_download.received_packets, g_download.total_packets);
            s_debug_last_final_missing = missing;
            s_debug_last_final_received = g_download.received_packets;
        }
        download_send_reply(REPLY_DATA, missing, g_download.total_packets);
        return;
    }
    debug_log("final before rsa");
}

/**
 * @brief Dispatches one raw parent payload to the active download state machine.
 */
void download_handle_host_payload(const u8 *payload, unsigned len, u32 event_id, u32 timestamp_us, u16 rx_status) {
    if (g_download.user_abort_requested) return;

    const u8 *msg = NULL;
    unsigned msg_len = 0;
    if (!download_extract_host_msg(payload, len, &msg, &msg_len)) return;

    switch (msg[0]) {
        case CMD_NAME_REQUEST: handle_name_request(); break;
        case CMD_REJECT:
        case CMD_CANCEL:       handle_abort_command(msg[0]); break;
        case CMD_RSA:          handle_rsa_command(msg, msg_len, event_id, timestamp_us, rx_status); break;
        case CMD_DATA:         download_handle_data(msg, msg_len, event_id, timestamp_us, rx_status); break;
        case CMD_FINAL:        handle_final_command(); break;
        default:                  break;
    }
}
