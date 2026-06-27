#pragma once
#include "state.h"

/** @brief Initializes both DS framebuffers and UI runtime state. */
void ui_init(void);

/** @brief Runs one UI tick, including redraws requested by state changes. */
void ui_frame(void);

/** @brief Forces an immediate redraw/present cycle. */
void ui_draw_now(void);

/** @brief Fades both screens to white before boot handover. */
void ui_fade_to_white(void);

/** @brief Displays the non-recoverable game-card-removed overlay. */
void ui_show_game_card_removed(void);

/** @brief Appends a formatted line to the on-screen log. */
void ui_log(const char *fmt, ...);

/** @brief Adds a successful saved slot to the UI history list. */
void ui_record_save_success(const ContentSlot *slot);

/** @brief Removes duplicate saved-history rows, keeping the newest per content ID. */
void ui_deduplicate_history(void);

/** @brief Increments the session failure counter shown in the status area. */
void ui_count_failure(void);

/** @brief Marks the UI dirty so the next frame redraws. */
void ui_mark_dirty(void);
