#pragma once
#include "state.h"
#include "../../common/beacon_ie.h"

/**
 * @brief Reassembles fixed beacon fragments into one metadata buffer.
 *
 * @param fixed Destination buffer of at least BEACON_FIXED_INFO_MAX bytes.
 * @param fixed_len Receives the number of assembled bytes.
 * @return true when all required fragments are present and structurally valid.
 */
bool meta_build_fixed_info(const ContentSlot *slot, u8 *fixed, unsigned *fixed_len);

/**
 * @brief Decodes host/title/description/icon fields from fixed beacon metadata.
 *
 * Text outputs are UTF-8 and always NUL-terminated when their size is non-zero.
 * `icon_valid` is cleared unless a full 32x32 icon is available.
 */
bool meta_decode_fixed_info(BeaconDataAttr attr, const u8 *fixed, unsigned fixed_len,
                           char *host, size_t host_size,
                           char *title, size_t title_size,
                           char *description, size_t description_size,
                           u16 *icon, bool *icon_valid);
