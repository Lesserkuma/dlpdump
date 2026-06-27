#pragma once

#include "types.h"

/**
 * @brief Rounds a u32 value up to the next multiple of a power-of-two alignment.
 *
 * `a` must be non-zero and a power of two. Callers validate overflow where the
 * rounded result is used for memory or file ranges.
 */
static inline u32 align_up_u32(u32 v, u32 a) {
    return (v + a - 1U) & ~(a - 1U);
}
