/**
 * @file system.c
 * @brief Reads firmware/system settings used by protocol replies and UI text.
 */
#include "state.h"
#include "boot.h"
#include "debug.h"
#include "download.h"
#include "file.h"
#include "ipc_arm9.h"
#include "meta.h"
#include "pcap.h"
#include "report.h"
#include "scan.h"
#include "system.h"
#include "ui.h"
#include "verify.h"

#define TWLCFG_RAM_BASE                  ((const volatile u8 *)0x02000400u)
#define TWLCFG_SELECTED_LANGUAGE_OFFSET  0x06u
#define SYSTEM_LANGUAGE_UNKNOWN          0xffu

/**
 * @brief Maps firmware language settings into DS Download Play language codes.
 */
static u8 system_language_from_personal_data(void) {
    const PERSONAL_DATA *pd = PersonalData;
    if (!pd) return SYSTEM_LANGUAGE_UNKNOWN;
    return (u8)(pd->language & 0x07u);
}

/**
 * @brief Returns the language code reported to parent titles.
 */
u8 system_language_code(void) {
    if (systemIsTwlMode()) {
        /*
         * DSi/TWL mode exposes a RAM copy of TWLCFGn.dat at 02000400h.
         * Byte 006h in that RAM copy is the selected system language.
         * This keeps extended languages such as Chinese (6) and Korean (7)
         * visible instead of falling back to the NDS compatibility field.
         */
        u8 language = TWLCFG_RAM_BASE[TWLCFG_SELECTED_LANGUAGE_OFFSET];
        if (language <= 7u) return language;
    }

    return system_language_from_personal_data();
}

/**
 * @brief Returns the report/UI label for the active firmware language.
 */
const char *system_language_name(void) {
    switch (system_language_code()) {
        case 0: return "Japanese";
        case 1: return "English";
        case 2: return "French";
        case 3: return "German";
        case 4: return "Italian";
        case 5: return "Spanish";
        case 6: return "Chinese";
        case 7: return "Korean";
        default: return "Unknown";
    }
}
