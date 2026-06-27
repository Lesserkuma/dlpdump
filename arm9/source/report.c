/**
 * @file report.c
 * @brief Writes the top-level text report for a completed dump.
 */
#include "report_internal.h"

/**
 * @brief Writes the complete text report to an already opened file.
 */
bool report_write_to_file(FILE *f, const Download *dl, const char *base_name,
                         const u8 *nds_header, u32 nds_header_size,
                         const FileDigest *nds_digest,
                         const FileDigest *bcn_digest) {
    if (!f || !dl || !dl->slot || !base_name || !base_name[0] || !nds_digest || !bcn_digest) return false;

    u8 fixed[BEACON_FIXED_INFO_MAX];
    unsigned fixed_len = 0;
    if (!meta_build_fixed_info(dl->slot, fixed, &fixed_len)) {
        fixed_len = 0;
    }

    bool interval_changed = false;
    report_beacon_interval_tu(dl->slot, &interval_changed);

    fputs("= DS Download Play Dump Report =\n\n", f);

    fputs("== File Information ==\n", f);
    fputs("=== NDS File ===\n", f);
    report_digest(f, base_name, ".nds", nds_digest, true);
    fputs("=== BCN File ===\n", f);
    report_digest(f, base_name, ".bcn", bcn_digest, false);
    fputc('\n', f);

    char time_text[48];
    report_format_iso_time(dl->completion_time, time_text, sizeof(time_text));
    fputs("== General Information ==\n", f);
    report_kv(f, "Software:", "%s", REPORT_SOFTWARE);
    report_kv(f, "Platform:", "%s", systemIsTwlMode() ? "TWL" : "NTR");
    report_kv(f, "System Language:", "%s", system_language_name());
    report_kv(f, "Dump Time:", "%s", time_text);
    report_kv(f, "Completion Reason:", "%s", dl->completion_reason[0] ? dl->completion_reason : "N/A");
    report_elapsed(f, dl, nds_digest->size);
    fputc('\n', f);

    fputs("== Beacon Parsed Data ==\n", f);
    report_beacon(f, dl, fixed, fixed_len);
    fputc('\n', f);

    fputs("== RSA Parsed Data ==\n", f);
    report_rsa(f, dl);
    fputc('\n', f);

    fputs("== ROM Parsed Data ==\n", f);
    report_rom(f, nds_header, nds_header_size);
    fputc('\n', f);

    fputs("== Wireless Information ==\n", f);
    report_wireless(f, dl);
    fputc('\n', f);

    fputs("== Protocol Statistics ==\n", f);
    report_protocol(f, dl);
    fputc('\n', f);

    report_warnings(f, dl, nds_header, nds_header_size, interval_changed);

    return ferror(f) == 0;
}

/**
 * @brief Writes the text report atomically through a temporary file.
 */
bool report_write(const Download *dl, const char *base_name,
                   const u8 *nds_header, u32 nds_header_size,
                   const FileDigest *nds_digest,
                   const FileDigest *bcn_digest) {
    char path[256];
    if (!base_name || !path_make_output_file(path, sizeof(path), base_name, ".txt")) return false;
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = report_write_to_file(f, dl, base_name, nds_header, nds_header_size,
                                  nds_digest, bcn_digest);
    ok = fflush(f) == 0 && ok;
    ok = fclose(f) == 0 && ok;
    return ok;
}
