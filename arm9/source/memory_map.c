/**
 * @file memory_map.c
 * @brief Validates DS Download Play section ranges against allowed memory maps.
 */
#include "memory_map.h"

/**
 * @brief Adds two 32-bit address values and reports wraparound.
 *
 * @param a First address/size term.
 * @param b Second address/size term.
 * @param out Receives the wrapped C addition result when non-NULL.
 * @return true when the sum did not overflow the 32-bit address space.
 */
bool memory_map_add_u32(u32 a, u32 b, u32 *out) {
    if (!out) return false;
    *out = a + b;
    return *out >= a;
}

/**
 * @brief Checks whether a non-empty load range is fully inside a memory window.
 *
 * @param start Inclusive start address.
 * @param size Byte size. Zero-size sections are rejected.
 * @param lo Inclusive window start.
 * @param hi Exclusive window end.
 * @return true only when `[start, start + size)` fits without overflow.
 */
bool memory_map_range_inside(u32 start, u32 size, u32 lo, u32 hi) {
    u32 end;
    if (!size || !memory_map_add_u32(start, size, &end)) return false;
    return start >= lo && end <= hi;
}

/**
 * @brief Validates one RSA section load range against DS Download Play memory.
 *
 * Section 0 must be the fixed header copy, section 1 must fit ARM9 main memory,
 * and section 2 may use ARM7 WRAM or ARM7 main memory. ARM7 main memory is
 * split at `MEMORY_ARM7_HIGH_REGION_START`; crossing the split is rejected
 * because the DS Download Play handover only tolerates the small high region as
 * a separate target.
 */
bool memory_map_validate_download_section(unsigned section_index, u32 load_addr, u32 size) {
    switch (section_index) {
        case 0:
            return memory_map_range_inside(load_addr, size,
                                              MEMORY_FIXED_HEADER_START,
                                              MEMORY_FIXED_HEADER_END);
        case 1:
            return memory_map_range_inside(load_addr, size,
                                              MEMORY_ARM9_MAIN_START,
                                              MEMORY_ARM9_MAIN_END);
        case 2:
            if (memory_map_range_inside(load_addr, size,
                                           MEMORY_ARM7_WRAM_START,
                                           MEMORY_ARM7_WRAM_END)) {
                return true;
            }
            if (!memory_map_range_inside(load_addr, size,
                                            MEMORY_ARM7_MAIN_START,
                                            MEMORY_ARM7_MAIN_END)) {
                return false;
            }
            if (load_addr < MEMORY_ARM7_HIGH_REGION_START &&
                load_addr + size > MEMORY_ARM7_HIGH_REGION_START) {
                return false;
            }
            if (load_addr >= MEMORY_ARM7_HIGH_REGION_START &&
                size > MEMORY_ARM7_HIGH_REGION_MAX_BYTES) {
                return false;
            }
            return true;
        default:
            return false;
    }
}

