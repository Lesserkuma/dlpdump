/**
 * @file ui_text.c
 * @brief Formats status, timing, player and log text for the UI.
 */
#include "ui_internal.h"

/**
 * @brief Trims a UI string at an incomplete UTF-8 tail.
 */
void ui_trim_incomplete_utf8_tail(char *s) {
    text_trim_incomplete_utf8_tail(s);
}

/**
 * @brief Normalizes a log line so it fits the bitmap font renderer.
 */
void ui_normalize_log_line(char *s) {
    if (!s) return;
    char *read = s;
    char *write = s;
    bool pending_space = false;

    while (*read) {
        unsigned char c = (unsigned char)*read++;
        if (c == '\r' || c == '\n') {
            pending_space = false;
            if (write > s && write[-1] != '\n') *write++ = '\n';
            continue;
        }
        if (c == '\t') c = ' ';
        if (c == ' ') {
            pending_space = write != s && write[-1] != '\n';
            continue;
        }
        if (pending_space) *write++ = ' ';
        pending_space = false;
        *write++ = (char)c;
    }
    while (write > s && (write[-1] == ' ' || write[-1] == '\n')) write--;
    *write = 0;
}

/**
 * @brief Returns the status label for the current run state.
 */
static const char *status_text(void) {
    if (download_waiting_for_transfer_start()) return "Waiting for data…";
    if (download_waiting_for_final()) return "Waiting for completion…";
    switch (g_runState) {
        case RUN_CONNECTING: return "Connecting…";
        case RUN_DOWNLOADING: return "Downloading…";
        case RUN_CHECKING_RSA_HASH:
            return g_verifyStatus == VERIFY_SKIPPED ?
                   "RSA verification skipped." : "Verifying RSA signature…";
        case RUN_SAVING: return "Saving…";
        case RUN_CREATING_REPORT: return "Creating Dump Report…";
        default: return "Scanning…";
    }
}

/**
 * @brief Returns whether the status line should include WLAN channel text.
 */
static bool status_shows_channel(void) {
    return g_runState == RUN_SCANNING ||
           g_runState == RUN_CONNECTING ||
           g_runState == RUN_DOWNLOADING;
}

/**
 * @brief Formats the top-screen status line for the current run state.
 */
void ui_format_status_line(char *out, size_t out_size) {
    if (!out_size) return;
    const char *st = status_text();
    if (download_waiting_for_transfer_start()) {
        unsigned secs = download_transfer_wait_seconds_remaining();
        if (g_currentWlanChannel >= 1 && g_currentWlanChannel <= 13 && g_download.signal_valid) {
            snprintf(out, out_size, "Status: %s (%us, Channel %u, Signal %u%%)", st, secs, g_currentWlanChannel, g_download.signal_percent);
        } else if (g_currentWlanChannel >= 1 && g_currentWlanChannel <= 13) {
            snprintf(out, out_size, "Status: %s (%us, Channel %u)", st, secs, g_currentWlanChannel);
        } else {
            snprintf(out, out_size, "Status: %s (%us)", st, secs);
        }
        return;
    }
    if (download_waiting_for_final()) {
        unsigned secs = download_final_wait_seconds_remaining();
        snprintf(out, out_size, "Status: %s (%us)", st, secs);
        return;
    }
    if (g_runState == RUN_CREATING_REPORT) {
        if (g_download.save_progress_valid) {
            snprintf(out, out_size, "Status: %s (%u%%) - Press B to skip", st, g_download.save_percent);
        } else {
            snprintf(out, out_size, "Status: %s - Press B to skip", st);
        }
        return;
    }
    if (g_runState == RUN_SAVING && g_download.save_progress_valid) {
        snprintf(out, out_size, "Status: %s (%u%%)", st, g_download.save_percent);
        return;
    }
    if (!status_shows_channel()) {
        snprintf(out, out_size, "Status: %s", st);
    } else if (g_currentWlanChannel >= 1 && g_currentWlanChannel <= 13 && g_download.active && g_download.signal_valid) {
        snprintf(out, out_size, "Status: %s (Channel %u, Signal %u%%)", st, g_currentWlanChannel, g_download.signal_percent);
    } else if (g_currentWlanChannel >= 1 && g_currentWlanChannel <= 13) {
        snprintf(out, out_size, "Status: %s (Channel %u)", st, g_currentWlanChannel);
    } else {
        snprintf(out, out_size, "Status: %s", st);
    }
}

/**
 * @brief Counts scan slots currently visible to the user.
 */
unsigned ui_count_seen(void) {
    unsigned seen = 0;
    for (unsigned i = 0; i < CONTENT_SLOT_COUNT; i++) if (g_slots[i].used) seen++;
    return seen;
}

/**
 * @brief Formats a short seconds counter for UI display.
 */
void ui_format_duration_short(u32 seconds, char *out, size_t out_size) {
    if (!out_size) return;
    if (seconds < 60) {
        snprintf(out, out_size, "%lus", (unsigned long)seconds);
    } else if (seconds < 3600) {
        snprintf(out, out_size, "%lum %lus",
                 (unsigned long)(seconds / 60),
                 (unsigned long)(seconds % 60));
    } else {
        snprintf(out, out_size, "%luh %lum",
                 (unsigned long)(seconds / 3600),
                 (unsigned long)((seconds / 60) % 60));
    }
}

/**
 * @brief Formats scan/download/save runtime text.
 */
void ui_format_run_time(char *out, size_t out_size) {
    if (!out_size) return;
    time_t now = time(NULL);
    u32 seconds = (s_startTime > 0 && now > s_startTime) ? (u32)(now - s_startTime) : 0;
    u32 days = seconds / 86400u;
    seconds %= 86400u;
    u32 hours = seconds / 3600u;
    seconds %= 3600u;
    u32 minutes = seconds / 60u;
    seconds %= 60u;

    char *p = out;
    size_t left = out_size;
    int n = snprintf(p, left, "Run time:");
    if (n < 0 || (size_t)n >= left) {
        out[out_size - 1] = 0;
        return;
    }
    p += n; left -= (size_t)n;

    bool wrote = false;
#define APPEND_RUN_TIME_PART(value, suffix) do { \
        if ((value) || wrote) { \
            n = snprintf(p, left, " %lu%s", (unsigned long)(value), (suffix)); \
            if (n < 0 || (size_t)n >= left) { \
                out[out_size - 1] = 0; \
                return; \
            } \
            p += n; \
            left -= (size_t)n; \
            wrote = true; \
        } \
    } while (0)
    APPEND_RUN_TIME_PART(days, "d");
    APPEND_RUN_TIME_PART(hours, "h");
    APPEND_RUN_TIME_PART(minutes, "m");
    APPEND_RUN_TIME_PART(seconds, "s");
#undef APPEND_RUN_TIME_PART
}

/**
 * @brief Returns the current parent payload size in bytes.
 */
static unsigned current_payload_size(void) {
    if (g_download.max_payload) return g_download.max_payload;
    if (g_download.parent_packet_max_bytes > 6) return g_download.parent_packet_max_bytes - 6;
    return 0;
}

/**
 * @brief Returns the expected download payload byte count.
 */
static u32 download_total_bytes(void) {
    if (g_download.have_rsa) {
        u32 total = 0;
        for (unsigned i = 0; i < 3; i++) total += g_download.sec[i].size;
        return total;
    }
    return (u32)g_download.total_packets * current_payload_size();
}

/**
 * @brief Returns the number of payload bytes received so far.
 */
static u32 download_received_bytes(void) {
    u32 total = download_total_bytes();
    if (g_download.total_packets && g_download.received_packets >= g_download.total_packets) return total;
    u32 bytes = (u32)g_download.received_packets * current_payload_size();
    return bytes > total && total ? total : bytes;
}

/**
 * @brief Returns elapsed seconds for the active download.
 */
u32 ui_download_elapsed_seconds(void) {
    if (!g_download.active || !g_download.start_time) return 0;
    time_t t = time(NULL);
    if (t <= 0 || (u32)t <= g_download.start_time) return 0;
    return (u32)t - g_download.start_time;
}

/**
 * @brief Computes the current download rate in kB/s units.
 */
u32 ui_download_rate_kbps(void) {
    if (!g_download.active || !g_download.data_start_time) return 0;
    time_t t = time(NULL);
    if (t <= 0 || (u32)t <= g_download.data_start_time) return 0;
    u32 elapsed = (u32)t - g_download.data_start_time;
    u32 bytes = download_received_bytes();
    if (!elapsed || !bytes) return 0;
    return (u32)(((u64)bytes + (u64)elapsed * 512u) / ((u64)elapsed * 1024u));
}

/**
 * @brief Estimates seconds remaining for the active download.
 */
bool ui_download_seconds_left(u32 *out) {
    if (!out || !g_download.active || !g_download.data_start_time) return false;
    u32 done = download_received_bytes();
    u32 total = download_total_bytes();
    if (!done || !total || done >= total) {
        *out = 0;
        return total && done >= total;
    }
    time_t t = time(NULL);
    if (t <= 0 || (u32)t <= g_download.data_start_time) return false;
    u32 elapsed = (u32)t - g_download.data_start_time;
    u64 bytes_per_second = ((u64)done + elapsed / 2u) / elapsed;
    if (!bytes_per_second) return false;
    *out = (u32)(((u64)(total - done) + bytes_per_second - 1u) / bytes_per_second);
    return true;
}

/**
 * @brief Appends bounded ASCII text to a UI string buffer.
 */
static void append_ascii_text(char *out, size_t out_size, const char *text) {
    if (!out_size || !text || !*text) return;
    size_t len = strlen(out);
    if (len + 1 >= out_size) return;
    snprintf(out + len, out_size - len, "%s", text);
    ui_trim_incomplete_utf8_tail(out);
}

/**
 * @brief Appends bounded UTF-8 text to a UI string buffer.
 */
static void append_utf8_text(char *out, size_t out_size, const char *text) {
    if (!out_size || !text || !*text) return;
    size_t len = strlen(out);
    const char *p = text;
    u32 cp;
    while (text_utf8_next(&p, &cp)) {
        char tmp[4];
        int n = text_codepoint_to_utf8(cp, tmp);
        if (n <= 0 || len + (size_t)n >= out_size) break;
        memcpy(out + len, tmp, (size_t)n);
        len += (size_t)n;
    }
    out[len] = 0;
}

/**
 * @brief Returns whether the player line belongs to the current active download.
 */
static bool user_line_slot_is_active_download(const ContentSlot *slot) {
    return g_download.active && (slot == g_download.slot || slot == g_download.scan_slot);
}

/**
 * @brief Formats the user/player detail line for the top screen.
 */
void ui_format_user_line(const ContentSlot *slot, char *out, size_t out_size) {
    if (!out_size) return;
    unsigned current = slot ? slot->connected_count : 0;
    unsigned max_players = slot && slot->max_players ? slot->max_players : 16;
    snprintf(out, out_size, "Connected: %u/%u", current, max_players);

    bool wrote_name = false;
    if (slot) {
        for (unsigned i = 0; i < MEMBER_SLOT_COUNT; i++) {
            if (!(slot->member_name_mask & (1u << i)) || !slot->member_names[i][0]) continue;
            if (user_line_slot_is_active_download(slot) &&
                g_download.assoc_aid == (u16)(i + 1u)) {
                continue;
            }
            if (!wrote_name) {
                append_ascii_text(out, out_size, " (");
            } else {
                append_ascii_text(out, out_size, ", ");
            }
            append_utf8_text(out, out_size, slot->member_names[i]);
            wrote_name = true;
        }
    }
    if (wrote_name) append_ascii_text(out, out_size, ")");
}

/**
 * @brief Returns the UTF-8 byte count that fits a pixel width.
 */
static size_t text_fit_bytes(const char *text, int max_w) {
    if (!text || max_w <= 0) return 0;
    const char *s = text;
    const char *fit = text;
    const char *word_break = NULL;
    int w = 0;
    uint32_t cp;
    while (*s) {
        const char *before = s;
        if (!text_utf8_next(&s, &cp)) break;
        if (cp == '\n' || cp == '\r') break;
        if (cp == '\t') cp = ' ';
        int glyph = font_find_glyph(&font_8x8, cp);
        if (glyph < 0) {
            fit = s;
            continue;
        }
        int adv = font_glyph_advance(&font_8x8, (u16)glyph, 0);
        if (w + adv > max_w) {
            if (word_break && word_break > text) return (size_t)(word_break - text);
            if (fit == text) fit = before;
            break;
        }
        w += adv;
        fit = s;
        if (cp == ' ') word_break = s;
    }
    return (size_t)(fit - text);
}

/**
 * @brief Copies a bounded UTF-8 segment into an output buffer.
 */
static void copy_text_segment(char *out, size_t out_size, const char *text, size_t bytes) {
    if (!out_size) return;
    if (!text) {
        out[0] = 0;
        return;
    }
    if (bytes >= out_size) bytes = out_size - 1;
    memcpy(out, text, bytes);
    out[bytes] = 0;
    ui_trim_incomplete_utf8_tail(out);
}

/**
 * @brief Wraps one log line segment to the available pixel width.
 */
unsigned ui_wrap_log_line(const char *text, UiWrappedLogLine *out) {
    if (!out) return 0;
    out->count = 0;
    if (!text || !text[0]) return 0;

    size_t first_len = text_fit_bytes(text, UI_W - 8);
    if (!first_len) {
        const char *s = text;
        uint32_t cp;
        if (text_utf8_next(&s, &cp)) first_len = (size_t)(s - text);
    }
    copy_text_segment(out->text[0], sizeof(out->text[0]), text, first_len);
    out->count = 1;

    const char *rest = text + first_len;
    while (*rest == ' ' || *rest == '\t' || *rest == '\r' || *rest == '\n') rest++;
    if (*rest) {
        snprintf(out->text[1], sizeof(out->text[1]), UI_LOG_CONTINUATION_PREFIX "%s", rest);
        ui_trim_incomplete_utf8_tail(out->text[1]);
        out->count = 2;
    }
    return out->count;
}

/**
 * @brief Formats wall-clock time for UI display.
 */
void ui_format_time_text(time_t t, char *out, size_t out_size) {
    if (!out_size) return;
    out[0] = 0;
    if (t <= 0) {
        snprintf(out, out_size, "time unknown");
        return;
    }
    struct tm *lt = localtime(&t);
    if (!lt) {
        snprintf(out, out_size, "time unknown");
        return;
    }
    if (!strftime(out, out_size, "%Y-%m-%d %H:%M:%S", lt)) {
        snprintf(out, out_size, "time unknown");
    }
}

/**
 * @brief Copies UI text while preserving UTF-8 validity.
 */
void ui_copy_text(char *dst, size_t dst_size, const char *src) {
    if (!dst_size) return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    dst[0] = 0;
    append_utf8_text(dst, dst_size, src);
}
