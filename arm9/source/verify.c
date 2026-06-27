/**
 * @file verify.c
 * @brief Verifies downloaded DS Download Play sections against the signed RSA digest.
 */
#include "state.h"
#include "boot.h"
#include "debug.h"
#include "download.h"
#include "file.h"
#include "hash.h"
#include "ipc_arm9.h"
#include "meta.h"
#include "pcap.h"
#include "report.h"
#include "scan.h"
#include "system.h"
#include "ui.h"
#include "verify.h"
#include "download_rsa.h"

#include <string.h>

#define RSA_WORDS 32u
#define RSA_BYTES 128u
#define RSA_SHA1_DIGEST_BYTES 20u
#define RSA_SECTION_DIGEST_BYTES 64u

static u8 s_dlp_rsa_public_key_be[RSA_BYTES];
static bool s_dlp_rsa_public_key_loaded;

/**
 * @brief Clears the in-memory RSA public key and disables signature verification.
 */
static void clear_public_key(void) {
    memset(s_dlp_rsa_public_key_be, 0, sizeof(s_dlp_rsa_public_key_be));
    s_dlp_rsa_public_key_loaded = false;
}

/**
 * @brief Loads the external RSA public-key modulus from the DLDI file system.
 */
bool verify_load_public_key(void) {
    clear_public_key();

    FILE *f = fopen(RSA_PUBLIC_KEY_PATH, "rb");
    if (!f) return false;

    u8 key[RSA_BYTES];
    size_t read = fread(key, 1, sizeof(key), f);
    int extra = fgetc(f);
    bool ok = read == sizeof(key) && extra == EOF && ferror(f) == 0;
    if (fclose(f) != 0) ok = false;

    if (!ok) return false;
    memcpy(s_dlp_rsa_public_key_be, key, sizeof(s_dlp_rsa_public_key_be));
    s_dlp_rsa_public_key_loaded = true;
    return true;
}

/**
 * @brief Reports whether the RSA public-key modulus has been loaded.
 */
bool verify_public_key_loaded(void) {
    return s_dlp_rsa_public_key_loaded;
}

/**
 * @brief Converts big-endian RSA bytes into little-endian 32-bit limbs.
 *
 * The least significant limb is encoded by bytes `[127..124]` of the incoming
 * signature/modulus block. Keeping that byte order explicit is critical for
 * the repeated-squaring modular arithmetic below.
 */
static void be_to_words(const u8 be[RSA_BYTES], u32 words[RSA_WORDS]) {
    for (unsigned i = 0; i < RSA_WORDS; i++) {
        unsigned off = RSA_BYTES - 1u - i * 4u;
        words[i] = ((u32)be[off]) |
                   ((u32)be[off - 1u] << 8) |
                   ((u32)be[off - 2u] << 16) |
                   ((u32)be[off - 3u] << 24);
    }
}

/**
 * @brief Converts little-endian RSA limbs back into a big-endian byte block.
 *
 * This is the inverse of `be_to_words`: limb zero writes the final four bytes
 * of the decrypted PKCS#1 block in most-significant-byte-first order.
 */
static void words_to_be(const u32 words[RSA_WORDS], u8 be[RSA_BYTES]) {
    for (unsigned i = 0; i < RSA_WORDS; i++) {
        u32 v = words[i];
        unsigned off = RSA_BYTES - 1u - i * 4u;
        be[off] = (u8)v;
        be[off - 1u] = (u8)(v >> 8);
        be[off - 2u] = (u8)(v >> 16);
        be[off - 3u] = (u8)(v >> 24);
    }
}

/** @brief Compares a one-word-extended remainder with the RSA modulus. */
static int remainder_cmp_mod(const u32 rem[RSA_WORDS + 1u],
                             const u32 mod[RSA_WORDS]) {
    if (rem[RSA_WORDS]) return 1;
    for (int i = (int)RSA_WORDS - 1; i >= 0; i--) {
        if (rem[i] == mod[i]) continue;
        return rem[i] > mod[i] ? 1 : -1;
    }
    return 0;
}

/** @brief Subtracts the modulus from a one-word-extended remainder. */
static void remainder_sub_mod(u32 rem[RSA_WORDS + 1u],
                              const u32 mod[RSA_WORDS]) {
    u64 borrow = 0;
    for (unsigned i = 0; i < RSA_WORDS; i++) {
        u64 subtrahend = (u64)mod[i] + borrow;
        if (rem[i] < subtrahend) {
            rem[i] = (u32)((1ULL << 32) + rem[i] - subtrahend);
            borrow = 1;
        } else {
            rem[i] = (u32)(rem[i] - subtrahend);
            borrow = 0;
        }
    }
    rem[RSA_WORDS] -= (u32)borrow;
}

/** @brief Returns one bit from a 2048-bit product stored as little-endian words. */
static int wide_get_bit(const u32 wide[RSA_WORDS * 2u], unsigned bit) {
    return (wide[bit >> 5] >> (bit & 31u)) & 1u;
}

/** @brief Reduces a 2048-bit product modulo the 1024-bit DS Download Play RSA modulus. */
static void reduce_wide_mod(const u32 wide[RSA_WORDS * 2u],
                            const u32 mod[RSA_WORDS],
                            u32 out[RSA_WORDS]) {
    u32 rem[RSA_WORDS + 1u];
    memset(rem, 0, sizeof(rem));

    for (int bit = (int)(RSA_WORDS * 64u) - 1; bit >= 0; bit--) {
        u32 carry = (u32)wide_get_bit(wide, (unsigned)bit);
        for (unsigned i = 0; i < RSA_WORDS + 1u; i++) {
            u32 next_carry = rem[i] >> 31;
            rem[i] = (rem[i] << 1) | carry;
            carry = next_carry;
        }
        if (remainder_cmp_mod(rem, mod) >= 0) {
            remainder_sub_mod(rem, mod);
        }
    }

    memcpy(out, rem, RSA_WORDS * sizeof(out[0]));
}

/** @brief Multiplies two 1024-bit values and reduces the product modulo RSA n. */
static void modmul(const u32 a[RSA_WORDS],
                   const u32 b[RSA_WORDS],
                   const u32 mod[RSA_WORDS],
                   u32 out[RSA_WORDS]) {
    u32 wide[RSA_WORDS * 2u];
    memset(wide, 0, sizeof(wide));

    for (unsigned i = 0; i < RSA_WORDS; i++) {
        u64 carry = 0;
        for (unsigned j = 0; j < RSA_WORDS; j++) {
            u64 cur = (u64)a[i] * (u64)b[j] + wide[i + j] + carry;
            wide[i + j] = (u32)cur;
            carry = cur >> 32;
        }

        unsigned k = i + RSA_WORDS;
        while (carry && k < RSA_WORDS * 2u) {
            u64 cur = (u64)wide[k] + carry;
            wide[k] = (u32)cur;
            carry = cur >> 32;
            k++;
        }
    }

    reduce_wide_mod(wide, mod, out);
}

/** @brief Computes `sig^65537 mod n` with repeated squaring. */
static void rsa_public_decrypt(const u8 sig[RSA_BYTES], u8 out[RSA_BYTES]) {
    u32 mod[RSA_WORDS];
    u32 base[RSA_WORDS];
    u32 result[RSA_WORDS];
    u32 tmp[RSA_WORDS];

    be_to_words(s_dlp_rsa_public_key_be, mod);
    be_to_words(sig, base);
    memcpy(result, base, sizeof(result));

    for (unsigned i = 0; i < 16u; i++) {
        modmul(result, result, mod, tmp);
        memcpy(result, tmp, sizeof(result));
    }

    modmul(result, base, mod, tmp);
    words_to_be(tmp, out);
}

/** @brief Verifies the fixed PKCS#1 v1.5 SHA-1 DigestInfo wrapper. */
static bool pkcs1_sha1_digest_matches(const u8 dec[RSA_BYTES],
                                      const u8 digest[RSA_SHA1_DIGEST_BYTES]) {
    static const u8 sha1_digest_info_prefix[15] = {
        0x30, 0x21, 0x30, 0x09, 0x06,
        0x05, 0x2b, 0x0e, 0x03, 0x02,
        0x1a, 0x05, 0x00, 0x04, 0x14,
    };

    if (dec[0] != 0 || dec[1] != 1) return false;
    for (unsigned i = 2u; i < 92u; i++) {
        if (dec[i] != 0xff) return false;
    }
    if (dec[92] != 0) return false;
    if (memcmp(dec + 93u, sha1_digest_info_prefix, sizeof(sha1_digest_info_prefix)) != 0) return false;
    return memcmp(dec + 108u, digest, RSA_SHA1_DIGEST_BYTES) == 0;
}

/** @brief Verifies an RSA signature against an already-computed SHA-1 digest. */
static bool rsa_verify_digest(const u8 sig[RSA_BYTES],
                              const u8 digest[RSA_SHA1_DIGEST_BYTES]) {
    u8 decrypted[RSA_BYTES];
    rsa_public_decrypt(sig, decrypted);
    return pkcs1_sha1_digest_matches(decrypted, digest);
}

/** @brief Hashes one downloaded section into the section-digest block. */
static bool hash_section_digest(const Section *section, u8 *out_digest) {
    if (!section || !section->data || !out_digest) return false;

    Sha1Ctx sha1;
    sha1_init(&sha1);
    sha1_update(&sha1, section->data, section->size);
    sha1_final(&sha1, out_digest);
    return true;
}

/** @brief Computes the final signed digest from section hashes and seed. */
static bool compute_signed_digest(const DownloadRsaFrame *rsa,
                                  const Section sec[3],
                                  u8 digest[RSA_SHA1_DIGEST_BYTES]) {
    u8 section_digest[RSA_SECTION_DIGEST_BYTES];
    memset(section_digest, 0, sizeof(section_digest));

    if (!rsa || !sec) return false;
    for (unsigned i = 0; i < 3u; i++) {
        if (!hash_section_digest(&sec[i], section_digest + i * RSA_SHA1_DIGEST_BYTES)) {
            return false;
        }
    }
    memcpy(section_digest + 60u, rsa->signature_seed, 4u);

    Sha1Ctx sha1;
    sha1_init(&sha1);
    sha1_update(&sha1, section_digest, sizeof(section_digest));
    sha1_final(&sha1, digest);
    return true;
}

/** @brief Checks RSA control-frame invariants that must hold before hashing. */
static bool download_rsa_fields_sane(const Download *dl) {
    u16 total_packets = 0;
    return dl && dl->have_rsa &&
           download_rsa_frame_valid(&dl->rsa, dl->parent_packet_max_bytes,
                                       &total_packets) &&
           total_packets == dl->total_packets;
}

/**
 * @brief Verifies the downloaded sections against the signed DS Download Play digest.
 *
 * The verifier hashes each downloaded section, combines the three SHA-1 digests
 * with the RSA seed from the control block, decrypts the parent signature with
 * the externally loaded DS Download Play public key, and checks the PKCS#1 v1.5
 * SHA-1 wrapper. It returns false for missing public-key data, missing RSA
 * state, malformed control flags, hashing failures, or signature mismatch.
 */
bool verify_download(const Download *dl) {
    u8 digest[RSA_SHA1_DIGEST_BYTES];

    if (!verify_public_key_loaded()) return false;
    if (!download_rsa_fields_sane(dl)) return false;
    if (!compute_signed_digest(&dl->rsa, dl->sec, digest)) return false;
    return rsa_verify_digest(dl->rsa.signature, digest);
}

/**
 * @brief Verifies loaded sections against a DS Download Play RSA signature.
 */
bool verify_sections(const DownloadRsaFrame *rsa, const Section sec[3]) {
    u8 digest[RSA_SHA1_DIGEST_BYTES];

    if (!verify_public_key_loaded()) return false;
    if (!download_rsa_control_struct_valid(rsa)) return false;
    if (!compute_signed_digest(rsa, sec, digest)) return false;
    return rsa_verify_digest(rsa->signature, digest);
}
