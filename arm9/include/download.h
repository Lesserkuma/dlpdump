#pragma once
#include "state.h"

/**
 * @brief Starts association and transfer for a fully discovered scan slot.
 *
 * @param slot Scan slot owned by `g_slots`; it must remain valid until the
 *        transfer returns to scan mode.
 */
void download_start(ContentSlot *slot);

/**
 * @brief Processes one parent DS Download Play command extracted from a host frame.
 *
 * @param payload Bounded 802.11 payload bytes.
 * @param len Number of payload bytes.
 * @param event_id Monotonic ARM7 event identifier for diagnostics.
 * @param timestamp_us Capture timestamp in microseconds.
 * @param rx_status Raw receive flags from ARM7.
 */
void download_handle_host_payload(const u8 *payload, unsigned len, u32 event_id, u32 timestamp_us, u16 rx_status);

/** @brief Records a failed ARM7 association attempt and restores scan mode. */
void download_handle_connect_failed(unsigned reason);

/** @brief Enters download mode after ARM7 reports the association AID. */
void download_handle_connected(unsigned aid);

/** @brief Converts an ARM7 disconnect event into retry, abort or completion state. */
void download_handle_disconnected(void);

/** @brief Aborts the active transfer and marks the slot as tried. */
void download_abort_and_scan(const char *why);

/** @brief Aborts the active transfer and allows the slot to be retried. */
void download_abort_and_retry(const char *why);

/** @brief Requests a user-visible, server-notified cancellation of the attempt. */
void download_request_user_abort(void);

/** @brief Releases all buffers owned by the active download state. */
void download_free(void);

/** @brief Refreshes the communication timeout timestamp for the active transfer. */
void download_touch_communication(void);

/**
 * @brief Checks whether a raw 802.11 frame belongs to the active parent.
 *
 * @return true if any source/destination/BSSID address matches the active slot.
 */
bool download_frame_from_active_parent(const void *frame, unsigned len);

/** @brief Feeds a captured parent/child frame to protocol statistics tracking. */
void download_observe_raw_frame(const void *frame, unsigned len, u16 raw_flags);

/** @brief Adds newly observed ARM7 event drops to the active transfer stats. */
void download_note_ipc_dropped_events(u32 dropped_events);

/** @brief Applies transfer-start, final-wait and communication timeouts. */
void download_check_timeout(void);

/** @brief Updates the displayed signal percentage, clamped to 0..100. */
void download_update_signal(u8 percent);

/** @brief Updates signal percentage plus raw RSSI range statistics. */
void download_update_signal_raw(u8 percent, u8 raw_rssi);

/** @brief Updates save/report progress while the UI is in a save state. */
void download_update_save_progress(unsigned percent);

/** @brief Returns true while RSA is acknowledged but DATA has not started. */
bool download_waiting_for_transfer_start(void);

/** @brief Returns remaining seconds before transfer-start timeout. */
unsigned download_transfer_wait_seconds_remaining(void);

/** @brief Returns true after all DATA packets arrived but FINAL is still absent. */
bool download_waiting_for_final(void);

/** @brief Returns remaining seconds before the final-wait fallback saves data. */
unsigned download_final_wait_seconds_remaining(void);
