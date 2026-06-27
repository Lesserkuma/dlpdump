#pragma once

#include "../../common/types.h"

/**
 * @file hash.h
 * @brief Streaming CRC32, MD5, SHA-1 and SHA-256 digest helpers.
 */

/**
 * @brief Final file digest set written to reports after saving output files.
 */
typedef struct {
    u32 size;
    u32 crc32;
    u8 md5[16];
    u8 sha1[20];
    u8 sha256[32];
} FileDigest;

/**
 * @brief Internal streaming state for MD5 hashing.
 */
typedef struct {
    u32 h[4];
    u64 bytes;
    u8 buf[64];
    unsigned used;
} Md5Ctx;

/**
 * @brief Internal streaming state for SHA-1 hashing.
 */
typedef struct {
    u32 h[5];
    u64 bytes;
    u8 buf[64];
    unsigned used;
    u32 dsi_bios[25];
    bool use_bios;
} Sha1Ctx;

/**
 * @brief Internal streaming state for SHA-256 hashing.
 */
typedef struct {
    u32 h[8];
    u64 bytes;
    u8 buf[64];
    unsigned used;
} Sha256Ctx;

/**
 * @brief Combined streaming hash context for all report digest algorithms.
 */
typedef struct {
    u32 crc32;
    Md5Ctx md5;
    Sha1Ctx sha1;
    Sha256Ctx sha256;
    u32 bytes;
} HashCtx;

/** @brief Initializes a streaming SHA-1 context. */
void sha1_init(Sha1Ctx *ctx);

/** @brief Adds bytes to a streaming SHA-1 context. */
void sha1_update(Sha1Ctx *ctx, const void *data, u32 len);

/** @brief Finalizes a SHA-1 digest into 20 big-endian bytes. */
void sha1_final(Sha1Ctx *ctx, u8 out[20]);

/**
 * @brief Initializes a streaming digest context.
 *
 * @param ctx Context to initialize. Must not be NULL.
 */
void hash_init(HashCtx *ctx);

/**
 * @brief Adds bytes to a streaming digest context.
 *
 * @param ctx Initialized context.
 * @param data Input bytes. Ignored when NULL or len is zero.
 * @param len Input byte count.
 */
void hash_update(HashCtx *ctx, const void *data, u32 len);

/**
 * @brief Finalizes all digests into a file-digest record.
 *
 * @param ctx Initialized context.
 * @param digest Output digest record.
 */
void hash_final(HashCtx *ctx, FileDigest *digest);
