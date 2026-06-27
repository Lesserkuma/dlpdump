#pragma once

/**
 * @file dlp_wire.h
 * @brief Shared DS Download Play wire-format offsets and parser helpers.
 *
 * ARM7 and ARM9 both see the same parent command envelopes and child reply
 * bodies. Keeping the byte offsets here prevents the fast ARM7 reply path and
 * the authoritative ARM9 download state machine from drifting apart.
 */

#include "protocol.h"
#include "types.h"

#define DLP_HOST_ENVELOPE_MIN_BYTES          7u
#define DLP_HOST_ENVELOPE_PREFIX0            0x06u
#define DLP_HOST_ENVELOPE_PREFIX1            0x01u
#define DLP_HOST_ENVELOPE_LEN_HALFWORDS_OFF  4u
#define DLP_HOST_ENVELOPE_FLAGS_OFF          5u
#define DLP_HOST_ENVELOPE_MSG_OFF            6u
#define DLP_HOST_ENVELOPE_MIN_HALFWORDS      3u
#define DLP_HOST_ENVELOPE_REQUIRED_FLAGS     0x11u
#define DLP_HOST_ENVELOPE_TRAILER_BYTES      2u

#define DLP_PARENT_FRAME_OVERHEAD_BYTES      6u
#define DLP_DATA_HEADER_BYTES                5u
#define DLP_DATA_FILE_NO_OFF                 1u
#define DLP_DATA_PACKET_NO_OFF               3u
#define DLP_DATA_PAYLOAD_OFF                 5u

#define DLP_CHILD_REPLY_LEN_DEFAULT_BYTES    10u
#define DLP_CHILD_REPLY_ERROR_BYTES          2u
#define DLP_CHILD_REPLY_LEN_HALFWORDS        4u
#define DLP_CHILD_REPLY_LEN_OFF              0u
#define DLP_CHILD_REPLY_FLAG_OFF             1u
#define DLP_CHILD_REPLY_TYPE_OFF             2u
#define DLP_CHILD_REPLY_PAYLOAD_OFF          3u
#define DLP_CHILD_REPLY_USERNAME_OFF         4u
#define DLP_CHILD_REPLY_FLAG_NORMAL          0x81u
#define DLP_CHILD_REPLY_FLAG_ERROR           0x80u
#define DLP_CHILD_REPLY_EXTRA_GOT_ALL_COUNT  8u

#define DLP_USERNAME_SNIPPET_COUNT           5u
#define DLP_USERNAME_REPLY_PAYLOAD_BYTES     6u
#define DLP_USERNAME_REPLY_SEQUENCE_BYTES    12u

/**
 * @brief Returns whether `command` is a parent command understood by the loader.
 *
 * @param command Raw command byte at `DLP_HOST_ENVELOPE_MSG_OFF`.
 * @return true when the byte identifies a known parent command, including the
 *         idle command zero; otherwise false.
 */
static inline bool dlp_host_command_id_known(u8 command) {
    return command == 0 ||
           command == CMD_NAME_REQUEST ||
           command == CMD_REJECT ||
           command == CMD_RSA ||
           command == CMD_DATA ||
           command == CMD_FINAL ||
           command == CMD_CANCEL;
}

/**
 * @brief Performs the cheap structural checks for a parent command envelope.
 *
 * The helper intentionally does not validate the command ID or declared length;
 * callers use it for diagnostics when a frame looks like Download Play traffic
 * but is rejected by the full parser.
 *
 * @param payload 802.11 payload starting at the Download Play envelope.
 * @param payload_len Number of valid bytes at `payload`.
 * @return true when prefix, minimum length and required flags look plausible.
 */
static inline bool dlp_host_envelope_looks_plausible(const u8 *payload,
                                                        unsigned payload_len) {
    return payload &&
           payload_len >= DLP_HOST_ENVELOPE_MIN_BYTES &&
           payload[0] == DLP_HOST_ENVELOPE_PREFIX0 &&
           payload[1] == DLP_HOST_ENVELOPE_PREFIX1 &&
           payload[DLP_HOST_ENVELOPE_LEN_HALFWORDS_OFF] >= DLP_HOST_ENVELOPE_MIN_HALFWORDS &&
           (payload[DLP_HOST_ENVELOPE_FLAGS_OFF] & DLP_HOST_ENVELOPE_REQUIRED_FLAGS) ==
               DLP_HOST_ENVELOPE_REQUIRED_FLAGS;
}

/**
 * @brief Extracts a bounded parent command message from a Download Play envelope.
 *
 * @param payload 802.11 payload starting at the Download Play envelope.
 * @param payload_len Number of valid bytes at `payload`.
 * @param msg Receives a pointer to the command byte on success.
 * @param msg_len Receives the declared command-message length in bytes on success.
 * @return true when the envelope is structurally valid, its command is known and
 *         the declared message bytes are fully present; otherwise false.
 */
static inline bool dlp_host_envelope_parse(const u8 *payload,
                                              unsigned payload_len,
                                              const u8 **msg,
                                              unsigned *msg_len) {
    if (!payload || !msg || !msg_len) return false;
    *msg = NULL;
    *msg_len = 0;

    if (!dlp_host_envelope_looks_plausible(payload, payload_len)) return false;

    const u8 command = payload[DLP_HOST_ENVELOPE_MSG_OFF];
    if (!dlp_host_command_id_known(command)) return false;

    const unsigned declared_len =
        (unsigned)payload[DLP_HOST_ENVELOPE_LEN_HALFWORDS_OFF] * 2u +
        DLP_HOST_ENVELOPE_TRAILER_BYTES;
    const unsigned required_len = DLP_HOST_ENVELOPE_MSG_OFF + declared_len;
    if (payload_len < required_len) return false;

    *msg = payload + DLP_HOST_ENVELOPE_MSG_OFF;
    *msg_len = declared_len;
    return true;
}
