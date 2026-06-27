#pragma once

#include <stdbool.h>
#include <stddef.h>

/**
 * @file path.h
 * @brief Bounded path construction helpers for output files.
 */

/**
 * @brief Joins a directory path and file name with one slash.
 *
 * @param dst Destination buffer.
 * @param dst_size Size of dst in bytes, including the terminator.
 * @param dir Directory path. Must not be NULL.
 * @param name File name. Must not be NULL or empty.
 * @return true when the full path fits; false on invalid input or truncation.
 *
 * @post On failure, dst is an empty string when dst_size is non-zero.
 */
bool path_join(char *dst, size_t dst_size, const char *dir, const char *name);

/**
 * @brief Replaces the last filename extension in a path.
 *
 * @param dst Destination buffer.
 * @param dst_size Size of dst in bytes, including the terminator.
 * @param path Source path. Must not be NULL or empty.
 * @param new_ext New extension, with or without a leading dot.
 * @return true when the rewritten path fits; false otherwise.
 *
 * @post On failure, dst is an empty string when dst_size is non-zero.
 */
bool path_replace_ext(char *dst, size_t dst_size, const char *path, const char *new_ext);

/**
 * @brief Checks whether one byte is forbidden in a dump file name.
 *
 * The list matches the filename sanitizer and rejects FAT/Windows special
 * characters in addition to path separators.
 */
bool path_filename_char_is_forbidden(unsigned char c);

/**
 * @brief Validates an output base name before it is joined with OUTPUT_DIR.
 *
 * Rejects empty names, forbidden filename characters, traversal, absolute path
 * forms, names ending in a space or dot, and FAT/Windows device names.
 */
bool path_output_base_is_safe(const char *base);

/**
 * @brief Builds a path inside OUTPUT_DIR from a base name and extension.
 *
 * @param dst Destination buffer.
 * @param dst_size Size of dst in bytes, including the terminator.
 * @param base Output base name. Must not be NULL or empty.
 * @param ext Extension, with or without a leading dot.
 * @return true when the output path fits; false otherwise.
 *
 * @post On failure, dst is an empty string when dst_size is non-zero.
 */
bool path_make_output_file(char *dst, size_t dst_size, const char *base, const char *ext);
