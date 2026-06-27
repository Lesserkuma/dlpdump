#ifndef REPORT_INTERNAL_H
#define REPORT_INTERNAL_H

#include "state.h"
#include "boot.h"
#include "debug.h"
#include "download.h"
#include "file.h"
#include "ipc_arm9.h"
#include "meta.h"
#include "path.h"
#include "pcap.h"
#include "report.h"
#include "scan.h"
#include "system.h"
#include "ui.h"
#include "verify.h"
#include "../../common/rom_header.h"

#include <stdarg.h>
#include <string.h>
#include <time.h>

#define REPORT_SOFTWARE APP_NAME " " APP_VERSION
#define REPORT_VALUE_INDENT            "                         "

/** @brief Writes one aligned key/value line to the text report. */
void report_kv(FILE *f, const char *label, const char *fmt, ...);

/** @brief Writes CRC/MD5/SHA digest information for one saved output file. */
void report_digest(FILE *f, const char *base_name, const char *ext, const FileDigest *d, bool mib);

/** @brief Copies a bounded ASCII ROM-header field into a printable buffer. */
void report_copy_ascii_field(char *out, size_t out_size, const u8 *src, unsigned len);

/** @brief Formats a Unix timestamp as the report's UTC ISO-8601 string. */
void report_format_iso_time(u32 seconds, char *out, size_t out_size);

/** @brief Writes elapsed transfer time and throughput for the saved payload. */
void report_elapsed(FILE *f, const Download *dl, u32 bytes);

/** @brief Writes a possibly multiline metadata text field to the report. */
void report_text_field(FILE *f, const char *label, const char *text);

/** @brief Writes RSA control-frame and section information. */
void report_rsa(FILE *f, const Download *dl);

/** @brief Writes wireless channel, BSSID and signal information. */
void report_wireless(FILE *f, const Download *dl);

/** @brief Writes DS Download Play packet counters and completion state. */
void report_protocol(FILE *f, const Download *dl);

/** @brief Computes the Nintendo DS ROM header CRC16 variant. */
u16 report_crc16_ds(const u8 *data, unsigned len);

/** @brief Computes the one's-complement checksum used by beacon metadata. */
u16 report_checksum16_ones_complement(const u8 *p, unsigned len);

/** @brief Compares stored and calculated DS ROM header CRC values. */
bool report_header_crc_matches(u16 stored, u16 calculated);

/** @brief Writes decoded ROM-header fields and integrity status. */
void report_rom(FILE *f, const u8 *h, u32 hsize);

/** @brief Checks the fixed Nintendo logo bytes in a ROM header. */
bool report_rom_logo_ok(const u8 *h, u32 hsize);

/** @brief Checks the DS ROM header CRC against the calculated value. */
bool report_rom_header_crc_ok(const u8 *h, u32 hsize);

/** @brief Returns the observed beacon interval and flags inconsistent samples. */
u16 report_beacon_interval_tu(const ContentSlot *slot, bool *changed);

/** @brief Detects mismatches between Nintendo-IE and beacon GGID fields. */
bool report_beacon_game_group_ids_mismatch(const ContentSlot *slot,
                                              u32 *ie_game_group_id,
                                              u32 *beacon_game_group_id);

/** @brief Classifies checksum consistency across captured beacon samples. */
int report_beacon_checksum_status(const ContentSlot *slot);

/** @brief Writes raw beacon, vendor IE and metadata-fragment report sections. */
void report_beacon(FILE *f, const Download *dl, const u8 *fixed, unsigned fixed_len);

/** @brief Writes warning lines for suspicious ROM, beacon or PCAP state. */
void report_warnings(FILE *f, const Download *dl, const u8 *h, u32 hsize, bool beacon_interval_changed);

#endif
