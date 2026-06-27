/**
 * @file meta.c
 * @brief Reassembles and decodes fixed DS Download Play beacon metadata.
 */
#include "state.h"
#include "boot.h"
#include "debug.h"
#include "download.h"
#include "file.h"
#include "ipc_arm9.h"
#include "meta.h"
#include "pcap.h"
#include "report.h"
#include "scan.h"
#include "system.h"
#include "ui.h"
#include "verify.h"
#include "text.h"

#include <string.h>

#define COLOR_OPAQUE           BIT(15)
/**
 * @brief Returns the number of metadata fragments expected for a slot layout.
 */
static unsigned slot_fragment_count(const ContentSlot *s) {
    unsigned count = BEACON_FIXED_MAX_FRAGMENTS;
    if (s && s->fragment_len[0] >= BEACON_FRAGMENT_HEADER_BYTES &&
        s->fragments[0][1] > 0 &&
        s->fragments[0][1] <= BEACON_FIXED_MAX_FRAGMENTS) {
        count = s->fragments[0][1];
    }
    return count;
}

/**
 * @brief Rebuilds fixed metadata bytes from a completed scan slot.
 */
bool meta_build_fixed_info(const ContentSlot *s, u8 *fixed, unsigned *fixed_len) {
    if (!s || !fixed || !fixed_len) return false;
    const FixedMetadataLayout *layout =
        beacon_fixed_metadata_layout((BeaconDataAttr)s->beacon_data_attr);
    if (!layout) return false;
    unsigned count = slot_fragment_count(s);
    if (count != layout->fragment_count) return false;
    u16 need_mask = (u16)((1u << count) - 1u);
    if ((s->fragment_mask & need_mask) != need_mask) return false;
    unsigned out = 0;
    for (unsigned i = 0; i < count; i++) {
        if (!(s->fragment_mask & (1u << i)) ||
            s->fragment_len[i] < BEACON_FRAGMENT_HEADER_BYTES) {
            return false;
        }
        const u8 *frag = s->fragments[i];
        if (frag[0] != i || frag[1] != count) return false;
        unsigned len = frag[2];
        unsigned stored = s->fragment_len[i] - BEACON_FRAGMENT_HEADER_BYTES;
        if (len > stored || len > BEACON_FIXED_FRAGMENT_BYTES ||
            out + len > BEACON_FIXED_INFO_MAX ||
            out + len > layout->required_len) {
            return false;
        }
        if (i + 1 < count && len != BEACON_FIXED_FRAGMENT_BYTES) return false;
        if (i + 1 == count && len == 0) return false;
        memcpy(fixed + out, frag + BEACON_FRAGMENT_HEADER_BYTES, len);
        out += len;
    }
    *fixed_len = out;
    return out == layout->required_len;
}

/**
 * @brief Appends one byte to a bounded UTF-8 output buffer.
 */
static void append_char(char *dst, size_t dst_size, size_t *out, char ch) {
    if (*out + 1 < dst_size) dst[(*out)++] = ch;
}

/**
 * @brief Decodes a bounded UCS-2 field into UTF-8 text.
 */
static void decode_ucs2_text(const u8 *src, unsigned units, char *dst, size_t dst_size, bool preserve_newlines) {
    size_t out = 0;
    bool prev_space = false;
    bool prev_newline = false;
    bool prev_cr = false;
    if (!dst_size) return;

    while (units-- && out + 1 < dst_size) {
        u16 c = (u16)src[0] | ((u16)src[1] << 8);
        src += 2;
        if (!c || c == 0xffff) break;

        if (c == '\r' || c == '\n') {
            if (preserve_newlines) {
                if (!(c == '\n' && prev_cr)) {
                    append_char(dst, dst_size, &out, '\n');
                }
                prev_cr = (c == '\r');
                prev_newline = true;
                prev_space = false;
            } else if (!prev_space && out) {
                append_char(dst, dst_size, &out, ' ');
                prev_space = true;
                prev_cr = false;
            }
            continue;
        }
        prev_cr = false;

        if (c == '\t') c = ' ';
        if (c == ' ') {
            if (prev_space || prev_newline || !out) continue;
            append_char(dst, dst_size, &out, ' ');
            prev_space = true;
            prev_newline = false;
            continue;
        }

        if (c < 0x20 || (c >= 0xd800 && c <= 0xdfff)) c = '?';
        (void)text_append_codepoint(dst, dst_size, &out, c);
        prev_space = false;
        prev_newline = false;
    }

    while (out && (dst[out - 1] == ' ' || dst[out - 1] == '\n')) out--;
    dst[out] = 0;
}

/**
 * @brief Decodes one fixed metadata text field into UTF-8.
 */
static void decode_fixed_string(const u8 *fixed, unsigned fixed_len, unsigned off, unsigned max_units,
                                char *dst, size_t dst_size, bool preserve_newlines) {
    if (dst_size) dst[0] = 0;
    if (off >= fixed_len) return;
    unsigned available = (fixed_len - off) / 2;
    if (available > max_units) available = max_units;
    decode_ucs2_text(fixed + off, available, dst, dst_size, preserve_newlines);
}

/**
 * @brief Decodes 4bpp icon tiles and palette from fixed metadata.
 */
static void decode_icon(const u8 *fixed, unsigned fixed_len, u16 *icon, bool *icon_valid) {
    if (icon_valid) *icon_valid = false;
    if (!icon || fixed_len < BEACON_FIXED_ICON_END) return;
    u16 pal[16];
    for (unsigned i = 0; i < 16; i++) {
        pal[i] = ((u16)fixed[BEACON_FIXED_ICON_PALETTE_OFF + i * 2] |
                  ((u16)fixed[BEACON_FIXED_ICON_PALETTE_OFF + i * 2 + 1] << 8)) | COLOR_OPAQUE;
    }
    const u8 *tiles = fixed + BEACON_FIXED_ICON_TILES_OFF;
    for (unsigned ty = 0; ty < 4; ty++) {
        for (unsigned tx = 0; tx < 4; tx++) {
            const u8 *tile = tiles + (ty * 4 + tx) * 32;
            for (unsigned y = 0; y < 8; y++) {
                u16 *dst = icon + (ty * 8 + y) * BEACON_ICON_W + tx * 8;
                const u8 *row = tile + y * 4;
                for (unsigned x = 0; x < 8; x += 2) {
                    u8 b = row[x >> 1];
                    dst[x] = pal[b & 0x0f];
                    dst[x + 1] = pal[b >> 4];
                }
            }
        }
    }
    if (icon_valid) *icon_valid = true;
}

/**
 * @brief Checks that fixed metadata contains enough bytes for its layout.
 */
static bool fixed_info_sane(const FixedMetadataLayout *layout, const u8 *fixed, unsigned fixed_len) {
    if (!layout || fixed_len < layout->required_len || layout->host_name_off == 0) return false;
    u8 host_len = fixed[layout->host_name_off - 1u];
    return host_len > 0 && host_len <= HOST_NAME_CHARS;
}

/**
 * @brief Decodes host, title, description, color and icon metadata.
 */
bool meta_decode_fixed_info(BeaconDataAttr attr, const u8 *fixed, unsigned fixed_len,
                           char *host, size_t host_size,
                           char *title, size_t title_size,
                           char *description, size_t description_size,
                           u16 *icon, bool *icon_valid) {
    if (!fixed) return false;
    if (host && host_size) host[0] = 0;
    if (title && title_size) title[0] = 0;
    if (description && description_size) description[0] = 0;
    if (icon_valid) *icon_valid = false;
    const FixedMetadataLayout *layout = beacon_fixed_metadata_layout(attr);
    if (!fixed_info_sane(layout, fixed, fixed_len)) return false;
    decode_fixed_string(fixed, fixed_len, layout->host_name_off, HOST_NAME_CHARS, host, host_size, false);
    decode_fixed_string(fixed, fixed_len, layout->title_off, TITLE_CHARS, title, title_size, false);
    decode_fixed_string(fixed, fixed_len, layout->description_off, DESCRIPTION_CHARS, description, description_size, true);
    if (layout->has_icon) decode_icon(fixed, fixed_len, icon, icon_valid);
    return (host && host[0]) || (title && title[0]) || (description && description[0]) || (icon_valid && *icon_valid);
}
