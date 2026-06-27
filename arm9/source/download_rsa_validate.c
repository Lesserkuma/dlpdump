/**
 * @file download_rsa_validate.c
 * @brief Validates RSA control-frame section ranges and packet counts.
 */
#include "download_rsa.h"
#include "endian.h"
#include "handover.h"
#include "rom_header.h"
#include "../../common/dlp_wire.h"

/** @brief Converts a full parent packet limit into usable DATA payload bytes. */
bool download_rsa_parent_payload_bytes(u16 parent_packet_max_bytes,
                                          u16 *out_max_payload) {
    if (!out_max_payload) return false;
    *out_max_payload = 0;
    if (parent_packet_max_bytes <= DLP_PARENT_FRAME_OVERHEAD_BYTES) return false;
    *out_max_payload = (u16)(parent_packet_max_bytes - DLP_PARENT_FRAME_OVERHEAD_BYTES);
    return *out_max_payload != 0;
}

/** @brief Checks the fixed signature identifier in an RSA control frame. */
bool download_rsa_signature_valid(const DownloadRsaFrame *rsa) {
    return rsa &&
           rsa->signature_id[0] == SIG_ID0 &&
           rsa->signature_id[1] == SIG_ID1 &&
           rsa->signature_id[2] == SIG_ID2 &&
           rsa->signature_id[3] == SIG_ID3;
}

/** @brief Returns whether section 0 is the exact fixed-header handover block. */
static bool download_rsa_fixed_header_section_valid(const DownloadSectionEntry *section) {
    return section &&
           section->load_addr == HANDOVER_FIXED_HEADER_ADDR &&
           section->size == HANDOVER_HEADER_SECTION_BYTES;
}

/** @brief Validates one RSA section entry against memory map and size limits. */
bool download_rsa_section_valid(unsigned section_index,
                                   const DownloadSectionEntry *section) {
    if (!section || section->size > DOWNLOAD_RSA_MAX_SECTION_BYTES) return false;
    if (section_index == 0 && !download_rsa_fixed_header_section_valid(section)) return false;
    return memory_map_validate_download_section(section_index,
                                                   section->load_addr,
                                                   section->size);
}

/** @brief Returns whether section flags match header, ARM9 and ARM7 semantics. */
static bool download_rsa_section_flags_valid(const DownloadRsaFrame *rsa) {
    return rsa &&
           rsa->section[0].flags == 0 &&
           rsa->section[1].flags == 0 &&
           rsa->section[2].flags == 1;
}

/** @brief Validates all RSA section entries against the DS Download Play memory contract. */
bool download_rsa_sections_valid(const DownloadRsaFrame *rsa) {
    if (!rsa) return false;
    for (unsigned i = 0; i < 3; i++) {
        if (!download_rsa_section_valid(i, &rsa->section[i])) return false;
    }
    return true;
}

/** @brief Validates RSA fields that do not depend on the negotiated packet size. */
bool download_rsa_control_struct_valid(const DownloadRsaFrame *rsa) {
    return rsa &&
           rsa->reserved_zero0 == 0 &&
           download_rsa_signature_valid(rsa) &&
           download_rsa_section_flags_valid(rsa) &&
           download_rsa_sections_valid(rsa);
}

/** @brief Computes total transfer packets for a valid RSA frame and payload size. */
bool download_rsa_total_packets(const DownloadRsaFrame *rsa,
                                   u16 max_payload,
                                   u16 *out_total) {
    if (!rsa || !max_payload || !out_total) return false;

    u32 total = 0;
    for (unsigned i = 0; i < 3; i++) {
        u32 size = rsa->section[i].size;
        total += (size + max_payload - 1u) / max_payload;
        if (total > 65535u) return false;
    }
    *out_total = (u16)total;
    return true;
}

/** @brief Validates RSA control fields and derives the expected DATA packet count. */
bool download_rsa_control_valid(const DownloadRsaFrame *rsa,
                                   u16 max_payload,
                                   u16 *out_total) {
    if (!out_total) return false;
    *out_total = 0;
    if (!download_rsa_control_struct_valid(rsa)) return false;
    return download_rsa_total_packets(rsa, max_payload, out_total) && *out_total != 0;
}

/** @brief Validates an RSA frame from the full parent-packet limit. */
bool download_rsa_frame_valid(const DownloadRsaFrame *rsa,
                                 u16 parent_packet_max_bytes,
                                 u16 *out_total) {
    u16 max_payload = 0;
    if (!out_total) return false;
    *out_total = 0;
    if (!download_rsa_parent_payload_bytes(parent_packet_max_bytes, &max_payload)) return false;
    return download_rsa_control_valid(rsa, max_payload, out_total);
}

/** @brief Returns whether section 1/2 load address, size and flags match header fields. */
static bool download_rsa_program_sections_match_header(const DownloadRsaFrame *rsa,
                                                          const u8 *header,
                                                          u32 arm9_size,
                                                          u32 arm7_size) {
    const DownloadSectionEntry *a9 = &rsa->section[1];
    const DownloadSectionEntry *a7 = &rsa->section[2];
    return a9->load_addr == le32(header + ROM_HEADER_ARM9_LOAD_OFF) &&
           a9->size == le32(header + ROM_HEADER_ARM9_SIZE_OFF) &&
           a9->size == arm9_size &&
           a9->flags == 0 &&
           a7->load_addr == le32(header + ROM_HEADER_ARM7_LOAD_OFF) &&
           a7->size == le32(header + ROM_HEADER_ARM7_SIZE_OFF) &&
           a7->size == arm7_size &&
           a7->flags == 1;
}

/** @brief Checks that a control frame matches all executable NDS header fields. */
bool download_rsa_matches_nds_header(const DownloadRsaFrame *rsa,
                                        const u8 *header,
                                        u32 header_size,
                                        u32 arm9_size,
                                        u32 arm7_size) {
    if (!rsa || !header || header_size != HANDOVER_HEADER_SECTION_BYTES) return false;
    if (!download_rsa_control_struct_valid(rsa)) return false;
    if (le32(header + ROM_HEADER_ARM9_SIZE_OFF) != arm9_size) return false;
    if (le32(header + ROM_HEADER_ARM7_SIZE_OFF) != arm7_size) return false;
    if (rsa->arm9_entrypoint != le32(header + ROM_HEADER_ARM9_ENTRY_OFF)) return false;
    if (rsa->arm7_entrypoint != le32(header + ROM_HEADER_ARM7_ENTRY_OFF)) return false;
    if (rsa->section[0].load_addr != HANDOVER_FIXED_HEADER_ADDR) return false;
    if (rsa->section[0].size != header_size || rsa->section[0].flags != 0) return false;
    return download_rsa_program_sections_match_header(rsa, header, arm9_size, arm7_size);
}
