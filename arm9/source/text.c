/**
 * @file text.c
 * @brief Converts UTF-8 and UTF-16 text used by metadata and UI rendering.
 */
#include "text.h"

#include <string.h>

/**
 * @brief Returns whether a byte is a UTF-8 continuation byte.
 */
bool text_is_utf8_continuation(unsigned char c) {
    return (c & 0xc0u) == 0x80u;
}

/**
 * @brief Returns the expected byte length of a UTF-8 sequence lead byte.
 */
unsigned text_utf8_sequence_len(unsigned char c) {
    if (c < 0x80u) return 1;
    if ((c & 0xe0u) == 0xc0u) return 2;
    if ((c & 0xf0u) == 0xe0u) return 3;
    if ((c & 0xf8u) == 0xf0u) return 4;
    return 0;
}

/**
 * @brief Trims a mutable string so it does not end inside a UTF-8 sequence.
 */
void text_trim_incomplete_utf8_tail(char *s) {
    if (!s) return;
    size_t len = strlen(s);
    if (!len) return;

    size_t start = len - 1;
    while (start && text_is_utf8_continuation((unsigned char)s[start])) start--;

    unsigned need = text_utf8_sequence_len((unsigned char)s[start]);
    if (!need || start + need > len) s[start] = 0;
}

/**
 * @brief Appends one Unicode codepoint to a bounded UTF-8 string builder.
 */
bool text_append_codepoint(char *dst, size_t dst_size, size_t *out, uint32_t codepoint) {
    if (!dst || !dst_size || !out) return false;
    char tmp[4];
    int n = text_codepoint_to_utf8(codepoint, tmp);
    if (n <= 0 || *out + (size_t)n >= dst_size) return false;
    memcpy(dst + *out, tmp, (size_t)n);
    *out += (size_t)n;
    return true;
}

/**
 * @brief Decodes one UTF-8 codepoint and advances the input pointer.
 */
int text_utf8_next(const char **s, uint32_t *codepoint) {
    if (!s || !*s || !**s) return 0;
    const unsigned char *p = (const unsigned char *)*s;
    uint32_t cp = *p++;
    if (cp < 0x80u) {
        *s = (const char *)p;
        if (codepoint) {
            *codepoint = cp;
        }
        return 1;
    }

    unsigned need = 0;
    uint32_t min_cp = 0;
    if ((cp & 0xe0u) == 0xc0u) {
        cp &= 0x1fu;
        need = 1;
        min_cp = 0x80u;
    } else if ((cp & 0xf0u) == 0xe0u) {
        cp &= 0x0fu;
        need = 2;
        min_cp = 0x800u;
    } else if ((cp & 0xf8u) == 0xf0u) {
        cp &= 0x07u;
        need = 3;
        min_cp = 0x10000u;
    } else {
        *s = (const char *)p;
        if (codepoint) {
            *codepoint = TEXT_REPLACEMENT_CODEPOINT;
        }
        return 1;
    }

    for (unsigned i = 0; i < need; i++) {
        unsigned char c = *p;
        if ((c & 0xc0u) != 0x80u) {
            *s = (const char *)p;
            if (codepoint) {
                *codepoint = TEXT_REPLACEMENT_CODEPOINT;
            }
            return 1;
        }
        cp = (cp << 6) | (uint32_t)(c & 0x3fu);
        p++;
    }
    if (cp < min_cp || cp > 0x10ffffu || (cp >= 0xd800u && cp <= 0xdfffu)) {
        cp = TEXT_REPLACEMENT_CODEPOINT;
    }
    *s = (const char *)p;
    if (codepoint) {
        *codepoint = cp;
    }
    return 1;
}

/**
 * @brief Encodes one Unicode codepoint into a UTF-8 output buffer.
 */
int text_codepoint_to_utf8(uint32_t codepoint, char *out) {
    if (!out) return 0;
    if (codepoint >= 0xd800u && codepoint <= 0xdfffu) codepoint = TEXT_REPLACEMENT_CODEPOINT;
    if (codepoint > 0x10ffffu) codepoint = TEXT_REPLACEMENT_CODEPOINT;

    if (codepoint <= 0x7fu) {
        out[0] = (char)codepoint;
        return 1;
    }
    if (codepoint <= 0x7ffu) {
        out[0] = (char)(0xc0u | (codepoint >> 6));
        out[1] = (char)(0x80u | (codepoint & 0x3fu));
        return 2;
    }
    if (codepoint <= 0xffffu) {
        out[0] = (char)(0xe0u | (codepoint >> 12));
        out[1] = (char)(0x80u | ((codepoint >> 6) & 0x3fu));
        out[2] = (char)(0x80u | (codepoint & 0x3fu));
        return 3;
    }
    out[0] = (char)(0xf0u | (codepoint >> 18));
    out[1] = (char)(0x80u | ((codepoint >> 12) & 0x3fu));
    out[2] = (char)(0x80u | ((codepoint >> 6) & 0x3fu));
    out[3] = (char)(0x80u | (codepoint & 0x3fu));
    return 4;
}

/**
 * @brief Converts bounded UTF-16 text into UTF-8.
 */
size_t text_utf16_to_utf8(char *dst, size_t dst_cap, const uint16_t *src, size_t src_len) {
    size_t out_len = 0;
    if (!dst || !dst_cap) return 0;
    if (!src) {
        dst[0] = 0;
        return 0;
    }

    for (size_t i = 0; i < src_len; i++) {
        uint32_t cp = src[i];
        if (cp >= 0xd800u && cp <= 0xdbffu && i + 1 < src_len) {
            uint32_t lo = src[i + 1];
            if (lo >= 0xdc00u && lo <= 0xdfffu) {
                cp = 0x10000u + (((cp - 0xd800u) << 10) | (lo - 0xdc00u));
                i++;
            } else {
                cp = TEXT_REPLACEMENT_CODEPOINT;
            }
        } else if (cp >= 0xdc00u && cp <= 0xdfffu) {
            cp = TEXT_REPLACEMENT_CODEPOINT;
        }

        char tmp[4];
        int n = text_codepoint_to_utf8(cp, tmp);
        if (n <= 0 || out_len + (size_t)n >= dst_cap) {
            break;
        }
        memcpy(dst + out_len, tmp, (size_t)n);
        out_len += (size_t)n;
    }
    dst[out_len] = 0;
    return out_len;
}

/**
 * @brief Converts NUL-terminated UTF-16 text into UTF-8.
 */
size_t text_utf16z_to_utf8(char *dst, size_t dst_cap, const uint16_t *src) {
    size_t len = 0;
    if (src) {
        while (src[len]) len++;
    }
    return text_utf16_to_utf8(dst, dst_cap, src, len);
}

