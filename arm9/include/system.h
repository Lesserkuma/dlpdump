#pragma once
#include "state.h"

/** @brief Reads the firmware language code used by the running system. */
u8 system_language_code(void);

/** @brief Returns a stable English label for the firmware language code. */
const char *system_language_name(void);
