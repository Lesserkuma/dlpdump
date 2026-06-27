/**
 * @file ui_history.c
 * @brief Maintains the saved-download history shown on the top screen.
 */
#include "ui_internal.h"

/**
 * @brief Orders saved-history rows by descending timestamp.
 */
static int history_compare(const HistoryItem *a, const HistoryItem *b) {
    if (a->completed_time != b->completed_time) return a->completed_time > b->completed_time ? -1 : 1;
    if (a->order != b->order) return a->order > b->order ? -1 : 1;
    return 0;
}

/**
 * @brief Sorts the saved-history list for display.
 */
static void sort_history(void) {
    for (unsigned i = 1; i < s_historyCount; i++) {
        HistoryItem tmp = s_history[i];
        unsigned j = i;
        while (j && history_compare(&tmp, &s_history[j - 1]) < 0) {
            s_history[j] = s_history[j - 1];
            j--;
        }
        s_history[j] = tmp;
    }
}

/**
 * @brief Finds an existing saved-history row by output base.
 */
static HistoryItem *find_history(u32 id) {
    for (unsigned i = 0; i < s_historyCount; i++) if (s_history[i].used && s_history[i].id == id) return &s_history[i];
    return NULL;
}

/**
 * @brief Allocates or reuses a saved-history row.
 */
static HistoryItem *alloc_history(u32 id) {
    if (!g_repeatDownloads) {
        HistoryItem *it = find_history(id);
        if (it) return it;
    }
    if (s_historyCount < UI_HISTORY_MAX) return &s_history[s_historyCount++];
    return &s_history[UI_HISTORY_MAX - 1];
}

/**
 * @brief Returns whether a compacted history prefix already contains an ID.
 */
static bool history_prefix_contains_id(unsigned count, u32 id) {
    for (unsigned i = 0; i < count; i++) {
        if (s_history[i].used && s_history[i].id == id) return true;
    }
    return false;
}

/**
 * @brief Removes older duplicate saved-history rows after disabling repeats.
 */
void ui_deduplicate_history(void) {
    sort_history();

    bool changed = false;
    unsigned out = 0;
    for (unsigned i = 0; i < s_historyCount; i++) {
        if (!s_history[i].used || history_prefix_contains_id(out, s_history[i].id)) {
            changed = true;
            continue;
        }
        if (out != i) {
            s_history[out] = s_history[i];
            changed = true;
        }
        out++;
    }

    for (unsigned i = out; i < s_historyCount; i++) memset(&s_history[i], 0, sizeof(s_history[i]));
    if (out != s_historyCount) changed = true;
    s_historyCount = out;
    if (changed) s_dirty = true;
}

/**
 * @brief Records one successful save in the UI history list.
 */
void ui_record_saved(const ContentSlot *slot) {
    if (!slot) return;
    HistoryItem *it = alloc_history(slot->id);
    memset(it, 0, sizeof(*it));
    it->used = true; it->id = slot->id; it->completed_time = time(NULL); it->order = ++s_historyOrder;
    ui_format_time_text(it->completed_time, it->time_text, sizeof(it->time_text));
    ui_copy_text(it->host_name, sizeof(it->host_name), slot->host_name);
    ui_copy_text(it->title, sizeof(it->title), slot->title);
    ui_copy_text(it->description, sizeof(it->description), slot->description);
    it->icon_valid = slot->icon_valid;
    if (slot->icon_valid) memcpy(it->icon, slot->icon, sizeof(it->icon));
    if (!it->title[0]) snprintf(it->title, sizeof(it->title), "%08lx", (unsigned long)slot->id);
    sort_history();
    s_dirty = true;
}
