#pragma once

/**
 * @file hash_common.h
 * @brief Small shared non-cryptographic hash helpers used by IPC metadata.
 */

#include "types.h"

#define FNV1A32_BASIS 2166136261u
#define FNV1A32_PRIME 16777619u

/**
 * @brief Updates a 32-bit FNV-1a hash with a bounded byte span.
 *
 * @param hash Current FNV-1a accumulator value.
 * @param data Bytes to hash; NULL is treated as an empty span.
 * @param len Number of bytes at `data`.
 * @return Updated accumulator.
 */
static inline u32 fnv1a32_update(u32 hash, const void *data, u32 len) {
    const u8 *p = (const u8*)data;
    if (!p) return hash;
    for (u32 i = 0; i < len; i++) {
        hash ^= p[i];
        hash *= FNV1A32_PRIME;
    }
    return hash;
}

/**
 * @brief Computes a 32-bit FNV-1a hash over a bounded byte span.
 *
 * @param data Bytes to hash; NULL is treated as an empty span.
 * @param len Number of bytes at `data`.
 * @return FNV-1a hash initialized with `FNV1A32_BASIS`.
 */
static inline u32 fnv1a32(const void *data, u32 len) {
    return fnv1a32_update(FNV1A32_BASIS, data, len);
}
