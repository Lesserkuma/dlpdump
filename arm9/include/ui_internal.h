#ifndef UI_INTERNAL_H
#define UI_INTERNAL_H

#include "state.h"
#include "boot.h"
#include "debug.h"
#include "download.h"
#include "file.h"
#include "font.h"
#include "ipc_arm9.h"
#include "meta.h"
#include "pcap.h"
#include "report.h"
#include "scan.h"
#include "system.h"
#include "text.h"
#include "ui.h"
#include "verify.h"
#include "assets.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define UI_W 256
#define UI_H 192
#define UI_BG_STRIDE 256
#define UI_HISTORY_MAX 24
#define UI_LOG_LINES 10
#define UI_LOG_LINE_BYTES (TEXT_UTF8_BYTES(TITLE_CHARS) + 64)
#define UI_LOG_CONTINUATION_PREFIX "  "
#define UI_LOG_WRAP_LINES 2
#define UI_LOG_VISUAL_LINE_BYTES (UI_LOG_LINE_BYTES + 4)
#define UI_USER_NAMES_BYTES (MEMBER_SLOT_COUNT * (TEXT_UTF8_BYTES(USER_NAME_CHARS) + 2))
#define UI_BOTTOM_LINE_BYTES (UI_USER_NAMES_BYTES + 32)
#define UI_DESC_RESERVED_LINES 2
#define UI_FRAME_PIXELS (UI_BG_STRIDE * UI_H)
#define UI_ACTIVE_REDRAW_FRAMES 5

#define UI_PROGRAM_NAME "DS Download Play Dumper"
#define UI_TOP_VISIBLE_BANNERS 4
#define UI_TOP_BAR_H SIGNAL_ICON_H
#define UI_TOP_CONTENT_Y UI_TOP_BAR_H
#define UI_TOP_CONTENT_H (UI_H - UI_TOP_CONTENT_Y)
#define UI_TOP_BANNER_SLOT_H ((UI_TOP_CONTENT_H / UI_TOP_VISIBLE_BANNERS) - 1)
#define UI_TOP_ICON_X 5
#define UI_TOP_ICON_Y_OFF 2
#define UI_TOP_TEXT_X 42
#define UI_TOP_DESC_Y_OFF 9
#define UI_TOP_DESC_LINE_STEP 9
#define UI_TOP_META_Y_OFF (UI_TOP_DESC_Y_OFF + ((UI_DESC_RESERVED_LINES - 1) * UI_TOP_DESC_LINE_STEP) + 8 + 1)
#define UI_TOP_BANNER_TEXT_H (UI_TOP_META_Y_OFF + 8)
#define UI_TOP_BANNER_ICON_H (UI_TOP_ICON_Y_OFF + BEACON_ICON_H)
#define UI_TOP_BANNER_ITEM_H (((UI_TOP_BANNER_TEXT_H > UI_TOP_BANNER_ICON_H) ? UI_TOP_BANNER_TEXT_H : UI_TOP_BANNER_ICON_H) + 4)
#define UI_TOP_BANNER_GROUP_H (((UI_TOP_VISIBLE_BANNERS - 1) * UI_TOP_BANNER_SLOT_H) + UI_TOP_BANNER_ITEM_H)
#define UI_TOP_BANNER_CENTER_Y_OFF ((UI_TOP_CONTENT_H > UI_TOP_BANNER_GROUP_H) ? ((UI_TOP_CONTENT_H - UI_TOP_BANNER_GROUP_H) / 2) : 0)
#define UI_TOP_BANNER_Y_SHIFT 2
#define UI_TOP_PROGRESS_X 135
#define UI_TOP_HISTORY_TIME_X 162
#define UI_TOP_SIGNAL_X (UI_W - SIGNAL_ICON_W)
#define UI_TOP_SIGNAL_Y 0
#define UI_TOP_TITLE_RIGHT_PAD 2
#define UI_TOP_HOST_RIGHT_PAD 4
#define UI_TOP_PROGRESS_W 90
#define UI_TOP_PROGRESS_H 7
#define UI_TOP_PROGRESS_Y_OFF 1
#define UI_TOP_PROGRESS_TEXT_GAP 2
#define UI_TOP_PROGRESS_RIGHT_PAD 4
#define UI_TOP_PROGRESS_TEXT_W (UI_W - UI_TOP_PROGRESS_X - UI_TOP_PROGRESS_W - UI_TOP_PROGRESS_TEXT_GAP - UI_TOP_PROGRESS_RIGHT_PAD)
#define UI_CARD_REMOVED_BOX_X 12
#define UI_CARD_REMOVED_BOX_Y 74
#define UI_CARD_REMOVED_BOX_W 232
#define UI_CARD_REMOVED_BOX_H 48
#define UI_CARD_REMOVED_TEXT1_X 59
#define UI_CARD_REMOVED_TEXT1_Y 88
#define UI_CARD_REMOVED_TEXT2_X 70
#define UI_CARD_REMOVED_TEXT2_Y 98

#define UI_BLACK 0x8000
#define UI_WHITE 0xffff
#define UI_DIM   0xc631
#define UI_MID   0xd6b5
#define UI_DARK  0x9ce7
#define UI_RED   UI_RGB15(31, 4, 4)
#define UI_DARK_BLUE 0xb040
#define UI_DARK_TEXT 0x94a5
#define UI_RGB15(r, g, b) ((u16)(0x8000 | (((r) & 31) << 0) | (((g) & 31) << 5) | (((b) & 31) << 10)))
#define UI_FADE_FRAMES 30
#define UI_MASTER_BRIGHT_WHITE 0x4000
#define UI_MASTER_BRIGHT_MAX 16

/**
 * @brief One saved-download entry shown in the top-screen history list.
 */
typedef struct {
    bool used;
    u32 id;
    time_t completed_time;
    u32 order;
    char time_text[24];
    char host_name[TEXT_UTF8_BYTES(HOST_NAME_CHARS)];
    char title[TEXT_UTF8_BYTES(TITLE_CHARS)];
    char description[TEXT_UTF8_BYTES(DESCRIPTION_CHARS)];
    bool icon_valid;
    u16 icon[BEACON_ICON_PIXELS];
} HistoryItem;

/**
 * @brief Holds the visual lines produced from one wrapped log string.
 */
typedef struct {
    unsigned count;
    char text[UI_LOG_WRAP_LINES][UI_LOG_VISUAL_LINE_BYTES];
} UiWrappedLogLine;

/**
 * @brief UI framebuffers, log state and save-history storage shared by UI files.
 */
extern u16 *s_topFb;
extern u16 *s_bottomFb;
extern u16 s_topBack[UI_FRAME_PIXELS];
extern u16 s_bottomBack[UI_FRAME_PIXELS];
extern bool s_dirty;
extern bool s_gameCardRemoved;
extern unsigned s_redrawTicker;
extern char s_logLines[UI_LOG_LINES][UI_LOG_LINE_BYTES];
extern unsigned s_logHead;
extern unsigned s_failedCount;
extern unsigned s_savedCount;
extern time_t s_startTime;
extern HistoryItem s_history[UI_HISTORY_MAX];
extern unsigned s_historyCount;
extern u32 s_historyOrder;

/** @brief Removes trailing partial UTF-8 sequences from an in-place string. */
void ui_trim_incomplete_utf8_tail(char *s);

/** @brief Normalizes one log line for bounded on-screen rendering. */
void ui_normalize_log_line(char *s);

/** @brief Formats the bottom-screen status line for the current run state. */
void ui_format_status_line(char *out, size_t out_size);

/** @brief Counts visible DS Download Play parents currently tracked by scan. */
unsigned ui_count_seen(void);

/** @brief Formats a short human-readable duration for compact UI labels. */
void ui_format_duration_short(u32 seconds, char *out, size_t out_size);

/** @brief Formats total runtime since application startup. */
void ui_format_run_time(char *out, size_t out_size);

/** @brief Returns elapsed seconds for the active download transfer. */
u32 ui_download_elapsed_seconds(void);

/** @brief Estimates the current download rate in kilobits per second. */
u32 ui_download_rate_kbps(void);

/** @brief Estimates remaining transfer seconds when enough data is available. */
bool ui_download_seconds_left(u32 *out);

/** @brief Formats the player/member-name line for one content slot. */
void ui_format_user_line(const ContentSlot *slot, char *out, size_t out_size);

/** @brief Wraps one log entry into the fixed two-line bottom-screen area. */
unsigned ui_wrap_log_line(const char *text, UiWrappedLogLine *out);

/** @brief Formats a completion timestamp for the saved-download history. */
void ui_format_time_text(time_t t, char *out, size_t out_size);

/** @brief Copies UI text with bounded UTF-8 tail trimming. */
void ui_copy_text(char *dst, size_t dst_size, const char *src);

/** @brief Fills a clipped rectangle in one framebuffer. */
void ui_fill_rect(u16 *fb, int x, int y, int w, int h, u16 color);

/** @brief Draws a clipped one-pixel rectangle outline. */
void ui_draw_rect(u16 *fb, int x, int y, int w, int h, u16 color);

/** @brief Draws text until the next glyph would exceed the pixel width. */
int ui_draw_text_limited(u16 *fb, int x, int y, const char *text, int max_w, u16 color);

/** @brief Renders the top screen with current scan, download and history state. */
void ui_draw_top(void);

/** @brief Renders the bottom screen status and log view. */
void ui_draw_bottom(void);

/** @brief Copies back buffers to the visible DS framebuffers. */
void ui_present_framebuffers(void);

/** @brief Maps a fade animation frame to a master-brightness factor. */
unsigned ui_fade_factor(unsigned step, unsigned steps);

/** @brief Applies white master brightness to both display engines. */
void ui_set_white_brightness(unsigned factor);

/** @brief Adds one successfully saved slot to the top-screen history list. */
void ui_record_saved(const ContentSlot *slot);

#endif
