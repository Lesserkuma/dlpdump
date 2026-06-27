#pragma once
#include "state.h"

/**
 * @brief Writes the human-readable dump report to an already opened stream.
 *
 * The caller owns the stream and handles flush/close/commit behavior.
 */
bool report_write_to_file(FILE *f, const Download *dl, const char *base_name,
                         const u8 *nds_header, u32 nds_header_size,
                         const FileDigest *nds_digest,
                         const FileDigest *bcn_digest);

/**
 * @brief Writes the human-readable dump report for a saved download.
 *
 * @param dl Completed download state used for beacon, RSA and protocol stats.
 * @param base_name Safe output base name without extension.
 * @param nds_header Validated NDS header bytes used for ROM fields.
 * @param nds_digest Digest record for the `.nds` file.
 * @param bcn_digest Digest record for the `.bcn` file.
 */
bool report_write(const Download *dl, const char *base_name,
                   const u8 *nds_header, u32 nds_header_size,
                   const FileDigest *nds_digest,
                   const FileDigest *bcn_digest);
