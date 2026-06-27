#pragma once

#include "types.h"

/** @brief Reads an unaligned little-endian 16-bit integer. */
static inline u16 le16(const void *p) {
    const u8 *b = (const u8*)p;
    return (u16)b[0] | ((u16)b[1] << 8);
}

/** @brief Reads an unaligned little-endian 32-bit integer. */
static inline u32 le32(const void *p) {
    const u8 *b = (const u8*)p;
    return (u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) | ((u32)b[3] << 24);
}

/** @brief Stores a 16-bit integer as unaligned little-endian bytes. */
static inline void stle16(void *p, u16 v) {
    u8 *b = (u8*)p;
    b[0] = (u8)v;
    b[1] = (u8)(v >> 8);
}

/** @brief Stores a 32-bit integer as unaligned little-endian bytes. */
static inline void stle32(void *p, u32 v) {
    u8 *b = (u8*)p;
    b[0] = (u8)v;
    b[1] = (u8)(v >> 8);
    b[2] = (u8)(v >> 16);
    b[3] = (u8)(v >> 24);
}
