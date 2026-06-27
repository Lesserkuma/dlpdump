/**
 * @file file_paths.c
 * @brief Builds, sanitizes and reserves output paths for saved dump sets.
 */
#include "file_internal.h"

/**
 * @brief Returns whether a filename ends with the requested extension.
 */
static bool has_ext(const char *name, const char *ext) {
    size_t name_len = strlen(name);
    size_t ext_len = strlen(ext);
    return name_len >= ext_len && strcmp(name + name_len - ext_len, ext) == 0;
}

/** @brief Checks whether a file exists and can be opened for reading. */
static bool file_exists(const char *path) {
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) return false;
    fclose(f);
    return true;
}

/**
 * @brief Returns whether a byte is an ASCII decimal digit.
 */
static bool is_dec_digit(char c) {
    return c >= '0' && c <= '9';
}

/**
 * @brief Returns whether a byte is an ASCII hexadecimal digit.
 */
static bool is_hex_digit(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

/**
 * @brief Returns whether a dump name uses the current timestamp prefix format.
 */
static bool has_new_timestamp_prefix(const char *name) {
    if (!name) return false;
    if (strlen(name) < 15) return false;
    for (unsigned i = 0; i < 8; i++) if (!is_dec_digit(name[i])) return false;
    if (name[8] != '-') return false;
    for (unsigned i = 9; i < 15; i++) if (!is_dec_digit(name[i])) return false;
    return true;
}

/**
 * @brief Returns whether a dump name uses the older ID-plus-title format.
 */
static bool old_id_title_prefix(const char *title, size_t title_len) {
    if (title_len < 11) return false;
    if (!title || title[0] != '[') return false;
    for (unsigned i = 1; i <= 8; i++) if (!is_hex_digit(title[i])) return false;
    return title[9] == ']' && title[10] == ' ';
}

/**
 * @brief Extracts sorting metadata from a saved dump filename.
 */
static bool parse_download_name_with_ext(const char *name, const char *ext,
                                         char *title, size_t title_size) {
    if (title && title_size) title[0] = 0;
    if (!name || !ext || !has_ext(name, ext)) return false;
    if (!has_new_timestamp_prefix(name)) return false;

    size_t len = strlen(name);
    size_t ext_len = strlen(ext);
    if (len < ext_len) return false;

    size_t base_len = len - ext_len;
    size_t title_len = 0;
    const char *title_start = NULL;
    if (name[15] == '.') {
        if (base_len != 15u) return false;
        title_start = "";
    } else {
        if (name[15] != ' ' || name[16] != '-' || name[17] != ' ') return false;
        title_start = name + 18;
        if (base_len <= 18u) return false;
        title_len = base_len - 18u;
        if (!title_len || old_id_title_prefix(title_start, title_len)) return false;
    }

    if (title && title_size) {
        if (title_len >= title_size) title_len = title_size - 1;
        memcpy(title, title_start, title_len);
        title[title_len] = 0;
    }
    return true;
}

/**
 * @brief One parsed output-directory entry participating in latest-pair search.
 */
typedef struct {
    char name[OUTPUT_BASE_BYTES + 8];
    char base[OUTPUT_BASE_BYTES];
    char key[15];
    char title[TEXT_UTF8_BYTES(TITLE_CHARS)];
    bool valid;
} DownloadNameInfo;

/**
 * @brief Builds a sortable key for timestamp-prefixed dump names.
 */
static void timestamp_sort_key(const char *name, char out[15]) {
    memcpy(out, name, 8);
    memcpy(out + 8, name + 9, 6);
    out[14] = 0;
}

/**
 * @brief Compares parsed dump names by timestamp and basename.
 */
static int download_name_compare(const DownloadNameInfo *a, const DownloadNameInfo *b) {
    int c = strcmp(a->key, b->key);
    if (c) return c;
    return strcmp(a->base, b->base);
}

/**
 * @brief Extracts comparable name metadata from one directory entry.
 */
static bool parse_download_entry(const char *name, const char *ext,
                                 bool keep_title, DownloadNameInfo *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!parse_download_name_with_ext(name, ext,
                                      keep_title ? out->title : NULL,
                                      keep_title ? sizeof(out->title) : 0)) {
        return false;
    }

    size_t name_len = strlen(name);
    size_t ext_len = strlen(ext);
    size_t base_len = name_len - ext_len;
    if (name_len >= sizeof(out->name) || base_len >= sizeof(out->base)) return false;

    memcpy(out->name, name, name_len + 1u);
    memcpy(out->base, name, base_len);
    out->base[base_len] = 0;
    timestamp_sort_key(name, out->key);
    out->valid = true;
    return true;
}

/**
 * @brief Returns whether a candidate is below the exclusive retry bound.
 */
static bool download_entry_below_bound(const DownloadNameInfo *entry,
                                       const DownloadNameInfo *upper_bound) {
    return !upper_bound || !upper_bound->valid ||
           download_name_compare(entry, upper_bound) < 0;
}

/**
 * @brief Updates the latest parsed entry below the active bound.
 */
static void update_latest_download_entry(DownloadNameInfo *best,
                                         const DownloadNameInfo *candidate,
                                         const DownloadNameInfo *upper_bound) {
    if (!download_entry_below_bound(candidate, upper_bound)) return;
    if (!best->valid || download_name_compare(candidate, best) > 0) {
        *best = *candidate;
    }
}

/**
 * @brief Scans once for newest NDS and BCN names below an optional upper bound.
 */
static bool scan_latest_download_entries(const DownloadNameInfo *upper_bound,
                                         DownloadNameInfo *best_nds,
                                         DownloadNameInfo *best_bcn) {
    if (!best_nds || !best_bcn) return false;
    memset(best_nds, 0, sizeof(*best_nds));
    memset(best_bcn, 0, sizeof(*best_bcn));

    DIR *dir = opendir(OUTPUT_DIR);
    if (!dir) return false;

    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        DownloadNameInfo candidate;
        const char *name = de->d_name;
        if (parse_download_entry(name, ".nds", true, &candidate)) {
            update_latest_download_entry(best_nds, &candidate, upper_bound);
        } else if (parse_download_entry(name, ".bcn", false, &candidate)) {
            update_latest_download_entry(best_bcn, &candidate, upper_bound);
        }
    }
    closedir(dir);
    return true;
}

/**
 * @brief Checks whether a parsed output entry can be opened for reading.
 */
static bool download_entry_file_exists(const DownloadNameInfo *entry) {
    char path[256];
    if (!entry || !entry->valid) return false;
    if (!path_join(path, sizeof(path), OUTPUT_DIR, entry->name)) return false;
    return file_exists(path);
}

/**
 * @brief Finds the newest saved NDS dump and returns its path, base and title.
 */
bool file_find_latest_download(char *path, size_t path_size,
                              char *base_name, size_t base_size,
                              char *title, size_t title_size) {
    DownloadNameInfo upper_bound = {0};
    DownloadNameInfo best_nds;
    DownloadNameInfo best_bcn;

    for (;;) {
        if (!scan_latest_download_entries(upper_bound.valid ? &upper_bound : NULL,
                                          &best_nds, &best_bcn)) {
            return false;
        }
        if (!best_nds.valid || !best_bcn.valid) return false;

        int cmp = download_name_compare(&best_nds, &best_bcn);
        if (cmp == 0) {
            if (download_entry_file_exists(&best_bcn)) break;
            upper_bound = best_bcn;
            continue;
        }
        upper_bound = cmp > 0 ? best_nds : best_bcn;
    }

    if (path && path_size && !path_join(path, path_size, OUTPUT_DIR, best_nds.name)) return false;
    if (base_name && base_size) {
        size_t base_len = strlen(best_nds.base);
        if (base_len >= base_size) base_len = base_size - 1;
        memcpy(base_name, best_nds.base, base_len);
        base_name[base_len] = 0;
    }
    if (title && title_size) {
        strncpy(title, best_nds.title, title_size - 1);
        title[title_size - 1] = 0;
    }
    return true;
}

/**
 * @brief Removes trailing characters that are invalid at the end of a filename.
 */
static void trim_filename_tail(char *s) {
    if (!s) return;
    text_trim_incomplete_utf8_tail(s);
    size_t len = strlen(s);
    while (len && (s[len - 1] == ' ' || s[len - 1] == '.')) s[--len] = 0;
}

/**
 * @brief Copies title text into a filesystem-safe filename component.
 */
static void sanitize_filename_part(char *out, size_t out_size, const char *text) {
    if (!out_size) return;
    out[0] = 0;
    if (!text) return;

    size_t pos = 0;
    while (*text && pos + 1 < out_size) {
        unsigned char c = (unsigned char)*text++;
        if (path_filename_char_is_forbidden(c)) continue;
        out[pos++] = (char)c;
    }
    out[pos] = 0;
    trim_filename_tail(out);
}

/**
 * @brief Formats the current local timestamp for output base names.
 */
static void make_current_stamp(char *out, size_t out_size) {
    if (!out_size) return;
    strncpy(out, "00000000-000000", out_size);
    out[out_size - 1] = 0;
    time_t t = time(NULL);
    struct tm *lt = t > 0 ? localtime(&t) : NULL;
    if (lt) strftime(out, out_size, "%Y%m%d-%H%M%S", lt);
}

/**
 * @brief Returns whether an output base name passes path-safety checks.
 */
bool file_base_name_is_safe(const char *base_name) {
    return path_output_base_is_safe(base_name);
}

/**
 * @brief Builds the preferred timestamped output base for a title.
 */
bool file_make_output_base(char *out, size_t out_size, const char *title) {
    if (!out || !out_size) return false;
    out[0] = 0;
    char stamp[16];
    char safe_title[OUTPUT_BASE_BYTES];
    make_current_stamp(stamp, sizeof(stamp));
    sanitize_filename_part(safe_title, sizeof(safe_title), title);
    int n = 0;
    if (safe_title[0]) {
        n = snprintf(out, out_size, "%s - %s", stamp, safe_title);
    } else {
        n = snprintf(out, out_size, "%s", stamp);
    }
    if (n < 0 || (size_t)n >= out_size) {
        out[0] = 0;
        return false;
    }
    trim_filename_tail(out);
    return file_base_name_is_safe(out);
}

/**
 * @brief Builds a fallback output base when title-based reservation fails.
 */
bool file_make_output_fallback_base(char *out, size_t out_size, const char *preferred_base) {
    if (!out || !out_size) return false;
    out[0] = 0;
    if (preferred_base && has_new_timestamp_prefix(preferred_base)) {
        int n = snprintf(out, out_size, "%.*s", 15, preferred_base);
        if (n < 0 || (size_t)n >= out_size) {
            out[0] = 0;
            return false;
        }
        trim_filename_tail(out);
        return file_base_name_is_safe(out);
    }
    return file_make_output_base(out, out_size, NULL);
}

/**
 * @brief Returns whether any final or temporary file uses an output base.
 */
static bool output_base_has_collision(const char *base_name) {
    static const char *const output_collision_extensions[] = {
        ".nds", ".bcn", ".txt", ".pcap", ".log", ".reserve.tmp",
    };
    char path[256];
    for (unsigned i = 0; i < ARRAY_COUNT(output_collision_extensions); i++) {
        if (!path_make_output_file(path, sizeof(path), base_name, output_collision_extensions[i])) return true;
        if (file_exists(path)) return true;
    }
    return false;
}

/**
 * @brief Appends a numeric collision suffix to an output base name.
 */
static bool add_output_suffix(char *out, size_t out_size, const char *root, unsigned suffix) {
    if (!out || !out_size || !root || !root[0]) return false;
    out[0] = 0;
    int n;
    if (!suffix) {
        n = snprintf(out, out_size, "%s", root);
    } else {
        size_t root_len = strlen(root);
        size_t max_root = out_size > 5u ? out_size - 5u : 0;
        if (!max_root) return false;
        if (root_len > max_root) root_len = max_root;
        while (root_len && (root[root_len - 1u] == ' ' || root[root_len - 1u] == '.')) root_len--;
        n = snprintf(out, out_size, "%.*s-%03u", (int)root_len, root, suffix);
    }
    if (n < 0 || (size_t)n >= out_size) {
        out[0] = 0;
        return false;
    }
    return file_base_name_is_safe(out);
}

/**
 * @brief Reserves a collision-free output base for the next dump.
 */
bool file_reserve_output_base(char *out, size_t out_size, const char *title) {
    static char s_last_root[OUTPUT_BASE_BYTES];
    static unsigned s_next_suffix;
    if (!out || !out_size) return false;
    out[0] = 0;

    char root[OUTPUT_BASE_BYTES];
    if (!file_make_output_base(root, sizeof(root), title)) return false;

    unsigned start = 0;
    if (strcmp(root, s_last_root) == 0) start = s_next_suffix;
    for (unsigned suffix = start; suffix < 1000u; suffix++) {
        char candidate[OUTPUT_BASE_BYTES];
        if (!add_output_suffix(candidate, sizeof(candidate), root, suffix)) continue;
        if (output_base_has_collision(candidate)) continue;
        if (!file_output_base_writable(candidate)) continue;
        strncpy(s_last_root, root, sizeof(s_last_root) - 1u);
        s_last_root[sizeof(s_last_root) - 1u] = 0;
        s_next_suffix = suffix + 1u;
        strncpy(out, candidate, out_size - 1u);
        out[out_size - 1u] = 0;
        return true;
    }
    return false;
}

/**
 * @brief Checks whether a candidate output base can be probed on disk.
 */
bool file_output_base_writable(const char *base_name) {
    if (!file_base_name_is_safe(base_name)) return false;
    if (output_base_has_collision(base_name)) return false;
    char path[256];
    char ext[32];
    static unsigned probe_counter;
    for (unsigned attempt = 0; attempt < 16u; attempt++) {
        unsigned counter = ++probe_counter;
        int n = snprintf(ext, sizeof(ext), ".probe%08lx%04x.tmp",
                         (unsigned long)time(NULL), counter & 0xffffu);
        if (n < 0 || (size_t)n >= sizeof(ext)) return false;
        if (!path_make_output_file(path, sizeof(path), base_name, ext)) return false;
        if (!file_exists(path)) break;
        path[0] = 0;
    }
    if (!path[0]) return false;
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fputc(0, f) != EOF;
    ok = fclose(f) == 0 && ok;
    (void)atomic_file_remove_path(path);
    return ok;
}
