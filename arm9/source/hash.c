/**
 * @file hash.c
 * @brief Implements streaming CRC32, MD5, SHA-1 and SHA-256 digests for dump reports.
 */
#include "hash.h"

#include <string.h>

#if defined(ARM9)
#include <calico/nds/system.h>

/**
 * @brief Initializes the DSi BIOS SHA-1 state used by TWL mode.
 */
static void dsi_bios_sha1_init(u32 state[25]) {
    memset(state, 0, 25u * sizeof(state[0]));
    register void *r0 __asm__("r0") = state;
    __asm__ volatile("swi 0x240000"
                     : "+r"(r0)
                     :
                     : "r1", "r2", "r3", "r12", "cc", "memory");
}

/**
 * @brief Feeds bytes into the DSi BIOS SHA-1 state.
 */
static void dsi_bios_sha1_update(u32 state[25], const void *data, u32 len) {
    register void *r0 __asm__("r0") = state;
    register const void *r1 __asm__("r1") = data;
    register u32 r2 __asm__("r2") = len;
    __asm__ volatile("swi 0x250000"
                     : "+r"(r0), "+r"(r1), "+r"(r2)
                     :
                     : "r3", "r12", "cc", "memory");
}

/**
 * @brief Finalizes the DSi BIOS SHA-1 state into a 20-byte digest.
 */
static void dsi_bios_sha1_final(u32 state[25], u8 out[20]) {
    register void *r0 __asm__("r0") = out;
    register void *r1 __asm__("r1") = state;
    __asm__ volatile("swi 0x260000"
                     : "+r"(r0), "+r"(r1)
                     :
                     : "r2", "r3", "r12", "cc", "memory");
}
#endif

/**
 * @brief Rotates a 32-bit word left for MD5 round operations.
 */
static u32 rol32(u32 v, unsigned n) {
    return (v << n) | (v >> (32u - n));
}

/**
 * @brief Compresses one 64-byte block into the MD5 state.
 */
static void md5_transform(Md5Ctx *ctx, const u8 block[64]) {
    static const u8 md5_shift_amounts[64] = {
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
    };
    static const u32 md5_round_constants[64] = {
        0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu,
        0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
        0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu,
        0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
        0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau,
        0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
        0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu,
        0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
        0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu,
        0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
        0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u,
        0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
        0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u,
        0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
        0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
        0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u,
    };
    u32 w[16];
    for (unsigned i = 0; i < 16; i++) w[i] = le32(block + i * 4);
    u32 a = ctx->h[0], b = ctx->h[1], c = ctx->h[2], d = ctx->h[3];
    for (unsigned i = 0; i < 64; i++) {
        u32 f, g;
        if (i < 16) {
            f = (b & c) | ((~b) & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | ((~d) & c);
            g = (5u * i + 1u) & 15u;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3u * i + 5u) & 15u;
        } else {
            f = c ^ (b | (~d));
            g = (7u * i) & 15u;
        }
        u32 tmp = d;
        d = c;
        c = b;
        b += rol32(a + f + md5_round_constants[i] + w[g], md5_shift_amounts[i]);
        a = tmp;
    }
    ctx->h[0] += a; ctx->h[1] += b; ctx->h[2] += c; ctx->h[3] += d;
}

/**
 * @brief Initializes an MD5 context.
 */
static void md5_init(Md5Ctx *ctx) {
    ctx->h[0] = 0x67452301u; ctx->h[1] = 0xefcdab89u;
    ctx->h[2] = 0x98badcfeu; ctx->h[3] = 0x10325476u;
    ctx->bytes = 0; ctx->used = 0;
}

/**
 * @brief Feeds bytes into an MD5 context.
 */
static void md5_update(Md5Ctx *ctx, const void *data, u32 len) {
    const u8 *p = (const u8*)data;
    ctx->bytes += len;
    while (len) {
        unsigned n = 64u - ctx->used;
        if (n > len) n = len;
        memcpy(ctx->buf + ctx->used, p, n);
        ctx->used += n;
        p += n;
        len -= n;
        if (ctx->used == 64) {
            md5_transform(ctx, ctx->buf);
            ctx->used = 0;
        }
    }
}

/**
 * @brief Finalizes an MD5 context and writes the digest.
 */
static void md5_final(Md5Ctx *ctx, u8 out[16]) {
    u64 bits = ctx->bytes * 8u;
    ctx->buf[ctx->used++] = 0x80;
    if (ctx->used > 56) {
        while (ctx->used < 64) ctx->buf[ctx->used++] = 0;
        md5_transform(ctx, ctx->buf);
        ctx->used = 0;
    }
    while (ctx->used < 56) ctx->buf[ctx->used++] = 0;
    for (unsigned i = 0; i < 8; i++) ctx->buf[ctx->used++] = (u8)(bits >> (i * 8));
    md5_transform(ctx, ctx->buf);
    for (unsigned i = 0; i < 4; i++) stle32(out + i * 4, ctx->h[i]);
}

/**
 * @brief Compresses one 64-byte block into the SHA-1 state.
 */
static void sha1_transform(Sha1Ctx *ctx, const u8 block[64]) {
    u32 w[80];
    for (unsigned i = 0; i < 16; i++) {
        w[i] = ((u32)block[i * 4] << 24) | ((u32)block[i * 4 + 1] << 16) |
               ((u32)block[i * 4 + 2] << 8) | block[i * 4 + 3];
    }
    for (unsigned i = 16; i < 80; i++) {
        u32 v = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
        w[i] = rol32(v, 1);
    }
    u32 a = ctx->h[0], b = ctx->h[1], c = ctx->h[2], d = ctx->h[3], e = ctx->h[4];
    for (unsigned i = 0; i < 80; i++) {
        u32 f, k;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5a827999u;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ed9eba1u;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8f1bbcdcu;
        } else {
            f = b ^ c ^ d;
            k = 0xca62c1d6u;
        }
        u32 t = rol32(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol32(b, 30); b = a; a = t;
    }
    ctx->h[0] += a; ctx->h[1] += b; ctx->h[2] += c; ctx->h[3] += d; ctx->h[4] += e;
}

/**
 * @brief Initializes a SHA-1 context with the software implementation.
 */
static void sha1_init_software(Sha1Ctx *ctx) {
    ctx->use_bios = false;
    memset(ctx->dsi_bios, 0, sizeof(ctx->dsi_bios));
    ctx->h[0] = 0x67452301u; ctx->h[1] = 0xefcdab89u; ctx->h[2] = 0x98badcfeu;
    ctx->h[3] = 0x10325476u; ctx->h[4] = 0xc3d2e1f0u;
    ctx->bytes = 0; ctx->used = 0;
}

/**
 * @brief Initializes a SHA-1 context with the DSi BIOS implementation.
 */
static bool sha1_init_dsi_bios(Sha1Ctx *ctx) {
    sha1_init_software(ctx);
#if defined(ARM9)
    if (systemIsTwlMode()) {
        ctx->use_bios = true;
        dsi_bios_sha1_init(ctx->dsi_bios);
        return true;
    }
#endif
    return false;
}

/**
 * @brief Initializes a SHA-1 context.
 */
void sha1_init(Sha1Ctx *ctx) {
    if (sha1_init_dsi_bios(ctx)) return;
}

/**
 * @brief Feeds bytes into a SHA-1 context.
 */
void sha1_update(Sha1Ctx *ctx, const void *data, u32 len) {
    if (!len) return;
#if defined(ARM9)
    if (ctx->use_bios) {
        dsi_bios_sha1_update(ctx->dsi_bios, data, len);
        return;
    }
#endif
    const u8 *p = (const u8*)data;
    ctx->bytes += len;
    while (len) {
        unsigned n = 64u - ctx->used;
        if (n > len) n = len;
        memcpy(ctx->buf + ctx->used, p, n);
        ctx->used += n;
        p += n;
        len -= n;
        if (ctx->used == 64) {
            sha1_transform(ctx, ctx->buf);
            ctx->used = 0;
        }
    }
}

/**
 * @brief Finalizes a SHA-1 context and writes the digest.
 */
void sha1_final(Sha1Ctx *ctx, u8 out[20]) {
#if defined(ARM9)
    if (ctx->use_bios) {
        dsi_bios_sha1_final(ctx->dsi_bios, out);
        return;
    }
#endif
    u64 bits = ctx->bytes * 8u;
    ctx->buf[ctx->used++] = 0x80;
    if (ctx->used > 56) {
        while (ctx->used < 64) ctx->buf[ctx->used++] = 0;
        sha1_transform(ctx, ctx->buf);
        ctx->used = 0;
    }
    while (ctx->used < 56) ctx->buf[ctx->used++] = 0;
    for (int i = 7; i >= 0; i--) ctx->buf[ctx->used++] = (u8)(bits >> (i * 8));
    sha1_transform(ctx, ctx->buf);
    for (unsigned i = 0; i < 5; i++) {
        out[i * 4] = (u8)(ctx->h[i] >> 24);
        out[i * 4 + 1] = (u8)(ctx->h[i] >> 16);
        out[i * 4 + 2] = (u8)(ctx->h[i] >> 8);
        out[i * 4 + 3] = (u8)ctx->h[i];
    }
}

/**
 * @brief Rotates a 32-bit word right for SHA-256 round operations.
 */
static u32 ror32(u32 v, unsigned n) {
    return (v >> n) | (v << (32u - n));
}

/**
 * @brief Compresses one 64-byte block into the SHA-256 state.
 */
static void sha256_transform(Sha256Ctx *ctx, const u8 block[64]) {
    static const u32 sha256_round_constants[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
        0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
        0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
        0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
        0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
    };
    u32 w[64];
    for (unsigned i = 0; i < 16; i++) {
        w[i] = ((u32)block[i * 4] << 24) | ((u32)block[i * 4 + 1] << 16) |
               ((u32)block[i * 4 + 2] << 8) | block[i * 4 + 3];
    }
    for (unsigned i = 16; i < 64; i++) {
        u32 s0 = ror32(w[i - 15], 7) ^ ror32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        u32 s1 = ror32(w[i - 2], 17) ^ ror32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    u32 a = ctx->h[0], b = ctx->h[1], c = ctx->h[2], d = ctx->h[3];
    u32 e = ctx->h[4], f = ctx->h[5], g = ctx->h[6], h = ctx->h[7];
    for (unsigned i = 0; i < 64; i++) {
        u32 s1 = ror32(e, 6) ^ ror32(e, 11) ^ ror32(e, 25);
        u32 ch = (e & f) ^ ((~e) & g);
        u32 t1 = h + s1 + ch + sha256_round_constants[i] + w[i];
        u32 s0 = ror32(a, 2) ^ ror32(a, 13) ^ ror32(a, 22);
        u32 maj = (a & b) ^ (a & c) ^ (b & c);
        u32 t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    ctx->h[0] += a; ctx->h[1] += b; ctx->h[2] += c; ctx->h[3] += d;
    ctx->h[4] += e; ctx->h[5] += f; ctx->h[6] += g; ctx->h[7] += h;
}

/**
 * @brief Initializes a SHA-256 context.
 */
static void sha256_init(Sha256Ctx *ctx) {
    ctx->h[0] = 0x6a09e667u; ctx->h[1] = 0xbb67ae85u; ctx->h[2] = 0x3c6ef372u; ctx->h[3] = 0xa54ff53au;
    ctx->h[4] = 0x510e527fu; ctx->h[5] = 0x9b05688cu; ctx->h[6] = 0x1f83d9abu; ctx->h[7] = 0x5be0cd19u;
    ctx->bytes = 0; ctx->used = 0;
}

/**
 * @brief Feeds bytes into a SHA-256 context.
 */
static void sha256_update(Sha256Ctx *ctx, const void *data, u32 len) {
    const u8 *p = (const u8*)data;
    ctx->bytes += len;
    while (len) {
        unsigned n = 64u - ctx->used;
        if (n > len) n = len;
        memcpy(ctx->buf + ctx->used, p, n);
        ctx->used += n;
        p += n;
        len -= n;
        if (ctx->used == 64) {
            sha256_transform(ctx, ctx->buf);
            ctx->used = 0;
        }
    }
}

/**
 * @brief Finalizes a SHA-256 context and writes the digest.
 */
static void sha256_final(Sha256Ctx *ctx, u8 out[32]) {
    u64 bits = ctx->bytes * 8u;
    ctx->buf[ctx->used++] = 0x80;
    if (ctx->used > 56) {
        while (ctx->used < 64) ctx->buf[ctx->used++] = 0;
        sha256_transform(ctx, ctx->buf);
        ctx->used = 0;
    }
    while (ctx->used < 56) ctx->buf[ctx->used++] = 0;
    for (int i = 7; i >= 0; i--) ctx->buf[ctx->used++] = (u8)(bits >> (i * 8));
    sha256_transform(ctx, ctx->buf);
    for (unsigned i = 0; i < 8; i++) {
        out[i * 4] = (u8)(ctx->h[i] >> 24);
        out[i * 4 + 1] = (u8)(ctx->h[i] >> 16);
        out[i * 4 + 2] = (u8)(ctx->h[i] >> 8);
        out[i * 4 + 3] = (u8)ctx->h[i];
    }
}

/**
 * @brief Initializes the selected output hash context.
 */
void hash_init(HashCtx *ctx) {
    ctx->crc32 = 0xffffffffu;
    ctx->bytes = 0;
    md5_init(&ctx->md5);
    sha1_init(&ctx->sha1);
    sha256_init(&ctx->sha256);
}

/**
 * @brief Feeds bytes into the selected output hash context.
 */
void hash_update(HashCtx *ctx, const void *data, u32 len) {
    if (!ctx || !data || !len) return;
    const u8 *p = (const u8*)data;
    for (u32 i = 0; i < len; i++) {
        ctx->crc32 ^= p[i];
        for (unsigned bit = 0; bit < 8; bit++) {
            ctx->crc32 = (ctx->crc32 >> 1) ^ (0xedb88320u & (0u - (ctx->crc32 & 1u)));
        }
    }
    ctx->bytes += len;
    md5_update(&ctx->md5, data, len);
    sha1_update(&ctx->sha1, data, len);
    sha256_update(&ctx->sha256, data, len);
}

/**
 * @brief Finalizes the selected output hash context.
 */
void hash_final(HashCtx *ctx, FileDigest *digest) {
    if (!ctx || !digest) return;
    digest->size = ctx->bytes;
    digest->crc32 = ctx->crc32 ^ 0xffffffffu;
    md5_final(&ctx->md5, digest->md5);
    sha1_final(&ctx->sha1, digest->sha1);
    sha256_final(&ctx->sha256, digest->sha256);
}
