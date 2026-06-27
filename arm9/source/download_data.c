/**
 * @file download_data.c
 * @brief Receives DATA commands and fills downloaded section buffers.
 */
#include "download_internal.h"

/**
 * @brief Tests whether a DATA packet number has already been received.
 */
static bool bit_test(u16 p) {
    return (g_download.received_bits[p >> 3] >> (p & 7)) & 1;
}

/**
 * @brief Marks a DATA packet received and seeds the next gap search cursor.
 */
static void bit_set(u16 p) {
    g_download.received_bits[p >> 3] |= (1u << (p & 7));
    s_next_missing_cursor = p;
}

/**
 * @brief Finds the next packet number still missing from the active bitset.
 *
 * @return The next gap to request from the parent, or `total_packets` when the
 *         transfer is complete.
 */
u16 download_next_missing(void) {
    if (!g_download.have_rsa || !g_download.received_bits) return 0;
    if (!g_download.total_packets) return 0;

    u32 cursor = s_next_missing_cursor + 1u;
    if (cursor >= g_download.total_packets) cursor = 0;
    for (u16 scanned = 0; scanned < g_download.total_packets; scanned++) {
        if (!bit_test((u16)cursor)) return (u16)cursor;
        cursor++;
        if (cursor >= g_download.total_packets) cursor = 0;
    }
    return g_download.total_packets;
}

/**
 * @brief Maps a packet number to its target section, section offset and length.
 */
static bool map_packet(u16 pkt, Section **out, u32 *off, u32 *copy_len) {
    if (!g_download.have_rsa || pkt >= g_download.total_packets) return false;
    u32 base = 0;
    for (unsigned i = 0; i < 3; i++) {
        Section *s = &g_download.sec[i];
        u32 count = (s->size + g_download.max_payload - 1) / g_download.max_payload;
        if (pkt < base + count) {
            u32 local = pkt - base;
            *off = local * g_download.max_payload;
            if (*off >= s->size) return false;
            u32 remain = s->size - *off;
            *copy_len = remain > g_download.max_payload ? g_download.max_payload : remain;
            *out = s;
            return true;
        }
        base += count;
    }
    return false;
}

/** @brief Starts data-transfer accounting when the first data packet arrives. */
static void note_transfer_started(u32 event_id, u32 timestamp_us, u16 rx_status) {
    if (!g_download.transfer_started) {
        g_download.transfer_started = true;
        g_download.data_start_time = download_now_seconds();
        debug_log("transfer started ev=%lu ts=%lu st=%04x",
                   (unsigned long)event_id, (unsigned long)timestamp_us,
                   rx_status);
        ui_mark_dirty();
    }
}

/** @brief Rejects data packets for an unexpected file number. */
static bool reject_wrong_file(u16 file_no, u16 pkt, u32 event_id, u32 timestamp_us, u16 rx_status) {
    if (file_no != g_download.expected_file_no) {
        g_download.stats.bad_file_no_packets++;
        debug_log("bad data file_no pkt=%u file_no=%u expected=%u total=%u ev=%lu ts=%lu st=%04x",
                   pkt, file_no, g_download.expected_file_no,
                   g_download.total_packets, (unsigned long)event_id,
                   (unsigned long)timestamp_us, rx_status);
        return true;
    }
    return false;
}

/** @brief Validates a packet's section mapping and payload length, replying on failure. */
static bool resolve_data_packet(u16 pkt, unsigned bytes_len, Section **sec, u32 *off, u32 *copy_len,
                                u32 event_id, u32 timestamp_us, u16 rx_status) {
    if (!map_packet(pkt, sec, off, copy_len)) {
        u16 missing = download_next_missing();
        g_download.stats.out_of_range_data++;
        debug_log("bad data packet pkt=%u len=%u missing=%u total=%u ev=%lu ts=%lu st=%04x",
                   pkt, bytes_len, missing, g_download.total_packets,
                   (unsigned long)event_id, (unsigned long)timestamp_us,
                   rx_status);
        download_send_reply(REPLY_DATA, missing, g_download.total_packets);
        return false;
    }
    if (bytes_len < *copy_len) {
        u16 missing = download_next_missing();
        g_download.stats.short_data_packets++;
        debug_log("short data packet pkt=%u len=%u need=%lu missing=%u ev=%lu ts=%lu st=%04x",
                   pkt, bytes_len, (unsigned long)*copy_len, missing,
                   (unsigned long)event_id, (unsigned long)timestamp_us,
                   rx_status);
        download_send_reply(REPLY_DATA, missing, g_download.total_packets);
        return false;
    }
    return true;
}

/** @brief Copies a new packet or records duplicate-payload statistics. */
static bool accept_or_account_duplicate(u16 pkt, Section *sec, u32 off, u32 copy_len,
                                        const u8 *bytes, u32 event_id, u32 timestamp_us,
                                        u16 rx_status) {
    if (!bit_test(pkt)) {
        memcpy(sec->data + off, bytes, copy_len);
        bit_set(pkt);
        g_download.received_packets++;
        g_download.stats.unique_data_packets++;
        return false;
    }

    bool duplicate_differs = memcmp(sec->data + off, bytes, copy_len) != 0;
    g_download.stats.duplicate_data++;
    if (duplicate_differs) {
        g_download.stats.duplicate_different++;
    } else {
        g_download.stats.duplicate_payload++;
    }
    if (duplicate_differs && s_debug_duplicate_mismatches < 8) {
        u32 diff = copy_len;
        for (u32 i = 0; i < copy_len; i++) {
            if (sec->data[off + i] != bytes[i]) {
                diff = i;
                break;
            }
        }
        u8 old_byte = diff < copy_len ? sec->data[off + diff] : 0;
        u8 new_byte = diff < copy_len ? bytes[diff] : 0;
        s_debug_duplicate_mismatches++;
        debug_log("duplicate data mismatch pkt=%u off=%lu len=%lu ev=%lu ts=%lu st=%04x diff=%lu old=%02x new=%02x count=%u",
                   pkt, (unsigned long)off, (unsigned long)copy_len,
                   (unsigned long)event_id, (unsigned long)timestamp_us, rx_status,
                   (unsigned long)diff, old_byte, new_byte,
                   s_debug_duplicate_mismatches);
    }
    return true;
}

/** @brief Emits periodic transfer-progress debug output. */
static void maybe_log_data_progress(bool already_received, u16 missing_after, u16 pkt) {
    if (!already_received &&
        (u16)(g_download.received_packets - s_debug_last_received_log) >= 256u) {
        debug_log("progress received=%u/%u missing=%u last_pkt=%u",
                   g_download.received_packets, g_download.total_packets,
                   missing_after, pkt);
        s_debug_last_received_log = g_download.received_packets;
    }
}

/** @brief Sends completion replies and saves if the final host command already arrived. */
static bool complete_data_transfer_if_ready(void) {
    if (g_download.received_packets >= g_download.total_packets) {
        if (!g_download.all_packets_time) {
            g_download.all_packets_time = download_now_seconds();
            debug_log("all packets received total=%u final_seen=%u",
                       g_download.total_packets, g_download.final_seen ? 1u : 0u);
            ui_mark_dirty();
        }
        download_send_reply(REPLY_GOT_ALL, g_download.total_packets, g_download.total_packets);
        if (g_download.final_seen) download_save_and_restart("Final command received");
        return true;
    }
    return false;
}

/** @brief Sends a correction reply if the parent should retransmit a missing packet. */
static void send_data_correction_if_needed(u16 pkt, u16 missing_after) {
    u16 expected_next = (u16)(pkt + 1u);
    if (expected_next >= g_download.total_packets) expected_next = 0;
    if (missing_after != expected_next) {
        if (missing_after != s_debug_last_correction_missing ||
            (u16)(g_download.received_packets - s_debug_last_correction_received) >= 256u) {
            debug_log("data correction pkt=%u missing=%u received=%u/%u",
                       pkt, missing_after,
                       g_download.received_packets, g_download.total_packets);
            s_debug_last_correction_missing = missing_after;
            s_debug_last_correction_received = g_download.received_packets;
        }
        download_send_reply(REPLY_DATA, missing_after, g_download.total_packets);
    }
}

/**
 * @brief Copies one parent DATA command into the verified download buffer.
 *
 * Malformed, out-of-range or short packets are accounted in statistics and
 * answered with the current correction request instead of mutating the buffer.
 */
void download_handle_data(const u8 *msg, unsigned len, u32 event_id, u32 timestamp_us, u16 rx_status) {
    if (len < DLP_DATA_HEADER_BYTES || !g_download.have_rsa) return;
    u16 file_no = le16(msg + DLP_DATA_FILE_NO_OFF);
    u16 pkt = le16(msg + DLP_DATA_PACKET_NO_OFF);
    const u8 *bytes = msg + DLP_DATA_PAYLOAD_OFF;
    unsigned bytes_len = len - DLP_DATA_HEADER_BYTES;

    note_transfer_started(event_id, timestamp_us, rx_status);
    if (reject_wrong_file(file_no, pkt, event_id, timestamp_us, rx_status)) return;
    download_touch_communication();

    Section *sec = NULL;
    u32 off = 0, copy_len = 0;
    if (!resolve_data_packet(pkt, bytes_len, &sec, &off, &copy_len, event_id, timestamp_us, rx_status)) return;

    bool already_received = accept_or_account_duplicate(pkt, sec, off, copy_len, bytes,
                                                        event_id, timestamp_us, rx_status);
    u16 missing_after = download_next_missing();
    maybe_log_data_progress(already_received, missing_after, pkt);
    if (complete_data_transfer_if_ready()) return;
    send_data_correction_if_needed(pkt, missing_after);
}
