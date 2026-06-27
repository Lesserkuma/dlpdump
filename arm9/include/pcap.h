#pragma once
#include "state.h"

typedef enum {
    PCAP_WRITE_ERROR = -1,
    PCAP_WRITE_SKIPPED = 0,
    PCAP_WRITE_WRITTEN = 1,
} PcapWriteResult;

/**
 * @brief Opens a per-download PCAP file and writes the global header.
 *
 * @param base_name Safe output base name without extension.
 * @return true if the file is open and ready for frame writes.
 */
bool pcap_open(const char *base_name);

/** @brief Writes all captured beacon frames for a complete scan slot. */
bool pcap_write_beacon_set(const ContentSlot *slot);

/** @brief Appends a Nintendo-source 802.11 frame to the current PCAP. */
PcapWriteResult pcap_write(u32 ts_us, const void *frame, unsigned len);

/** @brief Appends a selected 802.11 frame without source-OUI filtering. */
PcapWriteResult pcap_write_raw(u32 ts_us, const void *frame, unsigned len);

/**
 * @brief Flushes and closes the PCAP file.
 *
 * @return false if any earlier write, flush or close failed.
 */
bool pcap_close(void);

/** @brief Commits the closed PCAP temp file to its final path. */
bool pcap_commit(void);

/** @brief Removes an uncommitted PCAP temp file after an aborted download. */
void pcap_discard(void);

/** @brief Returns whether the current/last PCAP stream hit an I/O error. */
bool pcap_had_error(void);
