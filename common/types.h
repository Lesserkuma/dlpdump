#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define PACKED_ATTR __attribute__((packed))
#define ALIGNED_ATTR(x) __attribute__((aligned(x)))
#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

#include "endian.h"
#include "math.h"
