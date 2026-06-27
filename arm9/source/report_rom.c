/**
 * @file report_rom.c
 * @brief Decodes DS ROM header fields and integrity checks for reports.
 */
#include "report_internal.h"

/**
 * @brief Returns the report label for an NDS ROM unit code.
 */
static const char *unit_code_name(u8 unit) {
    switch (unit) {
        case 0x00: return "Nintendo DS";
        case 0x02: return "Nintendo DS/DSi";
        case 0x03: return "Nintendo DSi";
        default: return "Unknown";
    }
}

/**
 * @brief Computes the NDS ROM-header CRC16 used by Nintendo DS headers.
 */
u16 report_crc16_ds(const u8 *data, unsigned len) {
    u16 crc = 0xffffu;
    for (unsigned i = 0; i < len; i++) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; bit++) {
            crc = (crc & 1u) ? (u16)((crc >> 1) ^ 0xa001u) : (u16)(crc >> 1);
        }
    }
    return crc;
}

/**
 * @brief Returns whether the ROM header CRC matches its stored value.
 */
bool report_header_crc_matches(u16 stored, u16 calculated) {
    if (stored == calculated) return true;
    return (stored & 0xff00u) == 0 && (stored & 0x00ffu) == (calculated & 0x00ffu);
}

/**
 * @brief Writes ROM header identity and integrity fields to the report.
 */
void report_rom(FILE *f, const u8 *h, u32 hsize) {
    if (!h || hsize < ROM_HEADER_MIN_BYTES) {
        report_kv(f, "ROM Title:", "N/A");
        return;
    }
    char title[13], game_code[5];
    report_copy_ascii_field(title, sizeof(title), h + ROM_HEADER_TITLE_OFF, ROM_HEADER_TITLE_BYTES);
    report_copy_ascii_field(game_code, sizeof(game_code), h + ROM_HEADER_GAME_CODE_OFF, ROM_HEADER_GAME_CODE_BYTES);
    u8 unit = h[ROM_HEADER_UNIT_CODE_OFF];
    u16 logo_crc = le16(h + ROM_HEADER_LOGO_CRC_OFF);
    u16 calc_logo_crc = report_crc16_ds(h + ROM_HEADER_LOGO_OFF, ROM_HEADER_LOGO_BYTES);
    u16 header_crc = le16(h + ROM_HEADER_HEADER_CRC_OFF);
    u16 calc_header_crc = report_crc16_ds(h, ROM_HEADER_CRC_BYTES);
    bool logo_ok = logo_crc == calc_logo_crc && logo_crc == ROM_HEADER_EXPECTED_LOGO_CRC;

    report_kv(f, "ROM Title:", "%s", title[0] ? title : "N/A");
    report_kv(f, "Game Code:", "%s", game_code[0] ? game_code : "N/A");
    report_kv(f, "Unit Code:", "0x%X (%s)", unit, unit_code_name(unit));
    report_kv(f, "Revision:", "%u", h[ROM_HEADER_REVISION_OFF]);
    report_kv(f, "Logo CRC:", "%s (0x%X)", logo_ok ? "OK" : "Invalid", logo_crc);
    report_kv(f, "Header Checksum:", "%s (0x%X)", report_header_crc_matches(header_crc, calc_header_crc) ? "OK" : "Invalid", header_crc);
    report_kv(f, "ARM9 ROM Offset:", "0x%lX", (unsigned long)le32(h + ROM_HEADER_ARM9_ROM_OFF_OFF));
    report_kv(f, "ARM9 Entry Address:", "0x%lX", (unsigned long)le32(h + ROM_HEADER_ARM9_ENTRY_OFF));
    report_kv(f, "ARM9 Load Address:", "0x%lX", (unsigned long)le32(h + ROM_HEADER_ARM9_LOAD_OFF));
    report_kv(f, "ARM7 ROM Offset:", "0x%lX", (unsigned long)le32(h + ROM_HEADER_ARM7_ROM_OFF_OFF));
    report_kv(f, "ARM7 Entry Address:", "0x%lX", (unsigned long)le32(h + ROM_HEADER_ARM7_ENTRY_OFF));
    report_kv(f, "ARM7 Load Address:", "0x%lX", (unsigned long)le32(h + ROM_HEADER_ARM7_LOAD_OFF));
    report_kv(f, "FNT Offset:", "0x%lX", (unsigned long)le32(h + ROM_HEADER_FNT_OFF_OFF));
    report_kv(f, "FNT Size:", "0x%lX", (unsigned long)le32(h + ROM_HEADER_FNT_SIZE_OFF));
    report_kv(f, "FAT Offset:", "0x%lX", (unsigned long)le32(h + ROM_HEADER_FAT_OFF_OFF));
    report_kv(f, "FAT Size:", "0x%lX", (unsigned long)le32(h + ROM_HEADER_FAT_SIZE_OFF));
    report_kv(f, "ARM9 Overlay Offset:", "0x%lX", (unsigned long)le32(h + ROM_HEADER_ARM9_OVL_OFF_OFF));
    report_kv(f, "ARM9 Overlay Size:", "0x%lX", (unsigned long)le32(h + ROM_HEADER_ARM9_OVL_SIZE_OFF));
    report_kv(f, "ARM7 Overlay Offset:", "0x%lX", (unsigned long)le32(h + ROM_HEADER_ARM7_OVL_OFF_OFF));
    report_kv(f, "ARM7 Overlay Size:", "0x%lX", (unsigned long)le32(h + ROM_HEADER_ARM7_OVL_SIZE_OFF));
    report_kv(f, "ARM9 Autoload Addr.:", "0x%lX", (unsigned long)le32(h + ROM_HEADER_ARM9_AUTOLOAD_OFF));
    report_kv(f, "ARM7 Autoload Addr.:", "0x%lX", (unsigned long)le32(h + ROM_HEADER_ARM7_AUTOLOAD_OFF));
    report_kv(f, "RSA Offset:", "0x%lX", (unsigned long)le32(h + ROM_HEADER_RSA_OFFSET_OFF));
}

/**
 * @brief Returns whether the rebuilt ROM header Nintendo logo hash is valid.
 */
bool report_rom_logo_ok(const u8 *h, u32 hsize) {
    if (!h || hsize < ROM_HEADER_MIN_BYTES) return false;
    u16 stored = le16(h + ROM_HEADER_LOGO_CRC_OFF);
    u16 calculated = report_crc16_ds(h + ROM_HEADER_LOGO_OFF, ROM_HEADER_LOGO_BYTES);
    return stored == calculated && stored == ROM_HEADER_EXPECTED_LOGO_CRC;
}

/**
 * @brief Returns whether the rebuilt ROM header CRC is valid.
 */
bool report_rom_header_crc_ok(const u8 *h, u32 hsize) {
    return h && hsize >= ROM_HEADER_MIN_BYTES &&
           report_header_crc_matches(le16(h + ROM_HEADER_HEADER_CRC_OFF),
                              report_crc16_ds(h, ROM_HEADER_CRC_BYTES));
}
