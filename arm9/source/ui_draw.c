/**
 * @file ui_draw.c
 * @brief Draws the Nintendo DS top and bottom screen UI framebuffers.
 */
#include "ui_internal.h"

/**
 * @brief Fills one framebuffer with a solid RGB555 color.
 */
static void clear_fb(u16 *fb) {
    if (!fb) return;
    for (unsigned y = 0; y < UI_H; y++) {
        u16 *row = fb + y * UI_BG_STRIDE;
        for (unsigned x = 0; x < UI_W; x++) row[x] = UI_BLACK;
    }
}

/**
 * @brief Fills a clipped rectangle in a framebuffer.
 */
void ui_fill_rect(u16 *fb, int x, int y, int w, int h, u16 color) {
    if (!fb || w <= 0 || h <= 0) return;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > UI_W) w = UI_W - x;
    if (y + h > UI_H) h = UI_H - y;
    if (w <= 0 || h <= 0) return;
    for (int yy = 0; yy < h; yy++) {
        u16 *dst = fb + (y + yy) * UI_BG_STRIDE + x;
        for (int xx = 0; xx < w; xx++) dst[xx] = color;
    }
}

/**
 * @brief Draws a one-pixel rectangle outline.
 */
void ui_draw_rect(u16 *fb, int x, int y, int w, int h, u16 color) {
    if (w <= 1 || h <= 1) return;
    ui_fill_rect(fb, x, y, w, 1, color);
    ui_fill_rect(fb, x, y + h - 1, w, 1, color);
    ui_fill_rect(fb, x, y, 1, h, color);
    ui_fill_rect(fb, x + w - 1, y, 1, h, color);
}

/**
 * @brief Draws text clipped to a maximum pixel width.
 */
int ui_draw_text_limited(u16 *fb, int x, int y, const char *text, int max_w, u16 color) {
    if (!text || !*text || max_w <= 0) return 0;
    const char *s = text;
    int x0 = x;
    uint32_t cp;
    while (text_utf8_next(&s, &cp)) {
        if (cp == '\n') break;
        if (cp == '\r') continue;
        if (cp == '\t') cp = ' ';
        int glyph = font_find_glyph(&font_8x8, cp);
        if (glyph < 0) continue;
        int adv = font_glyph_advance(&font_8x8, (u16)glyph, 0);
        if (x + adv > x0 + max_w) break;
        x += font_draw_glyph_rgb555(fb, UI_BG_STRIDE, UI_H, x, y, &font_8x8, (u16)glyph, color, 0);
    }
    return x - x0;
}

/**
 * @brief Returns the number of visible log lines for the bottom screen.
 */
static unsigned log_line_capacity(int y) {
    const int limit = UI_H - 18;
    if (y >= limit) return 0;
    return (unsigned)(((limit - 1 - y) / 9) + 1);
}

/**
 * @brief Draws the bottom-screen log with per-line wrapping.
 */
static int draw_wrapped_log_lines(u16 *fb, int y) {
    static UiWrappedLogLine wrapped[UI_LOG_LINES];
    unsigned wrapped_count = 0;
    unsigned used_lines = 0;
    unsigned capacity = log_line_capacity(y);

    for (unsigned i = 0; i < UI_LOG_LINES && used_lines < capacity; i++) {
        unsigned idx = (s_logHead + UI_LOG_LINES - 1u - i) % UI_LOG_LINES;
        if (!s_logLines[idx][0]) continue;

        UiWrappedLogLine tmp;
        unsigned lines = ui_wrap_log_line(s_logLines[idx], &tmp);
        if (!lines) continue;
        if (used_lines + lines > capacity) {
            unsigned remaining = capacity - used_lines;
            if (!remaining) break;
            if (lines > remaining) {
                unsigned drop = lines - remaining;
                for (unsigned src = drop, dst = 0; src < lines; src++, dst++) {
                    memmove(tmp.text[dst], tmp.text[src], sizeof(tmp.text[0]));
                }
                lines = remaining;
            }
            tmp.count = remaining;
        }
        wrapped[wrapped_count++] = tmp;
        used_lines += lines;
        if (used_lines >= capacity) break;
    }

    for (unsigned i = wrapped_count; i > 0; i--) {
        const UiWrappedLogLine *entry = &wrapped[i - 1u];
        for (unsigned line = 0; line < entry->count && y < UI_H - 18; line++) {
            ui_draw_text_limited(fb, 4, y, entry->text[line], UI_W - 8, UI_DIM);
            y += 9;
        }
    }

    return y;
}

/**
 * @brief Draws one label/value description row.
 */
static const char *draw_desc_line(u16 *fb, int x, int y, const char *text, int max_w, u16 color) {
    if (!text) return text;
    const char *start = text;
    const char *end = start;
    while (*end && *end != '\n' && *end != '\r') end++;
    char tmp[TEXT_UTF8_BYTES(DESCRIPTION_CHARS)];
    size_t n = (size_t)(end - start);
    if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
    memcpy(tmp, start, n);
    tmp[n] = 0;
    ui_draw_text_limited(fb, x, y, tmp[0] ? tmp : "-", max_w, color);
    while (*end == '\r' || *end == '\n') end++;
    return end;
}

/**
 * @brief Draws a placeholder row for a missing saved-history entry.
 */
static void draw_desc_reserved(u16 *fb, int x, int y, const char *text, int max_w, u16 color) {
    const char *p = (text && text[0]) ? text : NULL;
    for (unsigned i = 0; i < UI_DESC_RESERVED_LINES; i++) {
        if (p && *p) {
            p = draw_desc_line(fb, x, y + (int)i * UI_TOP_DESC_LINE_STEP, p, max_w, color);
        } else if (i == 0) {
            ui_draw_text_limited(fb, x, y, "-", max_w, color);
        }
    }
}

/**
 * @brief Draws a 32x32 decoded title icon with a one-pixel frame.
 */
static void draw_icon32(u16 *fb, int x, int y, const u16 *icon, bool valid) {
    if (!fb) return;
    if (valid && icon) {
        for (unsigned yy = 0; yy < BEACON_ICON_H; yy++) {
            if (y + (int)yy < 0 || y + (int)yy >= UI_H) continue;
            u16 *dst = fb + (y + yy) * UI_BG_STRIDE + x;
            const u16 *src = icon + yy * BEACON_ICON_W;
            for (unsigned xx = 0; xx < BEACON_ICON_W; xx++) {
                if (x + (int)xx >= 0 && x + (int)xx < UI_W) {
                    dst[xx] = src[xx];
                }
            }
        }
    }
    ui_draw_rect(fb, x - 1, y - 1, BEACON_ICON_W + 2, BEACON_ICON_H + 2, UI_MID);
}

/**
 * @brief Draws the wireless signal-strength bitmap for a slot.
 */
static void draw_signal_icon(u16 *fb, int x, int y, int level) {
    if (level < 0) level = 0;
    if (level > 3) level = 3;
    const u16 *src = g_signalIcons[level];
    for (unsigned yy = 0; yy < SIGNAL_ICON_H; yy++) {
        if (y + (int)yy < 0 || y + (int)yy >= UI_H) continue;
        u16 *dst = fb + (y + yy) * UI_BG_STRIDE + x;
        for (unsigned xx = 0; xx < SIGNAL_ICON_W; xx++) {
            if (x + (int)xx < 0 || x + (int)xx >= UI_W) continue;
            u16 c = src[yy * SIGNAL_ICON_W + xx];
            dst[xx] = c ? c : UI_BLACK;
        }
    }
}

/**
 * @brief Returns the firmware favorite-color index used for UI accenting.
 */
static u8 firmware_favorite_color(void) {
    const PERSONAL_DATA *pd = PersonalData;
    return pd ? (u8)(pd->theme & 0x0f) : 0;
}

/**
 * @brief Returns the top-bar background color for a firmware color.
 */
static u16 top_bar_color(void) {
    static const u16 firmware_favorite_color_rgb15[16] = {
        UI_RGB15(13, 17, 20), UI_RGB15(25,  7,  0), UI_RGB15(31,  1,  3), UI_RGB15(30, 13, 30),
        UI_RGB15(31, 16,  0), UI_RGB15(31, 29,  0), UI_RGB15(19, 31,  0), UI_RGB15( 0, 29,  2),
        UI_RGB15( 0, 21,  6), UI_RGB15( 9, 26, 16), UI_RGB15( 7, 22, 28), UI_RGB15( 2, 11, 28),
        UI_RGB15( 2,  1, 18), UI_RGB15(18,  0, 27), UI_RGB15(27,  0, 28), UI_RGB15(31,  0, 16),
    };
    return firmware_favorite_color_rgb15[firmware_favorite_color()];
}

/**
 * @brief Returns the readable text color for a top-bar background.
 */
static u16 top_bar_text_color(void) {
    if ((firmware_favorite_color() == 5) || (firmware_favorite_color() == 6)){
        return UI_DARK_TEXT;
    }
    return UI_WHITE;
}

/**
 * @brief Scales an RGB555 color channel set by a fixed brightness factor.
 */
static u16 adjust_rgb15(u16 color, int delta) {
    int r = (int)(color & 31u) + delta;
    int g = (int)((color >> 5) & 31u) + delta;
    int b = (int)((color >> 10) & 31u) + delta;
    if (r < 0) r = 0; else if (r > 31) r = 31;
    if (g < 0) g = 0; else if (g > 31) g = 31;
    if (b < 0) b = 0; else if (b > 31) b = 31;
    return UI_RGB15((unsigned)r, (unsigned)g, (unsigned)b);
}

/**
 * @brief Draws the top-screen bar background and fade treatment.
 */
static void draw_top_bar_bg(u16 color) {
    u16 hi = adjust_rgb15(color, 6);
    u16 light = adjust_rgb15(color, 3);
    u16 dark = adjust_rgb15(color, -3);
    u16 low = adjust_rgb15(color, -7);

    for (int y = 0; y < UI_TOP_BAR_H; y++) {
        u16 *dst = s_topBack + y * UI_BG_STRIDE;
        for (int x = 0; x < UI_W; x++) {
            if (y == 0) {
                dst[x] = hi;
            } else if (y == UI_TOP_BAR_H - 1) {
                dst[x] = low;
            } else {
                u16 a, b;
                if (y < UI_TOP_BAR_H / 3) {
                    a = hi;
                    b = light;
                } else if (y < (UI_TOP_BAR_H * 2) / 3) {
                    a = light;
                    b = color;
                } else {
                    a = color;
                    b = dark;
                }
                dst[x] = ((x ^ y) & 1) ? a : b;
            }
        }
    }
}

/**
 * @brief Returns whether the UI is currently showing RSA verification.
 */
static bool rsa_check_running(void) {
    return g_download.active &&
           g_runState == RUN_CHECKING_RSA_HASH &&
           g_verifyStatus == VERIFY_PENDING;
}

/**
 * @brief Computes received and total byte counts for progress display.
 */
static void download_progress_values(unsigned *received, unsigned *total) {
    if (!received || !total) return;
    if (rsa_check_running()) {
        *received = 1;
        *total = 1;
    } else {
        *received = g_download.received_packets;
        *total = g_download.total_packets;
    }
}

/**
 * @brief Converts received and total byte counts into a percentage.
 */
static unsigned progress_percent(unsigned received, unsigned total) {
    if (!total) return 0;
    return received >= total ? 100u : (received * 100u) / total;
}

/**
 * @brief Draws the download/save progress bar.
 */
static void draw_progress(u16 *fb, int x, int y, int w, int h, unsigned received, unsigned total) {
    ui_draw_rect(fb, x, y, w, h, UI_MID);
    if (w <= 2 || h <= 2 || !total) return;
    unsigned pct = progress_percent(received, total);
    int fill_w = ((w - 2) * (int)pct) / 100;
    if (fill_w > 0) ui_fill_rect(fb, x + 1, y + 1, fill_w, h - 2, UI_WHITE);
}

/**
 * @brief Returns whether a list item should show a signal icon.
 */
static bool signal_icon_visible(void) {
    return g_download.active &&
           (g_runState == RUN_CONNECTING || g_runState == RUN_DOWNLOADING);
}

/**
 * @brief Maps signal percentage into the displayed icon level.
 */
static int signal_level(void) {
    if (!g_download.active || !g_download.signal_valid) return 0;
    if (g_download.signal_percent >= SIGNAL_LEVEL_3_PERCENT) return 3;
    if (g_download.signal_percent >= SIGNAL_LEVEL_2_PERCENT) return 2;
    if (g_download.signal_percent >= SIGNAL_LEVEL_1_PERCENT) return 1;
    return 0;
}

/**
 * @brief Draws one discovered-title row on the top screen.
 */
static int draw_item(u16 *fb, int y, const char *title, const char *desc, const char *host, const u16 *icon, bool icon_valid, bool active, const char *time_text) {
    if (y >= UI_H) return y;

    const int icon_x = UI_TOP_ICON_X;
    const int text_x = UI_TOP_TEXT_X;
    const int title_y = y;
    const int icon_y = title_y + UI_TOP_ICON_Y_OFF;
    const int desc_y = title_y + UI_TOP_DESC_Y_OFF;
    const int meta_y = title_y + UI_TOP_META_Y_OFF;
    const int progress_x = UI_TOP_PROGRESS_X;
    const int history_time_x = UI_TOP_HISTORY_TIME_X;
    const int text_bottom = meta_y + 8;
    const int icon_bottom = icon_y + BEACON_ICON_H;
    const int item_h = ((text_bottom > icon_bottom) ? text_bottom : icon_bottom) - y + 4;

    int title_w = UI_W - text_x - UI_TOP_TITLE_RIGHT_PAD;
    draw_icon32(fb, icon_x, icon_y, icon, icon_valid);
    ui_draw_text_limited(fb, text_x, title_y, title && title[0] ? title : "-", title_w, UI_WHITE);

    draw_desc_reserved(fb, text_x, desc_y, desc, UI_W - text_x - UI_TOP_TITLE_RIGHT_PAD, UI_MID);
    int host_w = (active ? progress_x : history_time_x) - text_x - UI_TOP_HOST_RIGHT_PAD;
    ui_draw_text_limited(fb, text_x, meta_y, host && host[0] ? host : "-", host_w, UI_DIM);

    if (active) {
        unsigned progress_received, progress_total;
        download_progress_values(&progress_received, &progress_total);
        const int progress_text_x = progress_x;
        const int progress_text_w = UI_TOP_PROGRESS_TEXT_W;
        const int progress_bar_x = progress_text_x + progress_text_w + UI_TOP_PROGRESS_TEXT_GAP;
        const int progress_text_right = progress_bar_x - UI_TOP_PROGRESS_TEXT_GAP;

        draw_progress(fb, progress_bar_x, meta_y + UI_TOP_PROGRESS_Y_OFF, UI_TOP_PROGRESS_W, UI_TOP_PROGRESS_H,
                      progress_received, progress_total);
        char pct[16];
        if (progress_total) {
            snprintf(pct, sizeof(pct), "%u%%", progress_percent(progress_received, progress_total));
        } else {
            snprintf(pct, sizeof(pct), "--");
        }
        int pct_w = font_measure_utf8(&font_8x8, pct, 0);
        int pct_x = progress_text_right - pct_w;
        if (pct_x < progress_text_x) pct_x = progress_text_x;
        ui_draw_text_limited(fb, pct_x, meta_y, pct, progress_text_x + progress_text_w - pct_x, UI_DIM);
    } else {
        ui_draw_text_limited(fb, history_time_x, meta_y, time_text && time_text[0] ? time_text : "time unknown", UI_W - history_time_x, UI_DIM);
    }

    return y + item_h;
}

/**
 * @brief Returns the y coordinate for the top-screen status banner.
 */
static int top_banner_y(unsigned index) {
    return UI_TOP_CONTENT_Y + UI_TOP_BANNER_CENTER_Y_OFF + UI_TOP_BANNER_Y_SHIFT + (int)index * UI_TOP_BANNER_SLOT_H;
}

/**
 * @brief Draws the active top-screen status bar.
 */
static void draw_top_bar(void) {
    bool show_signal = signal_icon_visible();
    int title_w = show_signal ? UI_TOP_SIGNAL_X - 8 : UI_W - 8;
    u16 bar_color = top_bar_color();
    u16 text_color = top_bar_text_color();

    draw_top_bar_bg(bar_color);
    ui_draw_text_limited(s_topBack, 3, (UI_TOP_BAR_H - 8) / 2 - 1, UI_PROGRAM_NAME,
                      title_w, text_color);
    if (show_signal) {
        draw_signal_icon(s_topBack, UI_TOP_SIGNAL_X, UI_TOP_SIGNAL_Y, signal_level());
    }
}

/**
 * @brief Draws the fatal game-card-removed overlay.
 */
static void draw_game_card_removed_overlay(void) {
    ui_fill_rect(s_topBack, UI_CARD_REMOVED_BOX_X, UI_CARD_REMOVED_BOX_Y,
              UI_CARD_REMOVED_BOX_W, UI_CARD_REMOVED_BOX_H, UI_BLACK);
    ui_draw_rect(s_topBack, UI_CARD_REMOVED_BOX_X, UI_CARD_REMOVED_BOX_Y,
              UI_CARD_REMOVED_BOX_W, UI_CARD_REMOVED_BOX_H, UI_MID);
    ui_draw_text_limited(s_topBack, UI_CARD_REMOVED_TEXT1_X, UI_CARD_REMOVED_TEXT1_Y,
                      "The DS Game Card was removed.",
                      UI_CARD_REMOVED_BOX_X + UI_CARD_REMOVED_BOX_W - UI_CARD_REMOVED_TEXT1_X,
                      UI_RED);
    ui_draw_text_limited(s_topBack, UI_CARD_REMOVED_TEXT2_X, UI_CARD_REMOVED_TEXT2_Y,
                      "Please turn off the power.",
                      UI_CARD_REMOVED_BOX_X + UI_CARD_REMOVED_BOX_W - UI_CARD_REMOVED_TEXT2_X,
                      UI_RED);
}

/**
 * @brief Returns the live slot used only for the active download's player line.
 */
static const ContentSlot *download_player_line_slot(void) {
    const ContentSlot *scan = g_download.scan_slot;
    if (!g_download.active || !g_download.slot) return g_download.slot;
    if (!scan) return g_download.slot;
    if (!scan->used || scan->id != g_download.id ||
        scan->temporary_group_id != g_download.start_temporary_group_id ||
        scan->file_no != g_download.expected_file_no) {
        return g_download.slot;
    }
    return scan;
}

/**
 * @brief Draws the complete top-screen UI for the current run state.
 */
void ui_draw_top(void) {
    clear_fb(s_topBack);
    draw_top_bar();
    unsigned drawn = 0;
    bool drew = false;
    if (drawn < UI_TOP_VISIBLE_BANNERS && g_download.active && g_download.slot) {
        const ContentSlot *s = g_download.slot;
        draw_item(s_topBack, top_banner_y(drawn++), s->title, s->description, s->host_name, s->icon, s->icon_valid, true, NULL);
        drew = true;
    }
    for (unsigned i = 0; i < s_historyCount && drawn < UI_TOP_VISIBLE_BANNERS; i++) {
        const HistoryItem *h = &s_history[i];
        if (!h->used) continue;
        if (!g_repeatDownloads && g_download.active && g_download.id == h->id) continue;
        draw_item(s_topBack, top_banner_y(drawn++), h->title, h->description, h->host_name, h->icon, h->icon_valid, false, h->time_text);
        drew = true;
    }
    if (!drew) {
        ui_draw_text_limited(s_topBack, 4, UI_TOP_CONTENT_Y + 4, "No downloaded contents yet.", UI_W - 8, UI_DIM);
        ui_draw_text_limited(s_topBack, 4, UI_TOP_CONTENT_Y + 16, "Move within range of a distribution to begin.", UI_W - 8, UI_DIM);
    }
    if (s_gameCardRemoved) draw_game_card_removed_overlay();
}

/**
 * @brief Redraws the bottom screen status, transfer details, log, and runtime.
 *
 * The view adapts to idle, active download, saving, and report-generation
 * states. It reads only current UI/download state and writes the backbuffer;
 * presentation is handled separately by `ui_present_framebuffers`.
 */
void ui_draw_bottom(void) {
    clear_fb(s_bottomBack);
    char line[UI_BOTTOM_LINE_BYTES];
    int y = 4;

    ui_format_status_line(line, sizeof(line));
    ui_draw_text_limited(s_bottomBack, 4, y, line, UI_W - 8, UI_WHITE);
    y += 12;

    snprintf(line, sizeof(line), "Content: %u seen, %u saved, %u failed",
             ui_count_seen(), s_savedCount, s_failedCount);
    ui_draw_text_limited(s_bottomBack, 4, y, line, UI_W - 8, UI_MID);
    y += 18;

    if (g_download.active && g_download.slot) {
        const ContentSlot *slot = g_download.slot;
        const char *title = slot->title[0] ? slot->title : "Untitled";
        snprintf(line, sizeof(line), "\"%s\" (%08lx):",
                 title, (unsigned long)g_download.id);
        ui_draw_text_limited(s_bottomBack, 4, y, line, UI_W - 8, UI_WHITE);
        y += 10;

        bool file_work = g_runState == RUN_SAVING || g_runState == RUN_CREATING_REPORT;
        if (file_work) {
            snprintf(line, sizeof(line), "Packets: %u/%u",
                     g_download.received_packets, g_download.total_packets);
        } else {
            u32 rate = ui_download_rate_kbps();
            snprintf(line, sizeof(line), "Packets: %u/%u at %lu KiB/s",
                     g_download.received_packets, g_download.total_packets,
                     (unsigned long)rate);
        }
        ui_draw_text_limited(s_bottomBack, 4, y, line, UI_W - 8, UI_DIM);
        y += 10;

        char elapsed[24], left[24];
        ui_format_duration_short(ui_download_elapsed_seconds(), elapsed, sizeof(elapsed));
        u32 seconds_left = 0;
        if (!file_work && ui_download_seconds_left(&seconds_left)) {
            ui_format_duration_short(seconds_left, left, sizeof(left));
            snprintf(line, sizeof(line), "Time: %s elapsed, %s left", elapsed, left);
        } else {
            snprintf(line, sizeof(line), "Time: %s elapsed", elapsed);
        }
        ui_draw_text_limited(s_bottomBack, 4, y, line, UI_W - 8, UI_DIM);
        y += 10;

        bool download_complete = g_download.total_packets &&
                                 g_download.received_packets >= g_download.total_packets;
        if (!download_complete) {
            ui_format_user_line(download_player_line_slot(), line, sizeof(line));
            ui_draw_text_limited(s_bottomBack, 4, y, line, UI_W - 8, UI_DIM);
        }
        y += 16;
    } else {
        y += 46;
    }

    ui_draw_text_limited(s_bottomBack, 4, y, "Log:", UI_W - 8, UI_WHITE);
    y += 10;
    draw_wrapped_log_lines(s_bottomBack, y);
    ui_format_run_time(line, sizeof(line));
    int version_w = font_measure_utf8(&font_8x8, APP_VERSION, 0);
    int version_x = UI_W - 3 - version_w;
    int footer_y = UI_H - 3 - font_8x8.cell_h;
    if (version_x < 4) version_x = 4;
    ui_draw_text_limited(s_bottomBack, 4, footer_y, line, version_x - 7, UI_DARK);
    ui_draw_text_limited(s_bottomBack, version_x, footer_y, APP_VERSION,
                            UI_W - 3 - version_x, UI_DARK);
}

/**
 * @brief Copies software framebuffers to video memory.
 */
void ui_present_framebuffers(void) {
    if (!s_topFb || !s_bottomFb) return;
    DC_FlushRange(s_topBack, sizeof(s_topBack));
    DC_FlushRange(s_bottomBack, sizeof(s_bottomBack));
    dmaCopyWords(3, s_topBack, s_topFb, sizeof(s_topBack));
    dmaCopyWords(3, s_bottomBack, s_bottomFb, sizeof(s_bottomBack));
}

/**
 * @brief Returns the active fade brightness value.
 */
unsigned ui_fade_factor(unsigned step, unsigned steps) {
    if (step >= steps) return UI_MASTER_BRIGHT_MAX;
    return (step * UI_MASTER_BRIGHT_MAX + steps / 2u) / steps;
}

/**
 * @brief Applies white brightness to both displays.
 */
void ui_set_white_brightness(unsigned factor) {
    if (factor > UI_MASTER_BRIGHT_MAX) factor = UI_MASTER_BRIGHT_MAX;
    u16 v = factor ? (u16)(UI_MASTER_BRIGHT_WHITE | factor) : 0;
    REG_MASTER_BRIGHT = v;
    REG_MASTER_BRIGHT_SUB = v;
}
