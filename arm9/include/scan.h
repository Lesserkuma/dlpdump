#pragma once
#include "state.h"

/** @brief Clears all scan slots, downloaded-id memory and retry selection state. */
void scan_reset(void);

/** @brief Applies one validated ARM7 BSS event to the ARM9 scan table. */
void scan_handle_bss(const Arm7BssEvent *ev, u32 timestamp_us);

/** @brief Selects the next complete, recent and retry-eligible slot to download. */
ContentSlot *scan_pick_next(void);

/** @brief Blocks all automatic download starts for the repeat-download cooldown window. */
void scan_pause_downloads_for_cooldown(void);

/** @brief Finds a scan slot by its provisional or final content id. */
ContentSlot *scan_find_by_id(u32 id);

/** @brief Marks a slot as successfully saved and records it for repeat filtering. */
void scan_mark_downloaded(ContentSlot *s);

/** @brief Checks whether all expected beacon frames are available for PCAP setup. */
bool scan_beacon_frames_complete(const ContentSlot *s);
