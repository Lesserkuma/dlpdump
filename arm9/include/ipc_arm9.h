#pragma once
#include "state.h"

/** @brief Initializes shared ARM7/ARM9 IPC memory and starts ARM7 Wi-Fi setup. */
void ipc_init(void);

/** @brief Publishes one command to ARM7 and kicks the user PXI channel. */
void ipc_send_command(u32 id, const void *arg, size_t arg_len);

/** @brief Returns the ARM7 event-drop counter and stores it as the baseline. */
u32 ipc_dropped_events_snapshot(void);

/** @brief Discards pending ARM7 events after a mode transition. */
void ipc_reset_event_queue(void);

/** @brief Copies and dispatches stable ARM7 events from the shared ring buffer. */
void ipc_poll(void);

/** @brief Stores the currently scanned/connected WLAN channel for UI display. */
void set_current_wlan_channel(u8 channel);
