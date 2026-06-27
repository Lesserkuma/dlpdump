#pragma once

#include "../../common/types.h"

/**
 * @file memory_map.h
 * @brief Named Nintendo DS memory ranges used by DS Download Play boot validation.
 */

#define MEMORY_FIXED_HEADER_START          0x027ffe00u
#define MEMORY_FIXED_HEADER_END            0x027fff60u
#define MEMORY_ARM9_MAIN_START             0x02000000u
#define MEMORY_ARM9_MAIN_END               0x022c0000u
#define MEMORY_ARM7_WRAM_START             0x037f8000u
#define MEMORY_ARM7_WRAM_END               0x0380f000u
#define MEMORY_ARM7_MAIN_START             0x02000000u
#define MEMORY_ARM7_MAIN_END               0x023fe800u
#define MEMORY_ARM7_HIGH_REGION_START      0x02300000u
#define MEMORY_ARM7_HIGH_REGION_MAX_BYTES  0x00040000u

/**
 * @brief Adds two u32 values and reports overflow.
 *
 * @param a First addend.
 * @param b Second addend.
 * @param out Receives the sum.
 * @return true when the sum did not overflow.
 */
bool memory_map_add_u32(u32 a, u32 b, u32 *out);

/**
 * @brief Checks whether a non-empty range is fully inside a memory range.
 *
 * @param start Inclusive start address.
 * @param size Range length in bytes.
 * @param lo Inclusive lower bound.
 * @param hi Exclusive upper bound.
 * @return true when the range is non-empty, non-overflowing and contained.
 */
bool memory_map_range_inside(u32 start, u32 size, u32 lo, u32 hi);

/**
 * @brief Validates one RSA-controlled DS Download Play section load range.
 *
 * Section 0 is the fixed header area, section 1 is ARM9 main memory, and
 * section 2 is either ARM7 WRAM or the constrained ARM7-visible main RAM area.
 *
 * @param section_index RSA section index 0..2.
 * @param load_addr Runtime load address.
 * @param size Section size in bytes.
 * @return true when the section is valid for the DS Download Play boot contract.
 */
bool memory_map_validate_download_section(unsigned section_index, u32 load_addr, u32 size);

