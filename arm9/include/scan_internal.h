#ifndef SCAN_INTERNAL_H
#define SCAN_INTERNAL_H

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
#include "text.h"
#include "ui.h"
#include "verify.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define BEACON_BODY_SIZE            12u
#define IE_SSID                     0u
#define IE_DS_PARAM_SET             3u
#define IE_TIM                      5u
#define DOWNLOADED_ID_CAPACITY      64u

/**
 * @brief Download-history and picker state shared by scan source files.
 */
extern u32 s_downloaded_ids[DOWNLOADED_ID_CAPACITY];
extern unsigned s_downloaded_id_count;
extern u32 s_repeat_download_block_until;
extern u32 s_download_start_block_until;
extern u32 s_pick_rng;

/** @brief Builds a stable provisional slot ID from the first ARM7 scan event. */
u32 scan_make_provisional_id(const Arm7BssEvent *ev);

/** @brief Builds the final content ID from fixed metadata and beacon context. */
u32 scan_make_content_id(const ContentSlot *s, const u8 *fixed, unsigned fixed_len);

/** @brief Moves a completed slot to its final content ID and downloaded history. */
void scan_set_final_content_id(ContentSlot *s, u32 id);

/** @brief Returns the current wall-clock seconds used by scan cooldowns. */
u32 scan_now_seconds(void);

/** @brief Reports whether repeat downloads are still blocked by cooldown. */
bool scan_repeat_download_block_active(void);

/** @brief Reports whether all automatic download starts are temporarily blocked. */
bool scan_download_start_block_active(void);

/** @brief Advances the deterministic picker used to rotate eligible slots. */
u32 scan_next_pick_random(void);

/** @brief Decodes a member-name metadata fragment into UTF-8. */
void scan_decode_member_name(const u8 *frag, char *dst, size_t dst_size);

/** @brief Returns whether all metadata required for display/download is present. */
bool scan_metadata_complete(const ContentSlot *s);

/** @brief Updates the BCN handover prefix from the selected parent event. */
void scan_update_handover_prefix(ContentSlot *s, const Arm7BssEvent *ev);

/** @brief Records a final content ID so immediate repeat downloads are skipped. */
void scan_remember_downloaded_id(u32 id);

#endif
