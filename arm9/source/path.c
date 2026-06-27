/**
 * @file path.c
 * @brief Validates and constructs safe output paths inside the dump directory.
 */
#include "path.h"
#include "config.h"

#include <stdio.h>
#include <string.h>

/**
 * @brief Compares two ASCII strings case-insensitively for a fixed length.
 */
static bool ascii_ci_equal_n(const char *a, size_t a_len, const char *b) {
    size_t b_len = strlen(b);
    if (a_len != b_len) return false;
    for (size_t i = 0; i < a_len; i++) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 'a' + 'A');
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 'a' + 'A');
        if (ca != cb) return false;
    }
    return true;
}

/**
 * @brief Appends formatted text to a bounded path buffer.
 */
static bool write_checked(char *dst, size_t dst_size, const char *fmt,
                          const char *a, const char *b, const char *c) {
    if (!dst || dst_size == 0) return false;
    dst[0] = 0;
    int n = snprintf(dst, dst_size, fmt, a, b, c);
    if (n < 0 || (size_t)n >= dst_size) {
        dst[0] = 0;
        return false;
    }
    return true;
}

/**
 * @brief Returns whether a character is forbidden in output filenames.
 */
bool path_filename_char_is_forbidden(unsigned char c) {
    switch (c) {
        case '*':
        case '"':
        case '/':
        case '\\':
        case '<':
        case '>':
        case ':':
        case '|':
        case '?':
            return true;
        default:
            return c < 0x20u;
    }
}

/** @brief Checks FAT/Windows device names before optional extension suffixes. */
static bool base_is_reserved_device_name(const char *base, size_t len) {
    static const char *const reserved_device_names[] = { "CON", "PRN", "AUX", "NUL" };
    size_t stem_len = 0;
    while (stem_len < len && base[stem_len] != '.') stem_len++;
    if (!stem_len) return false;
    for (unsigned i = 0; i < sizeof(reserved_device_names) / sizeof(reserved_device_names[0]); i++) {
        if (ascii_ci_equal_n(base, stem_len, reserved_device_names[i])) return true;
    }
    if (stem_len == 4 &&
        (base[0] == 'C' || base[0] == 'c') &&
        (base[1] == 'O' || base[1] == 'o') &&
        (base[2] == 'M' || base[2] == 'm') &&
        base[3] >= '1' && base[3] <= '9') {
        return true;
    }
    if (stem_len == 4 &&
        (base[0] == 'L' || base[0] == 'l') &&
        (base[1] == 'P' || base[1] == 'p') &&
        (base[2] == 'T' || base[2] == 't') &&
        base[3] >= '1' && base[3] <= '9') {
        return true;
    }
    return false;
}

/**
 * @brief Joins a directory and leaf name into a bounded path buffer.
 */
bool path_join(char *dst, size_t dst_size, const char *dir, const char *name) {
    if (!dst || dst_size == 0) return false;
    dst[0] = 0;
    if (!dir || !name || !name[0]) return false;

    size_t dir_len = strlen(dir);
    if (dir_len && (dir[dir_len - 1] == '/' || dir[dir_len - 1] == '\\')) {
        return write_checked(dst, dst_size, "%s%s%s", dir, name, "");
    }
    return write_checked(dst, dst_size, "%s/%s%s", dir, name, "");
}

/**
 * @brief Replaces a path extension in a bounded path buffer.
 */
bool path_replace_ext(char *dst, size_t dst_size, const char *path, const char *new_ext) {
    if (!dst || dst_size == 0) return false;
    dst[0] = 0;
    if (!path || !path[0] || !new_ext) return false;

    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    if (!slash || (backslash && backslash > slash)) slash = backslash;

    const char *base = slash ? slash + 1 : path;
    const char *dot = strrchr(base, '.');
    size_t stem_len = dot ? (size_t)(dot - path) : strlen(path);
    const char *sep = new_ext[0] == '.' || new_ext[0] == 0 ? "" : ".";

    if (stem_len >= dst_size) return false;
    memcpy(dst, path, stem_len);
    dst[stem_len] = 0;

    size_t used = stem_len;
    int n = snprintf(dst + used, dst_size - used, "%s%s", sep, new_ext);
    if (n < 0 || (size_t)n >= dst_size - used) {
        dst[0] = 0;
        return false;
    }
    return true;
}

/** @brief Rejects output base names that could escape or confuse the dump directory. */
bool path_output_base_is_safe(const char *base) {
    if (!base || !base[0]) return false;
    size_t len = strlen(base);
    if (base[len - 1u] == ' ' || base[len - 1u] == '.') return false;
    if (base[0] == '/' || base[0] == '\\') return false;
    if (len >= 2u && base[1] == ':') return false;
    if (strstr(base, "..")) return false;
    if (base_is_reserved_device_name(base, len)) return false;
    for (size_t i = 0; i < len; i++) {
        if (path_filename_char_is_forbidden((unsigned char)base[i])) return false;
    }
    return true;
}

/**
 * @brief Builds a final output path for a base name and extension.
 */
bool path_make_output_file(char *dst, size_t dst_size, const char *base, const char *ext) {
    if (!dst || dst_size == 0) return false;
    dst[0] = 0;
    if (!base || !base[0] || !ext) return false;
    if (!path_output_base_is_safe(base)) return false;

    char name[256];
    const char *sep = ext[0] == '.' || ext[0] == 0 ? "" : ".";
    int n = snprintf(name, sizeof(name), "%s%s%s", base, sep, ext);
    if (n < 0 || (size_t)n >= sizeof(name)) return false;
    return path_join(dst, dst_size, OUTPUT_DIR, name);
}

