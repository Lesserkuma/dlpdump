#pragma once

#include "memory_map.h"
#include "../../common/protocol.h"

/**
 * @file download_rsa.h
 * @brief Host-testable DS Download Play RSA control-frame validation helpers.
 */

#define DOWNLOAD_RSA_MAX_SECTION_BYTES (4u * 1024u * 1024u)

/**
 * @brief Converts a parent MP frame limit into usable DATA payload bytes.
 *
 * @param parent_packet_max_bytes Full parent frame capacity from beacon/RSA context.
 * @param out_max_payload Receives bytes available after DLP parent-frame overhead.
 * @return true when the parent packet size is large enough for DATA payloads.
 */
bool download_rsa_parent_payload_bytes(u16 parent_packet_max_bytes,
                                          u16 *out_max_payload);

/**
 * @brief Checks the fixed signature-id bytes in an RSA control frame.
 */
bool download_rsa_signature_valid(const DownloadRsaFrame *rsa);

/**
 * @brief Validates one declared download section against the DS memory map.
 *
 * @param section_index Section number: 0 header, 1 ARM9, 2 ARM7.
 * @param section Section descriptor from the RSA frame.
 * @return true if the destination range and size are acceptable.
 */
bool download_rsa_section_valid(unsigned section_index,
                                   const DownloadSectionEntry *section);

/** @brief Validates all three section descriptors in an RSA control frame. */
bool download_rsa_sections_valid(const DownloadRsaFrame *rsa);

/**
 * @brief Validates fixed RSA control-frame fields independent of packet size.
 *
 * @param rsa Control frame to inspect.
 * @return true when reserved fields, signature identifier, section flags and
 *         section ranges match the DS Download Play contract.
 */
bool download_rsa_control_struct_valid(const DownloadRsaFrame *rsa);

/**
 * @brief Validates an RSA control frame and computes the DATA packet count.
 *
 * @param rsa Control frame received from the parent.
 * @param parent_packet_max_bytes Full parent frame capacity before DLP overhead.
 * @param out_total Receives total packet count when validation succeeds.
 * @return false on malformed control fields, invalid sections, too-small parent
 *         packets or u16 packet-count overflow.
 */
bool download_rsa_frame_valid(const DownloadRsaFrame *rsa,
                                 u16 parent_packet_max_bytes,
                                 u16 *out_total);

/**
 * @brief Validates an RSA control frame using already-computed DATA payload bytes.
 *
 * @param rsa Control frame received from the parent.
 * @param max_payload Usable parent DATA payload bytes after frame overhead.
 * @param out_total Receives total packet count when validation succeeds.
 * @return false on malformed control fields, invalid sections, zero payload size
 *         or u16 packet-count overflow.
 */
bool download_rsa_control_valid(const DownloadRsaFrame *rsa,
                                   u16 max_payload,
                                   u16 *out_total);

/**
 * @brief Computes DATA packet count from section sizes and payload capacity.
 *
 * @return false on NULL input, zero payload size, or u16 packet-count overflow.
 */
bool download_rsa_total_packets(const DownloadRsaFrame *rsa,
                                   u16 max_payload,
                                   u16 *out_total);

/**
 * @brief Checks that an RSA frame exactly matches the rebuilt NDS header fields.
 *
 * @param rsa Control frame to compare.
 * @param header NDS header bytes containing entrypoints, load addresses and sizes.
 * @param header_size Size of the fixed-header section described by section 0.
 * @param arm9_size Expected ARM9 section size.
 * @param arm7_size Expected ARM7 section size.
 * @return true when all frame, fixed-header, ARM9 and ARM7 invariants match.
 */
bool download_rsa_matches_nds_header(const DownloadRsaFrame *rsa,
                                        const u8 *header,
                                        u32 header_size,
                                        u32 arm9_size,
                                        u32 arm7_size);
