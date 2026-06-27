/**
 * @file arm7_reply.c
 * @brief Builds and transmits ARM7 child reply frames for DS Download Play parents.
 */
#include "arm7_internal.h"
#include "dlp_wire.h"

/**
 * @brief Reports whether ARM7 can track every expected packet in its bitset.
 */
static bool arm7_fast_can_track(void) {
    return g_fast_expected_total != 0 && g_fast_expected_total <= FAST_MAX_PACKETS;
}

/**
 * @brief Resets ARM7-side fast packet tracking for a new or cancelled transfer.
 */
void arm7_fast_reset_packets(u16 total_packets) {
    g_fast_next_packet = 0;
    g_fast_received_packets = 0;
    g_fast_expected_total = total_packets;
    g_fast_last_packet = 0xffff;
    g_fast_complete_next = 0;
    g_fast_complete_total = 0;
    g_fast_gotall_replies = 0;
    g_fast_data_complete = false;
    memset(s_fast_received_bits, 0, sizeof(s_fast_received_bits));
}

/**
 * @brief Tests the ARM7-side received bit for a packet number.
 */
static bool arm7_fast_bit_test(u16 packet) {
    return (s_fast_received_bits[packet >> 3] >> (packet & 7)) & 1u;
}

/**
 * @brief Marks a packet received and records it as the last accepted packet.
 */
static void arm7_fast_bit_set(u16 packet) {
    s_fast_received_bits[packet >> 3] |= (u8)(1u << (packet & 7));
    g_fast_last_packet = packet;
}

/**
 * @brief Clears one received bit when ARM9 reports that packet as missing.
 */
static void arm7_fast_bit_clear(u16 packet) {
    s_fast_received_bits[packet >> 3] &= (u8)~(1u << (packet & 7));
}

/**
 * @brief Returns the packet number ARM7 should ask the parent to retransmit.
 */
static u16 arm7_fast_next_missing(void) {
    if (!arm7_fast_can_track()) return g_fast_next_packet;
    if (g_fast_received_packets >= g_fast_expected_total) return g_fast_expected_total;
    return g_fast_next_packet;
}

/**
 * @brief Advances the fast missing cursor past contiguous received packets.
 */
static void arm7_fast_advance_next(void) {
    if (!arm7_fast_can_track()) return;
    while (g_fast_next_packet < g_fast_expected_total &&
           arm7_fast_bit_test(g_fast_next_packet)) {
        g_fast_next_packet++;
    }
}

/**
 * @brief Accounts for one DATA packet accepted by ARM7 fast tracking.
 */
static void arm7_fast_accept_packet(u16 packet) {
    if (arm7_fast_can_track()) {
        if (packet < g_fast_expected_total && !arm7_fast_bit_test(packet)) {
            arm7_fast_bit_set(packet);
            if (g_fast_received_packets != 0xffff) g_fast_received_packets++;
            if (packet == g_fast_next_packet) arm7_fast_advance_next();
        }
        return;
    }

    if (packet == g_fast_next_packet) {
        if (g_fast_next_packet != 0xffff) g_fast_next_packet++;
        if (g_fast_received_packets != 0xffff) g_fast_received_packets++;
    }
}

/**
 * @brief Synchronizes fast tracking with ARM9's authoritative missing packet.
 */
static void arm7_fast_apply_arm9_missing(u16 packet, u16 total_packets) {
    if (total_packets) g_fast_expected_total = total_packets;
    g_fast_next_packet = packet;
    if (!arm7_fast_can_track() || packet >= g_fast_expected_total) return;
    g_fast_last_packet = packet ? (u16)(packet - 1u) : (u16)(g_fast_expected_total - 1u);
    if (arm7_fast_bit_test(packet)) {
        arm7_fast_bit_clear(packet);
        if (g_fast_received_packets) g_fast_received_packets--;
    }
    arm7_fast_advance_next();
}

/**
 * @brief Writes raw frame bytes into the Mitsumi transmit buffer register.
 */
static void arm7_tx_write(const void *src, unsigned len) {
    const u16 *src16 = (const u16*)src;
    while (len > 1) {
        MWL_REG(W_TXBUF_WR_DATA) = *src16++;
        len -= 2;
    }
    if (len) {
        MWL_REG(W_TXBUF_WR_DATA) = *(const u8*)src16;
    }
}

/**
 * @brief Builds the six-byte username fragment for a child reply.
 */
static void arm7_fill_username_snippet(ReplyParams *p) {
    build_username_snippet(p, g_active_parent.game_id, g_active_parent.file_no, &g_active_parent.user);
}

/**
 * @brief Queues the next username reply in the parent-expected sequence.
 */
static void arm7_queue_next_username_reply(void) {
    static const u8 username_reply_sequence[DLP_USERNAME_REPLY_SEQUENCE_BYTES] = { 1, 2, 3, 4, 0, 1, 2, 3, 4, 0, 1, 2 };
    u8 item;
    if (g_name_reply_step < ARRAY_COUNT(username_reply_sequence)) {
        item = username_reply_sequence[g_name_reply_step++];
    } else {
        item = (u8)((g_name_reply_step++ - ARRAY_COUNT(username_reply_sequence)) % 5);
    }

    ReplyParams p;
    memset(&p, 0, sizeof(p));
    p.reply_type = REPLY_USERNAME;
    p.user_snippet_no = item;
    arm7_fill_username_snippet(&p);
    arm7_write_reply_frame(&p);
}

/**
 * @brief Queues a DATA correction or GOT_ALL reply from ARM7 fast state.
 */
static void arm7_queue_fast_data_reply(u16 packet, bool accepted) {
    if (g_fast_data_complete) {
        if (!g_fast_gotall_replies) return;
        g_fast_gotall_replies--;
        ReplyParams p;
        memset(&p, 0, sizeof(p));
        p.reply_type = REPLY_GOT_ALL;
        p.next_packet = g_fast_complete_next;
        p.total_packets = g_fast_complete_total;
        arm7_write_reply_frame(&p);
        return;
    }

    if (accepted) arm7_fast_accept_packet(packet);

    ReplyParams p;
    memset(&p, 0, sizeof(p));
    p.reply_type = REPLY_DATA;
    u16 next = arm7_fast_next_missing();
    if (g_fast_expected_total && next >= g_fast_expected_total) {
        p.next_packet = g_fast_expected_total - 1u;
    } else {
        p.next_packet = next;
    }
    p.total_packets = g_fast_expected_total;
    arm7_write_reply_frame(&p);
}

#define ARM7_REPLY_BODY_BYTES 16u

/**
 * @brief Initializes a normal child reply body with a type byte.
 */
static void arm7_reply_body_init(u8 body[ARM7_REPLY_BODY_BYTES], u8 reply_type) {
    memset(body, 0, ARM7_REPLY_BODY_BYTES);
    body[DLP_CHILD_REPLY_LEN_OFF] = DLP_CHILD_REPLY_LEN_HALFWORDS;
    body[DLP_CHILD_REPLY_FLAG_OFF] = DLP_CHILD_REPLY_FLAG_NORMAL;
    body[DLP_CHILD_REPLY_TYPE_OFF] = reply_type;
}

/**
 * @brief Encodes next/total packet counters into a child reply payload.
 */
static void arm7_reply_encode_counters(u8 body[ARM7_REPLY_BODY_BYTES], u16 next, u16 total) {
    stle16(body + DLP_CHILD_REPLY_PAYLOAD_OFF, next);
    stle16(body + DLP_CHILD_REPLY_PAYLOAD_OFF + 2u, total);
}

/**
 * @brief Encodes the firmware username fragment requested by the parent.
 */
static void arm7_reply_encode_username(const ReplyParams *p, u8 body[ARM7_REPLY_BODY_BYTES]) {
    body[DLP_CHILD_REPLY_PAYLOAD_OFF] = p->user_snippet_no;
    memcpy(body + DLP_CHILD_REPLY_USERNAME_OFF, p->user_payload, DLP_USERNAME_REPLY_PAYLOAD_BYTES);
    memcpy(g_client_msg_tail, body + DLP_CHILD_REPLY_PAYLOAD_OFF, sizeof(g_client_msg_tail));
    g_have_client_msg_tail = true;
}

/**
 * @brief Replays the username tail and initializes fast DATA tracking after RSA.
 */
static void arm7_reply_encode_rsa(const ReplyParams *p, bool update_state,
                                 u8 body[ARM7_REPLY_BODY_BYTES]) {
    if (g_have_client_msg_tail) {
        memcpy(body + DLP_CHILD_REPLY_PAYLOAD_OFF, g_client_msg_tail, sizeof(g_client_msg_tail));
    }
    if (update_state) arm7_fast_reset_packets(p->total_packets);
}

/**
 * @brief Encodes a DATA acknowledgement and applies ARM9's next-missing cursor.
 */
static void arm7_reply_encode_data(const ReplyParams *p, bool update_state,
                                  u8 body[ARM7_REPLY_BODY_BYTES]) {
    if (update_state) {
        g_fast_data_complete = false;
        arm7_fast_apply_arm9_missing(p->next_packet, p->total_packets);
    }
    arm7_reply_encode_counters(body, p->next_packet, p->total_packets);
}

/**
 * @brief Encodes the all-packets-received reply and schedules poll repeats.
 */
static void arm7_reply_encode_got_all(const ReplyParams *p, bool update_state,
                                    u8 body[ARM7_REPLY_BODY_BYTES]) {
    arm7_reply_encode_counters(body, p->next_packet, p->total_packets);
    if (!update_state) return;
    if (!g_fast_data_complete ||
        g_fast_complete_next != p->next_packet ||
        g_fast_complete_total != p->total_packets) {
        g_fast_gotall_replies = DLP_CHILD_REPLY_EXTRA_GOT_ALL_COUNT;
    }
    g_fast_complete_next = p->next_packet;
    g_fast_complete_total = p->total_packets;
    g_fast_data_complete = true;
}

/**
 * @brief Encodes the final child reply and clears fast completion state.
 */
static void arm7_reply_encode_final(const ReplyParams *p, bool update_state,
                                   u8 body[ARM7_REPLY_BODY_BYTES]) {
    u16 next = p->total_packets ? p->next_packet : g_fast_complete_next;
    u16 total = p->total_packets ? p->total_packets : g_fast_complete_total;
    arm7_reply_encode_counters(body, next, total);
    if (update_state) {
        g_fast_gotall_replies = 0;
        g_fast_data_complete = false;
    }
}

/**
 * @brief Encodes the short error reply used for unsupported reply types.
 */
static unsigned arm7_reply_encode_error(u8 body[ARM7_REPLY_BODY_BYTES]) {
    memset(body, 0, ARM7_REPLY_BODY_BYTES);
    body[DLP_CHILD_REPLY_FLAG_OFF] = DLP_CHILD_REPLY_FLAG_ERROR;
    return DLP_CHILD_REPLY_ERROR_BYTES;
}

/**
 * @brief Encodes the DS Download Play child reply body and updates transfer state.
 *
 * @param p Reply parameters supplied by ARM9 or the fast-reply tracker.
 * @param update_state Whether packet cursors and completion counters should be
 *        advanced while encoding this body.
 * @param body Output buffer for the 802.11 payload bytes before the FCS.
 * @return Number of body bytes that must be transmitted.
 */
static unsigned arm7_build_reply_body(const ReplyParams *p, bool update_state,
                                     u8 body[ARM7_REPLY_BODY_BYTES]) {
    arm7_reply_body_init(body, p->reply_type);

    switch (p->reply_type) {
        case REPLY_DUMMY:
            break;
        case REPLY_USERNAME:
            arm7_reply_encode_username(p, body);
            break;
        case REPLY_RSA:
            arm7_reply_encode_rsa(p, update_state, body);
            break;
        case REPLY_DATA:
            arm7_reply_encode_data(p, update_state, body);
            break;
        case REPLY_GOT_ALL:
            arm7_reply_encode_got_all(p, update_state, body);
            break;
        case REPLY_FINAL:
            arm7_reply_encode_final(p, update_state, body);
            break;
        default:
            return arm7_reply_encode_error(body);
    }

    return DLP_CHILD_REPLY_LEN_DEFAULT_BYTES;
}

/**
 * @brief Writes an encoded child reply into the hardware MP reply slot.
 *
 * The frame is addressed to the active parent BSSID, uses the DS Download Play
 * multicast destination expected by parents, and is also mirrored into the raw
 * capture queue when capture is enabled.
 */
static void arm7_write_reply_tx_frame(const u8 *body, unsigned body_len) {
    u8 frame[sizeof(MwlDataTxHdr) + sizeof(WlanMacHdr) + ARM7_REPLY_BODY_BYTES];
    memset(frame, 0, sizeof(frame));

    MwlDataTxHdr *tx = (MwlDataTxHdr*)frame;
    WlanMacHdr *wh = (WlanMacHdr*)(frame + sizeof(MwlDataTxHdr));
    u8 *payload = (u8*)(wh + 1);

    tx->status = 0;
    tx->mp_aid_mask = 0;
    tx->retry_count = 0;
    tx->app_rate = 0;
    tx->service_rate = 20;
    tx->mpdu_len = sizeof(WlanMacHdr) + body_len + 4;

    arm7_set_power_state(2);
    wh->fc.value = WIFI_FC_DATA_CF_ACK_TODS_PM; /* Data + CF-ACK, ToDS=1, power-management=1 for child reply frames. */
    wh->duration = 0;
    static const u8 nintendo_multicast_destination[6] = { 0x03, 0x09, 0xbf, 0x00, 0x00, 0x10 };
    memcpy(wh->rx_addr, g_active_parent.bss.bssid, 6);
    memcpy(wh->xtra_addr, nintendo_multicast_destination, 6);
    memcpy(wh->tx_addr, mwlGetCalibData()->mac_addr, 6);
    wh->sc.value = (g_tx_seq++ << 4);
    memcpy(payload, body, body_len);

    /*
     * Child reply frames are written directly to the MP reply slot. Normal
     * LOC queues are for explicit data TX and do not behave as slave replies.
     */
    u16 pos = s_mwlState.tx_reply_pos[g_reply_buf_index & 1u];
    g_reply_buf_index++;
    MWL_REG(W_TXBUF_WR_ADDR) = pos;
    arm7_tx_write(frame, sizeof(MwlDataTxHdr) + sizeof(WlanMacHdr) + body_len);
    MWL_REG(W_TXBUF_REPLY1) = (pos / 2) | 0x8000;

    if (g_raw_capture_enabled) {
        arm7_push_raw_event(RAW_TX, wh, sizeof(WlanMacHdr) + body_len);
    }
}

/**
 * @brief Builds and writes a reply, optionally updating and remembering state.
 */
static void arm7_write_reply_frame_internal(const ReplyParams *p, bool update_state, bool remember) {
    if (remember) {
        g_pending_reply = *p;
        g_have_pending_reply = true;
    }

    u8 body[ARM7_REPLY_BODY_BYTES];
    unsigned body_len = arm7_build_reply_body(p, update_state, body);
    arm7_write_reply_tx_frame(body, body_len);
}

/**
 * @brief Encodes and writes a child reply, remembering it for retransmission.
 */
void arm7_write_reply_frame(const ReplyParams *p) {
    arm7_write_reply_frame_internal(p, true, true);
}

/**
 * @brief Rewrites the last remembered reply without changing fast state.
 */
void arm7_rewrite_pending_reply(void) {
    if (g_have_pending_reply) {
        arm7_write_reply_frame_internal(&g_pending_reply, false, false);
    }
}

/**
 * @brief Forwards a captured raw frame to ARM9 when capture is enabled.
 *
 * RSSI is converted to both raw and percentage signal flags when RX metadata
 * is available.
 */
void arm7_raw_frame(const void *frame, unsigned len, const MwlDataRxHdr *rxhdr, MwlRxType type) {
    (void)type;
    if (g_raw_capture_enabled) {
        u16 flags = RAW_RX;
        if (rxhdr) {
            u8 rssi = mwlDecodeRssi(rxhdr->rssi);
            flags |= RAW_SIGNAL_VALID;
            flags |= (u16)(rssi & 0x3fu) << RAW_RSSI_SHIFT;
            flags |= (u16)arm7_signal_percent_from_rssi(rssi) << RAW_SIGNAL_SHIFT;
        }
        arm7_push_raw_event(flags, frame, len);
    }
}

/**
 * @brief Returns the multicast target bit for the local association ID.
 */
static u16 arm7_local_aid_bit(void) {
    u16 aid = g_assoc_aid & 0x000f;
    if (!aid || aid >= 16) return 0;
    return (u16)(1u << aid);
}

/**
 * @brief Extracts the target-AID mask from a parent command envelope.
 */
static u16 arm7_host_target_mask(const u8 *payload, unsigned payload_len) {
    if (payload_len < 4) return 0;
    if (payload_len >= 6) {
        u16 header = le16(payload + 4);
        unsigned off = 6u + (unsigned)(header & 0x00ffu) * 2u;
        if (header & 0x0800u) off += 2u;
        if ((header & 0x1000u) && off + 2u <= payload_len) {
            return le16(payload + off);
        }
    }
    return le16(payload + 2);
}

/**
 * @brief Checks whether a parent command envelope is addressed to this child.
 */
static bool arm7_host_payload_targets_local(const u8 *payload, unsigned payload_len) {
    u16 mask = arm7_host_target_mask(payload, payload_len);
    u16 local = arm7_local_aid_bit();
    if (!mask || !local) return true;
    return (mask & local) != 0;
}

/**
 * @brief Extracts the bounded host command from a DS Download Play payload.
 */
static bool arm7_extract_host_msg(const u8 *payload, unsigned payload_len, const u8 **msg, unsigned *msg_len) {
    return dlp_host_envelope_parse(payload, payload_len, msg, msg_len);
}

/**
 * @brief Rewrites the pending reply and mirrors an ignored command frame.
 */
static void arm7_rewrite_and_capture(const u8 *frame, unsigned len, const MwlDataRxHdr *rxhdr) {
    arm7_rewrite_pending_reply();
    arm7_raw_frame(frame, len, rxhdr, MwlRxType_MpCmdFrame);
}

/**
 * @brief Handles the parent username request command.
 */
static void arm7_handle_name_request(const u8 *frame, unsigned len, const MwlDataRxHdr *rxhdr) {
    if (!g_seen_rsa_cmd) {
        arm7_queue_next_username_reply();
    } else {
        arm7_rewrite_pending_reply();
    }
    arm7_raw_frame(frame, len, rxhdr, MwlRxType_MpCmdFrame);
}

/**
 * @brief Forwards parent reject/cancel commands to ARM9.
 */
static void arm7_handle_reject_or_cancel(u16 host_flags, const u8 *payload, unsigned payload_len,
                                       const u8 *frame, unsigned len, const MwlDataRxHdr *rxhdr) {
    arm7_push_event(EVENT_HOST_CMD, host_flags, payload, payload_len);
    arm7_raw_frame(frame, len, rxhdr, MwlRxType_MpCmdFrame);
}

/**
 * @brief Handles an RSA command and resets fast-completion state when needed.
 */
static void arm7_handle_rsa_command(void) {
    if (!g_seen_rsa_cmd) {
        g_seen_rsa_cmd = true;
        arm7_queue_next_username_reply();
    }
    if (!g_fast_next_packet && !g_fast_received_packets && !g_fast_data_complete) {
        g_fast_complete_next = 0;
        g_fast_complete_total = 0;
        g_fast_gotall_replies = 0;
    }
}

/**
 * @brief Handles a DATA command and emits the fast acknowledgement.
 */
static bool arm7_handle_data_command(const u8 *msg, unsigned msg_len, u16 host_flags,
                                    const u8 *payload, unsigned payload_len,
                                    const u8 *frame, unsigned len, const MwlDataRxHdr *rxhdr) {
    if (msg_len < DLP_DATA_HEADER_BYTES) return false;

    u16 file_no = le16(msg + DLP_DATA_FILE_NO_OFF);
    u16 packet = le16(msg + DLP_DATA_PACKET_NO_OFF);
    if (file_no != g_active_parent.file_no) {
        arm7_raw_frame(frame, len, rxhdr, MwlRxType_MpCmdFrame);
        return true;
    }

    bool queued = arm7_push_host_data_event(host_flags, payload, payload_len);
    arm7_queue_fast_data_reply(packet, queued);
    arm7_raw_frame(frame, len, rxhdr, MwlRxType_MpCmdFrame);
    return true;
}

/**
 * @brief Emits the final reply when the fast tracker has all packets.
 */
static void arm7_handle_final_command(void) {
    if (!g_fast_data_complete) return;

    ReplyParams p;
    memset(&p, 0, sizeof(p));
    p.reply_type = REPLY_FINAL;
    p.next_packet = g_fast_complete_next;
    p.total_packets = g_fast_complete_total;
    arm7_write_reply_frame(&p);
}

/**
 * @brief Processes a parent multiplayer command addressed to this child.
 *
 * Non-targeted or malformed payloads cause the pending reply to be rewritten so
 * the parent still receives an MP reply. Valid commands update fast-transfer
 * packet tracking, forward host payloads to ARM9, and queue the corresponding
 * username/data/final child reply.
 */
void arm7_mp_cmd_frame(const u8 *frame, unsigned len, const MwlDataRxHdr *rxhdr) {
    u16 host_flags = rxhdr ? rxhdr->status : 0xffffu;
    if (g_scan_enabled || !arm7_frame_from_active_parent(frame, len)) return;
    if (len <= sizeof(WlanMacHdr)) return;

    const u8 *payload = frame + sizeof(WlanMacHdr);
    unsigned payload_len = len - sizeof(WlanMacHdr);
    const u8 *msg = NULL;
    unsigned msg_len = 0;
    arm7_set_power_state(1);

    if (!arm7_host_payload_targets_local(payload, payload_len) ||
        !arm7_extract_host_msg(payload, payload_len, &msg, &msg_len)) {
        arm7_rewrite_and_capture(frame, len, rxhdr);
        return;
    }

    switch (msg[0]) {
        case CMD_IDLE:
            arm7_rewrite_and_capture(frame, len, rxhdr);
            return;
        case CMD_NAME_REQUEST:
            arm7_handle_name_request(frame, len, rxhdr);
            return;
        case CMD_REJECT:
        case CMD_CANCEL:
            arm7_handle_reject_or_cancel(host_flags, payload, payload_len, frame, len, rxhdr);
            return;
        case CMD_RSA:
            arm7_handle_rsa_command();
            break;
        case CMD_DATA:
            if (arm7_handle_data_command(msg, msg_len, host_flags, payload, payload_len,
                                        frame, len, rxhdr)) {
                return;
            }
            break;
        case CMD_FINAL:
            arm7_handle_final_command();
            break;
        default:
            break;
    }

    arm7_push_event(EVENT_HOST_CMD, host_flags, payload, payload_len);
    arm7_raw_frame(frame, len, rxhdr, MwlRxType_MpCmdFrame);
}

/**
 * @brief Emits extra GOT_ALL replies in response to multiplayer poll frames.
 */
void arm7_mp_poll_frame(const u8 *frame, unsigned len, const MwlDataRxHdr *rxhdr) {
    (void)rxhdr;
    if (g_scan_enabled || !arm7_frame_from_active_parent(frame, len)) return;
    arm7_set_power_state(1);
    if (!g_fast_data_complete) return;
    if (!g_fast_gotall_replies) return;
    g_fast_gotall_replies--;

    ReplyParams p;
    memset(&p, 0, sizeof(p));
    p.reply_type = REPLY_GOT_ALL;
    p.next_packet = g_fast_complete_next;
    p.total_packets = g_fast_complete_total;
    arm7_write_reply_frame(&p);

    arm7_raw_frame(frame, len, rxhdr, MwlRxType_MpReplyFrame);
}

/**
 * @brief Stores the association ID assigned by the parent.
 */
void arm7_assoc_aid(u16 aid) {
    g_assoc_aid = aid & 0x07ff;
}
