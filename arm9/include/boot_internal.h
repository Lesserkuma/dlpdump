#ifndef BOOT_INTERNAL_H
#define BOOT_INTERNAL_H

#include "state.h"
#include "boot.h"
#include "debug.h"
#include "download.h"
#include "file.h"
#include "ipc_arm9.h"
#include "memory_map.h"
#include "handover.h"
#include "rom_header.h"
#include "hw_regs.h"
#include "meta.h"
#include "path.h"
#include "pcap.h"
#include "report.h"
#include "scan.h"
#include "system.h"
#include "ui.h"
#include "verify.h"

#include <stdlib.h>
#include <string.h>

#define BOOT_HEADER_SECTION_SIZE HANDOVER_HEADER_SECTION_BYTES
#define BOOT_HANDOVER_SIZE       HANDOVER_BSS_SIZE
#define BOOT_WAIT_FRAMES         60u
#define BOOT_ARM7_COPY_WAIT_SPINS 2000000u
#define BOOT_RUNTIME_CONTROL_ADDR HANDOVER_RUNTIME_CONTROL_ADDR
#define BOOT_DOWNLOAD_PARAMETER_ADDR HANDOVER_DOWNLOAD_PARAMETER_ADDR
#define BOOT_DOWNLOAD_PARAMETER_SIZE HANDOVER_DOWNLOAD_PARAMETER_BYTES
#define BOOT_BCN_RESERVED_SIZE   HANDOVER_BCN_RESERVED_BYTES
#define BOOT_BCN_CONTEXT_SIZE    HANDOVER_BCN_CONTEXT_BYTES
#define BOOT_FIXED_HEADER_ADDR   HANDOVER_FIXED_HEADER_ADDR
#define BOOT_NTR_MAIN_MEM_ALIAS_MASK 0x003fffffu
#define BOOT_TWL_MAIN_MEM_ALIAS_LO   0x02400000u
#define BOOT_TWL_MAIN_MEM_ALIAS_HI   0x03000000u
#define BOOT_ITCM __attribute__((section(".itcm"), long_call, noinline, used))

/**
 * @brief Holds the parsed child image sections and saved boot handover block.
 */
typedef struct {
    DownloadRsaFrame control;
    DownloadRsaFrame copy_control;
    Section sec[3];
    bool handover_valid;
    u8 handover[BOOT_HANDOVER_SIZE];
} BootImage;

/**
 * @brief Arguments passed to ARM7 before the final boot-stub release.
 */
typedef struct {
    u32 switch_to_ntr;
    u32 game_code;
} BootPrepareArg;

/** @brief Frees heap-backed sections owned by a loaded boot image. */
void boot_image_free(BootImage *img);

/** @brief Reads the child game code from the fixed header section. */
u32 boot_game_code(const BootImage *img);

/** @brief Loads and validates the saved NDS image plus BCN handover sidecar. */
bool boot_load_nds_image(const char *path, const char *bcn_path, BootImage *img);

#endif
