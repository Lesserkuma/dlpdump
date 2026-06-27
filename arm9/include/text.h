#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @file text.h
 * @brief UTF-8 and UTF-16 conversion helpers shared by UI, metadata and fonts.
 */

#define TEXT_REPLACEMENT_CODEPOINT 0xfffdu

/**
 * @brief Returns whether a byte is a UTF-8 continuation byte.
 *
 * @param c Byte to classify.
 * @return true for bytes in the `10xxxxxx` continuation range.
 */
bool text_is_utf8_continuation(unsigned char c);

/**
 * @brief Returns the expected UTF-8 sequence length for a lead byte.
 *
 * @param c UTF-8 lead byte.
 * @return 1..4 for valid lead bytes, or 0 for invalid continuation/high bytes.
 */
unsigned text_utf8_sequence_len(unsigned char c);

/**
 * @brief Removes an incomplete UTF-8 sequence from the end of a mutable string.
 *
 * @param s NUL-terminated UTF-8 string to trim in place. NULL is ignored.
 */
void text_trim_incomplete_utf8_tail(char *s);

/**
 * @brief Appends one Unicode codepoint to a bounded UTF-8 string builder.
 *
 * @param dst Destination byte buffer.
 * @param dst_size Destination capacity including the terminator.
 * @param out Current write offset, advanced on success.
 * @param codepoint Unicode scalar value. Invalid values become U+FFFD.
 * @return true when the encoded bytes fit and were appended.
 */
bool text_append_codepoint(char *dst, size_t dst_size, size_t *out, uint32_t codepoint);

/**
 * @brief Reads one UTF-8 codepoint and advances the input pointer.
 *
 * Invalid sequences consume at least one byte and return U+FFFD so callers can
 * continue scanning bounded strings without looping forever.
 *
 * @param s Pointer to a NUL-terminated UTF-8 cursor.
 * @param codepoint Receives the decoded Unicode scalar value when non-NULL.
 * @return 1 when a codepoint was consumed; 0 at end of string or invalid input.
 */
int text_utf8_next(const char **s, uint32_t *codepoint);

/**
 * @brief Encodes one Unicode codepoint as UTF-8.
 *
 * @param codepoint Unicode scalar value. Invalid values become U+FFFD.
 * @param out Destination for up to four bytes. Must not be NULL.
 * @return Number of bytes written, or 0 on invalid output.
 */
int text_codepoint_to_utf8(uint32_t codepoint, char *out);

/**
 * @brief Converts a bounded UTF-16 string to UTF-8.
 *
 * @param dst Destination buffer.
 * @param dst_cap Destination size in bytes, including the terminator.
 * @param src UTF-16 input.
 * @param src_len Number of UTF-16 code units to read.
 * @return Number of UTF-8 bytes written, excluding the terminator.
 */
size_t text_utf16_to_utf8(char *dst, size_t dst_cap, const uint16_t *src, size_t src_len);

/**
 * @brief Converts a NUL-terminated UTF-16 string to UTF-8.
 *
 * @param dst Destination buffer.
 * @param dst_cap Destination size in bytes, including the terminator.
 * @param src NUL-terminated UTF-16 input.
 * @return Number of UTF-8 bytes written, excluding the terminator.
 */
size_t text_utf16z_to_utf8(char *dst, size_t dst_cap, const uint16_t *src);

