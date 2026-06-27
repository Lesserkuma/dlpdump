/**
 * @file report_warnings.c
 * @brief Emits report warnings for suspicious dump, beacon and PCAP state.
 */
#include "report_internal.h"

/**
 * @brief Writes one warning line to the report.
 */
static void report_warning(FILE *f, unsigned *count, const char *text) {
    if (!*count) fputs("== Notes ==\n", f);
    fprintf(f, "* %s\n", text);
    (*count)++;
}

/**
 * @brief Writes report warnings derived from transfer and output status.
 */
void report_warnings(FILE *f, const Download *dl, const u8 *h, u32 hsize, bool beacon_interval_changed) {
    unsigned count = 0;
    if (!dl->final_seen) report_warning(f, &count, "Final command was not seen; saved after timeout or disconnect.");
    if (dl->pcap_error) report_warning(f, &count, "PCAP capture hit a write, flush or close error; the PCAP file is incomplete or missing.");
    if (beacon_interval_changed) report_warning(f, &count, "Beacon interval changed during capture.");
    u32 ie_game_group_id = 0, beacon_game_group_id = 0;
    if (report_beacon_game_group_ids_mismatch(dl->slot, &ie_game_group_id, &beacon_game_group_id)) {
        char warning[96];
        snprintf(warning, sizeof(warning),
                 "Nintendo IE GGID (0x%08lX) does not match beacon GGID (0x%08lX).",
                 (unsigned long)ie_game_group_id, (unsigned long)beacon_game_group_id);
        report_warning(f, &count, warning);
    }
    if (report_beacon_checksum_status(dl->slot) == 0) report_warning(f, &count, "Beacon checksum check failed.");
    if (dl->stats.duplicate_different) report_warning(f, &count, "Duplicate packet with different payload observed.");
    if (dl->slot && dl->start_temporary_group_id && dl->slot->temporary_group_id != dl->start_temporary_group_id) report_warning(f, &count, "TGID changed between scan and connect.");
    if (h && hsize > ROM_HEADER_UNIT_CODE_OFF && h[ROM_HEADER_UNIT_CODE_OFF] != 0x00) report_warning(f, &count, "ROM Unit Code is not NTR-only.");
    if (!report_rom_logo_ok(h, hsize)) report_warning(f, &count, "ROM logo CRC check failed.");
    if (!report_rom_header_crc_ok(h, hsize)) report_warning(f, &count, "ROM header checksum check failed.");
    if (dl->stats.packets_dropped) report_warning(f, &count, "IPC/event drops occurred during capture.");
    if (dl->stats.malformed_commands) report_warning(f, &count, "Malformed commands were observed.");
    if (dl->stats.bad_file_no_packets) report_warning(f, &count, "Data packets for an unexpected file number were observed.");
    if (dl->stats.out_of_range_data) report_warning(f, &count, "Out-of-range data packets were observed.");
    if (dl->stats.short_data_packets) report_warning(f, &count, "Short data packets were observed.");
}
