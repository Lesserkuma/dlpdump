/**
 * @file ui.c
 * @brief Owns high-level ARM9 UI state, logging and frame scheduling.
 */
#include "ui_internal.h"

u16 *s_topFb;
u16 *s_bottomFb;
u16 s_topBack[UI_FRAME_PIXELS] ALIGNED_ATTR(32);
u16 s_bottomBack[UI_FRAME_PIXELS] ALIGNED_ATTR(32);
bool s_dirty = true;
bool s_gameCardRemoved;
unsigned s_redrawTicker;
char s_logLines[UI_LOG_LINES][UI_LOG_LINE_BYTES];
unsigned s_logHead;
unsigned s_failedCount;
unsigned s_savedCount;
time_t s_startTime;

HistoryItem s_history[UI_HISTORY_MAX];
unsigned s_historyCount;
u32 s_historyOrder;

/**
 * @brief Marks the UI as needing redraw.
 */
void ui_mark_dirty(void) {
    s_dirty = true;
}

/**
 * @brief Appends one formatted message to the on-screen log.
 */
void ui_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_logLines[s_logHead], sizeof(s_logLines[s_logHead]), fmt, ap);
    va_end(ap);
    ui_normalize_log_line(s_logLines[s_logHead]);
    ui_trim_incomplete_utf8_tail(s_logLines[s_logHead]);
    if (!s_logLines[s_logHead][0]) return;
    s_logHead = (s_logHead + 1) % UI_LOG_LINES;
    s_dirty = true;
}

/**
 * @brief Increments the visible failure counter.
 */
void ui_count_failure(void) {
    s_failedCount++;
    s_dirty = true;
}

/**
 * @brief Records a completed output base in UI history.
 */
void ui_record_save_success(const ContentSlot *slot) {
    if (!slot) return;
    ui_record_saved(slot);
    s_savedCount++;
    s_dirty = true;
}

/**
 * @brief Forces an immediate UI redraw on both screens.
 */
void ui_draw_now(void) {
    ui_draw_top();
    ui_draw_bottom();
    ui_present_framebuffers();
    s_dirty = false;
}

/**
 * @brief Runs the short fade-in animation after startup.
 */
static void fade_in_from_white(void) {
    if (!s_topFb || !s_bottomFb) return;
    ui_set_white_brightness(UI_MASTER_BRIGHT_MAX);
    ui_draw_top();
    ui_draw_bottom();
    ui_present_framebuffers();
    for (unsigned step = UI_FADE_FRAMES; step > 0; step--) {
        swiWaitForVBlank();
        ui_set_white_brightness(ui_fade_factor(step, UI_FADE_FRAMES));
    }
    swiWaitForVBlank();
    ui_set_white_brightness(0);
    s_dirty = false;
}

/**
 * @brief Fades the display to white before boot handover.
 */
void ui_fade_to_white(void) {
    if (!s_topFb || !s_bottomFb) return;
    ui_set_white_brightness(0);
    for (unsigned step = 1; step <= UI_FADE_FRAMES; step++) {
        swiWaitForVBlank();
        ui_set_white_brightness(ui_fade_factor(step, UI_FADE_FRAMES));
    }
    ui_fill_rect(s_topBack, 0, 0, UI_W, UI_H, UI_WHITE);
    ui_fill_rect(s_bottomBack, 0, 0, UI_W, UI_H, UI_WHITE);
    ui_present_framebuffers();
    ui_set_white_brightness(0);
    s_dirty = false;
}

/**
 * @brief Switches the UI into the game-card-removed state.
 */
void ui_show_game_card_removed(void) {
    s_gameCardRemoved = true;
    s_dirty = true;
}

/**
 * @brief Runs one UI frame update when the display is dirty.
 */
void ui_frame(void) {
    bool periodic = g_download.active || g_runState != RUN_SCANNING;
    if (s_dirty || (periodic && ++s_redrawTicker >= UI_ACTIVE_REDRAW_FRAMES)) {
        s_redrawTicker = 0;
        ui_draw_now();
    } else if (!periodic) {
        s_redrawTicker = 0;
    }
}

/**
 * @brief Initializes video, fonts, UI history and the first screen draw.
 */
void ui_init(void) {
    s_startTime = time(NULL);
    videoSetMode(MODE_5_2D);
    videoSetModeSub(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);
    int topBg = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    int bottomBg = bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    bgSetPriority(topBg, 0);
    bgSetPriority(bottomBg, 0);
    s_topFb = (u16*)bgGetGfxPtr(topBg);
    s_bottomFb = (u16*)bgGetGfxPtr(bottomBg);
    bgUpdate();
    ui_set_white_brightness(0);
    fade_in_from_white();
}
