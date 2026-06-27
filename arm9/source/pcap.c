/**
 * @file pcap.c
 * @brief Captures selected raw Wi-Fi frames into a temporary PCAP output.
 */
#include "state.h"
#include "atomic_file.h"
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
#include "path.h"

#include <string.h>
#include <time.h>

#if DEBUG_VERSION

static FILE *s_pcap;
static u32 s_prev_ts;
static u32 s_write_count;
static u8 s_pcap_buf[32768];
static unsigned s_pcap_buf_used;
static bool s_pcap_error;
static char s_pcap_final_path[256];
static char s_pcap_temp_path[256];

static PcapWriteResult pcap_write_frame(u32 ts_us, const void *frame,
                                          unsigned len, bool filter_nintendo);

/* Nintendo Co., Ltd. 24-bit MAC prefixes used to keep PCAP captures focused. */
static const u32 s_nintendo_mac_prefixes[] = {
    0x0009bfu, 0x001656u, 0x0017abu, 0x00191du, 0x0019fdu, 0x001ae9u, 0x001b7au, 0x001beau,
    0x001cbeu, 0x001dbcu, 0x001e35u, 0x001ea9u, 0x001f32u, 0x001fc5u, 0x002147u, 0x0021bdu,
    0x00224cu, 0x0022aau, 0x0022d7u, 0x002331u, 0x0023ccu, 0x00241eu, 0x002444u, 0x0024f3u,
    0x0025a0u, 0x002659u, 0x002709u, 0x0403d6u, 0x182a7bu, 0x1c4586u, 0x200bcfu, 0x201c3au,
    0x28cf51u, 0x2c10c1u, 0x3089ecu, 0x342fbdu, 0x34af2cu, 0x38c6ceu, 0x3ca9abu, 0x4044f7u,
    0x40d28au, 0x40f407u, 0x483177u, 0x48a5e7u, 0x48f1ebu, 0x4c306au, 0x50236du, 0x582f40u,
    0x58b03eu, 0x58bda3u, 0x5c0ce6u, 0x5c521eu, 0x601ac7u, 0x606bffu, 0x64b5c6u, 0x702c09u,
    0x7048f7u, 0x70f088u, 0x748469u, 0x74f9cau, 0x7820a5u, 0x78818cu, 0x78a2a0u, 0x7cbb8au,
    0x80d2e5u, 0x84c065u, 0x8c56c5u, 0x8ccde8u, 0x904528u, 0x9458cbu, 0x948e6du, 0x98415cu,
    0x98b6e9u, 0x98e255u, 0x98e8fau, 0x9ce635u, 0xa438ccu, 0xa45c27u, 0xa4c0e1u, 0xa4c1e8u,
    0xacfae4u, 0xb86870u, 0xb87826u, 0xb88aecu, 0xb8ae6eu, 0xbc744bu, 0xbc89a6u, 0xbc9ebbu,
    0xbcce25u, 0xc0a4cfu, 0xc84805u, 0xc89143u, 0xcc5b31u, 0xcc9e00u, 0xccfb65u, 0xd05509u,
    0xd4f057u, 0xd86b83u, 0xd86bf7u, 0xdc68ebu, 0xdccd18u, 0xe00c7fu, 0xe0e751u, 0xe0efbfu,
    0xe0f6b5u, 0xe84eceu, 0xe8a0cdu, 0xe8da20u, 0xecc40du,
};
/**
 * @brief Returns whether a MAC address has the Nintendo OUI used by DS traffic.
 */
static bool pcap_is_nintendo_mac(const u8 *mac) {
    if (!mac) return false;
    u32 prefix = ((u32)mac[0] << 16) | ((u32)mac[1] << 8) | (u32)mac[2];
    unsigned lo = 0;
    unsigned hi = ARRAY_COUNT(s_nintendo_mac_prefixes);
    while (lo < hi) {
        unsigned mid = lo + ((hi - lo) >> 1);
        u32 v = s_nintendo_mac_prefixes[mid];
        if (prefix == v) return true;
        if (prefix < v) hi = mid;
        else lo = mid + 1;
    }
    return false;
}

/**
 * @brief Selects the source address used to classify a raw 802.11 frame.
 */
static const u8 *pcap_source_address(const void *frame, unsigned len) {
    if (!frame || len < 2) return NULL;
    const u8 *f = (const u8*)frame;
    u16 fc = le16(f);
    unsigned type = (fc >> 2) & 0x03u;
    unsigned subtype = (fc >> 4) & 0x0fu;
    bool to_ds = (fc & 0x0100u) != 0;
    bool from_ds = (fc & 0x0200u) != 0;

    switch (type) {
        case 0: /* Management: Address 2 is the source/transmitter. */
            return len >= DOT11_HDR_SIZE ? f + 10 : NULL;

        case 1: /* Control: only some subtypes carry a transmitter address. */
            switch (subtype) {
                case 8:  /* Block Ack Request */
                case 9:  /* Block Ack */
                case 10: /* PS-Poll */
                case 11: /* RTS */
                    return len >= 16 ? f + 10 : NULL;
                default:
                    return NULL;
            }

        case 2: /* Data */
            if (to_ds && from_ds) return len >= 30 ? f + 24 : NULL;
            if (!to_ds && from_ds) return len >= DOT11_HDR_SIZE ? f + 16 : NULL;
            return len >= DOT11_HDR_SIZE ? f + 10 : NULL;

        default:
            return NULL;
    }
}

/**
 * @brief Returns whether a frame source passes the Nintendo MAC filter.
 */
static bool pcap_frame_from_nintendo_source(const void *frame, unsigned len) {
    return pcap_is_nintendo_mac(pcap_source_address(frame, len));
}

/**
 * @brief Counts stored beacon frames for a completed scan slot.
 */
static unsigned pcap_beacon_frame_count(const ContentSlot *slot) {
    const FixedMetadataLayout *layout = slot
        ? beacon_fixed_metadata_layout((BeaconDataAttr)slot->beacon_data_attr)
        : NULL;
    return layout ? layout->fragment_count : SNIPPET_COUNT;
}

/**
 * @brief Flushes buffered PCAP bytes to the temporary file.
 */
static bool pcap_flush_buffer(void) {
    if (s_pcap_error) return false;
    if (s_pcap && s_pcap_buf_used) {
        if (fwrite(s_pcap_buf, 1, s_pcap_buf_used, s_pcap) != s_pcap_buf_used) {
            s_pcap_error = true;
        }
        s_pcap_buf_used = 0;
    }
    return !s_pcap_error;
}

/**
 * @brief Writes bytes into the PCAP buffer and records write failures.
 */
static void pcap_write_bytes(const void *data, unsigned len) {
    if (s_pcap_error) return;
    const u8 *p = (const u8*)data;
    while (len) {
        unsigned room = sizeof(s_pcap_buf) - s_pcap_buf_used;
        if (!room) {
            if (!pcap_flush_buffer()) return;
            room = sizeof(s_pcap_buf);
        }
        if (len >= sizeof(s_pcap_buf) && s_pcap_buf_used == 0) {
            if (fwrite(p, 1, len, s_pcap) != len) s_pcap_error = true;
            return;
        }
        unsigned n = len < room ? len : room;
        memcpy(s_pcap_buf + s_pcap_buf_used, p, n);
        s_pcap_buf_used += n;
        p += n;
        len -= n;
    }
}

/**
 * @brief Writes a little-endian 32-bit value to the PCAP stream.
 */
static void wr32(u32 v) {
    u8 b[4];
    stle32(b, v);
    pcap_write_bytes(b, sizeof(b));
}

/**
 * @brief Writes a little-endian 16-bit value to the PCAP stream.
 */
static void wr16(u16 v) {
    u8 b[2];
    stle16(b, v);
    pcap_write_bytes(b, sizeof(b));
}

/**
 * @brief Opens a temporary PCAP file and writes its global header.
 */
bool pcap_open(const char *base_name) {
    pcap_close();
    pcap_discard();
    if (!base_name || !base_name[0]) return false;
    if (!path_make_output_file(s_pcap_final_path, sizeof(s_pcap_final_path), base_name, ".pcap")) return false;
    if (atomic_file_exists(s_pcap_final_path)) return false;
    if (!atomic_file_make_temp_path(s_pcap_temp_path, sizeof(s_pcap_temp_path), s_pcap_final_path, ".pcap.tmp")) return false;
    s_pcap = fopen(s_pcap_temp_path, "wb");
    if (!s_pcap) return false;
    s_pcap_buf_used = 0;
    s_pcap_error = false;
    wr32(0xa1b2c3d4u);
    wr16(2);
    wr16(4);
    wr32(0);
    wr32(0);
    wr32(MAX_IEEE_FRAME);
    wr32(105); /* LINKTYPE_IEEE802_11 */
    s_prev_ts = 0;
    s_write_count = 0;
    if (!pcap_flush_buffer() || fflush(s_pcap) != 0) {
        s_pcap_error = true;
        pcap_close();
        return false;
    }
    return !s_pcap_error;
}

/**
 * @brief Writes every stored beacon frame for the selected scan slot.
 */
bool pcap_write_beacon_set(const ContentSlot *slot) {
    if (!s_pcap || !scan_beacon_frames_complete(slot)) return false;
    unsigned count = pcap_beacon_frame_count(slot);
    unsigned written = 0;
    for (unsigned i = 0; i < count; i++) {
        PcapWriteResult result =
            pcap_write_frame(slot->beacon_frame_ts_us[i], slot->beacon_frames[i],
                             slot->beacon_frame_len[i], false);
        if (result != PCAP_WRITE_WRITTEN) {
            s_pcap_error = true;
            return false;
        }
        written++;
    }
    if (!pcap_flush_buffer() || fflush(s_pcap) != 0) s_pcap_error = true;
    return !s_pcap_error && written == count;
}

/**
 * @brief Writes one timestamped raw 802.11 frame to PCAP.
 */
static PcapWriteResult pcap_write_frame(u32 ts_us, const void *frame,
                                          unsigned len, bool filter_nintendo) {
    if (!s_pcap || s_pcap_error || !frame || !len) return PCAP_WRITE_ERROR;
    if (filter_nintendo && !pcap_frame_from_nintendo_source(frame, len)) {
        return PCAP_WRITE_SKIPPED;
    }
    if (len > MAX_IEEE_FRAME) len = MAX_IEEE_FRAME;
    if (ts_us < s_prev_ts) ts_us = s_prev_ts + 1;
    s_prev_ts = ts_us;
    u32 sec = ts_us / 1000000u;
    u32 usec = ts_us % 1000000u;
    wr32(sec);
    wr32(usec);
    wr32(len);
    wr32(len);
    pcap_write_bytes(frame, len);
    if ((++s_write_count & 0xff) == 0) pcap_flush_buffer();
    return s_pcap_error ? PCAP_WRITE_ERROR : PCAP_WRITE_WRITTEN;
}

/**
 * @brief Writes one raw frame only when its source passes capture filtering.
 */
PcapWriteResult pcap_write(u32 ts_us, const void *frame, unsigned len) {
    return pcap_write_frame(ts_us, frame, len, true);
}

/**
 * @brief Writes one already selected raw frame without source filtering.
 */
PcapWriteResult pcap_write_raw(u32 ts_us, const void *frame, unsigned len) {
    return pcap_write_frame(ts_us, frame, len, false);
}

/**
 * @brief Flushes and closes the temporary PCAP stream.
 */
bool pcap_close(void) {
    bool ok = !s_pcap_error;
    if (s_pcap) {
        ok = pcap_flush_buffer() && ok;
        if (fflush(s_pcap) != 0) ok = false;
        if (fclose(s_pcap) != 0) ok = false;
        s_pcap = NULL;
    }
    if (!ok) s_pcap_error = true;
    return ok;
}

/**
 * @brief Renames the temporary PCAP to its final reserved path.
 */
bool pcap_commit(void) {
    if (s_pcap && !pcap_close()) return false;
    if (s_pcap_error || !s_pcap_temp_path[0] || !s_pcap_final_path[0]) return false;
    if (atomic_file_exists(s_pcap_final_path)) {
        pcap_discard();
        s_pcap_error = true;
        return false;
    }
    if (!atomic_file_commit_temp(s_pcap_temp_path, s_pcap_final_path)) {
        pcap_discard();
        s_pcap_error = true;
        return false;
    }
    s_pcap_temp_path[0] = 0;
    s_pcap_final_path[0] = 0;
    return true;
}

/**
 * @brief Removes temporary PCAP state after a failed attempt.
 */
void pcap_discard(void) {
    if (s_pcap) (void)pcap_close();
    atomic_file_discard_temp(s_pcap_temp_path);
    s_pcap_final_path[0] = 0;
}

/**
 * @brief Returns whether any PCAP write, close or commit step failed.
 */
bool pcap_had_error(void) {
    return s_pcap_error;
}

#else

bool pcap_open(const char *base_name) {
    (void)base_name;
    return true;
}

bool pcap_write_beacon_set(const ContentSlot *slot) {
    return scan_beacon_frames_complete(slot);
}

PcapWriteResult pcap_write(u32 ts_us, const void *frame, unsigned len) {
    (void)ts_us;
    (void)frame;
    (void)len;
    return PCAP_WRITE_SKIPPED;
}

PcapWriteResult pcap_write_raw(u32 ts_us, const void *frame, unsigned len) {
    (void)ts_us;
    (void)frame;
    (void)len;
    return PCAP_WRITE_SKIPPED;
}

bool pcap_close(void) {
    return true;
}

bool pcap_commit(void) {
    return true;
}

void pcap_discard(void) {
}

bool pcap_had_error(void) {
    return false;
}

#endif
