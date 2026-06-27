#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/**
 * @file atomic_file.h
 * @brief Same-directory temporary-file and rename helpers for output artifacts.
 */

typedef bool (*AtomicFileWriter)(FILE *f, void *ctx);

/**
 * @brief Checks whether a path already exists as a readable file.
 *
 * @param path Path to probe. NULL returns false.
 * @return true when the path can be opened for reading.
 */
bool atomic_file_exists(const char *path);

/**
 * @brief Builds a unique same-directory temporary path next to a final path.
 *
 * @param out Destination buffer for the generated temporary path.
 * @param out_size Destination capacity in bytes.
 * @param final_path Final output path whose extension is replaced.
 * @param suffix Extension-like temporary suffix, for example `.tmp`.
 * @return true when a non-existing temporary path was produced.
 */
bool atomic_file_make_temp_path(char *out,
                                   size_t out_size,
                                   const char *final_path,
                                   const char *suffix);

/**
 * @brief Writes an output payload to a temporary file without committing it.
 *
 * The temporary file is flushed and closed before the function returns. On
 * write, flush or close failure the temporary path is removed and cleared.
 *
 * @param final_path Final path that must not already exist.
 * @param suffix Temporary suffix passed to `atomic_file_make_temp_path`.
 * @param tmp_path Receives the temporary path on success.
 * @param tmp_path_size Capacity of `tmp_path` in bytes.
 * @param writer Callback that writes the payload to the open file.
 * @param ctx Opaque callback context.
 * @return true when the temporary file is complete and ready to commit.
 */
bool atomic_file_write_temp(const char *final_path,
                               const char *suffix,
                               char *tmp_path,
                               size_t tmp_path_size,
                               AtomicFileWriter writer,
                               void *ctx);

/**
 * @brief Renames a completed temporary file to its final reserved path.
 *
 * @param tmp_path Existing temporary file to rename. It is removed on failure.
 * @param final_path Final path that must not already exist.
 * @return true when the rename completed successfully.
 */
bool atomic_file_commit_temp(const char *tmp_path, const char *final_path);

/**
 * @brief Removes a path used by an output transaction or cleanup probe.
 *
 * Missing files are treated as already cleaned up. The helper is the only
 * project-facing removal API so cleanup behavior stays centralized.
 *
 * @param path Path to remove. NULL or empty paths are accepted.
 * @return true when the path is absent after the call.
 */
bool atomic_file_remove_path(const char *path);

/**
 * @brief Removes a temporary path and clears its caller-owned buffer.
 *
 * @param tmp_path Mutable temporary path buffer. NULL is accepted.
 */
void atomic_file_discard_temp(char *tmp_path);

/**
 * @brief Writes, flushes, closes and commits a single output file atomically.
 *
 * @param final_path Final output path that must not already exist.
 * @param suffix Temporary suffix used for the same-directory temp path.
 * @param writer Callback that writes the payload.
 * @param ctx Opaque callback context.
 * @return true when the final file was committed.
 */
bool atomic_file_write(const char *final_path,
                          const char *suffix,
                          AtomicFileWriter writer,
                          void *ctx);
