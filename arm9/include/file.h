#pragma once
#include "state.h"

/**
 * @brief Ensures that the SD output directory exists and is a directory.
 *
 * @return true when the directory exists or was created.
 */
bool file_ensure_output_dir(void);

/**
 * @brief Finds the newest complete saved download set.
 *
 * A candidate must have a timestamped `.nds` file and a matching `.bcn`
 * sidecar. Output buffers may be NULL when that value is not needed.
 *
 * @return true if a complete set was found and all requested outputs fit.
 */
bool file_find_latest_download(char *path, size_t path_size,
                              char *base_name, size_t base_size,
                              char *title, size_t title_size);

/** @brief Rejects base names that are empty, absolute, traversal, or unsafe. */
bool file_base_name_is_safe(const char *base_name);

/** @brief Builds a timestamped safe output base from a decoded title. */
bool file_make_output_base(char *out, size_t out_size, const char *title);

/** @brief Builds a timestamp-only fallback base related to a preferred base. */
bool file_make_output_fallback_base(char *out, size_t out_size, const char *preferred_base);

/**
 * @brief Reserves a unique output base for a new dump set.
 *
 * The returned base is safe and has no existing `.nds`, `.bcn`, `.txt`,
 * `.pcap` or `.log` outputs. Repeated calls in the same process with the same
 * timestamp/title receive suffixed names.
 */
bool file_reserve_output_base(char *out, size_t out_size, const char *title);

/** @brief Probes whether a safe output base can be written in the dump directory. */
bool file_output_base_writable(const char *base_name);

/**
 * @brief Atomically saves `.nds` and `.bcn` files and writes a report when possible.
 *
 * @param dl Completed and verified download state with all three sections loaded.
 */
bool file_save_download(const Download *dl);
