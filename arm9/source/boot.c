/**
 * @file boot.c
 * @brief Coordinates the irreversible ARM9/ARM7 handoff into a saved child.
 *
 * The final cache maintenance, IPCSYNC ordering and TWL-to-NTR hardware
 * transitions use Pico-Loader-derived boot knowledge. Local code adds payload
 * validation, fixed memory preparation and DS Download Play UI integration.
 */
#include "boot_internal.h"

/**
 * @brief Checks whether two unsigned address spans intersect or overflow.
 *
 * Overflow is treated as overlap because an unsafe section map must not reach
 * the irreversible boot handover path.
 */
static bool span_overlap(u32 a, u32 asz, u32 b, u32 bsz) {
    u32 ae, be;
    if (!memory_map_add_u32(a, asz, &ae) || !memory_map_add_u32(b, bsz, &be)) return true;
    return a < be && b < ae;
}

/**
 * @brief Verifies that staged sections can be copied without clobbering inputs.
 *
 * @return true when at least one section can be safely copied at every step
 *         until all three header/ARM9/ARM7 sections are in place.
 */
static bool copy_order_possible(const DownloadRsaFrame *control) {
    bool done[3] = { false, false, false };
    unsigned remaining = 3;

    while (remaining) {
        bool found = false;
        for (unsigned i = 0; i < 3 && !found; i++) {
            if (done[i]) continue;
            const DownloadSectionEntry *e = &control->section[i];
            bool clobbers_source = false;
            for (unsigned j = 0; j < 3; j++) {
                if (i == j || done[j]) continue;
                const DownloadSectionEntry *other = &control->section[j];
                if (span_overlap(e->load_addr, e->size,
                                 other->staging_addr, other->size)) {
                    clobbers_source = true;
                    break;
                }
            }
            if (!clobbers_source) {
                done[i] = true;
                remaining--;
                found = true;
            }
        }
        if (!found) return false;
    }
    return true;
}

/**
 * @brief Publishes validated boot control blocks at fixed ARM7-visible addresses.
 *
 * The fixed memory contract is consumed by the ARM7 boot stub and by the ITCM
 * handover code after the filesystem/UI are no longer available.
 */
static void prepare_fixed_memory(const BootImage *img, const u8 handover[BOOT_HANDOVER_SIZE]) {
    memcpy((void*)HANDOVER_FIXED_CONTROL_ADDR, &img->control, sizeof(img->control));
    memcpy((void*)HANDOVER_RUNTIME_CONTROL_ADDR, &img->copy_control, sizeof(img->copy_control));
    memcpy((void*)HANDOVER_FIXED_BEACON_ADDR, handover, BOOT_HANDOVER_SIZE);
    *(volatile u32*)HANDOVER_ARM7_STATUS_ADDR = 0;
    DC_FlushAll();
}

/**
 * @brief Requests ARM7-side boot preparation and waits for a bounded reply.
 *
 * The ARM7 path may switch TWL hardware back toward NTR compatibility before
 * it acknowledges readiness.
 */
static bool wait_arm7_boot_ready(bool switch_to_ntr, u32 game_code) {
    BootPrepareArg arg = { switch_to_ntr ? 1u : 0u, game_code };
    g_arm7BootReady = false;
    ipc_send_command(ARM7_CMD_BOOT_PREPARE, &arg, sizeof(arg));
    for (unsigned i = 0; i < BOOT_WAIT_FRAMES; i++) {
        swiWaitForVBlank();
        ipc_poll();
        if (g_arm7BootReady) return true;
    }
    return false;
}

/**
 * @brief Waits until the ARM7 boot stub has copied the ARM7 payload section.
 *
 * @return true when the shared status word reaches `HANDOVER_ARM7_STATUS_COPIED`.
 */
static bool wait_arm7_sections_copied(void) {
    volatile u32 *status = (volatile u32*)HANDOVER_ARM7_STATUS_ADDR;
    for (unsigned i = 0; i < BOOT_ARM7_COPY_WAIT_SPINS; i++) {
        DC_InvalidateRange((void*)status, 32);
        if (*status == HANDOVER_ARM7_STATUS_COPIED) return true;
    }
    return false;
}

/*
 * ITCM handover helpers are declared together so the launch path can call them
 * before their definitions while the linker keeps each body in `.itcm`.
 */
static void itcm_memmove(void *dstv, const void *srcv, u32 len) BOOT_ITCM;
static u32 itcm_ntr_main_memory_alias(u32 addr) BOOT_ITCM;
static void itcm_copy_sections_to_actual(const DownloadRsaFrame *copy_control, bool switch_to_ntr) BOOT_ITCM;
static void itcm_write_handover_fields(const DownloadRsaFrame *control) BOOT_ITCM;

static void itcm_request_arm7_ntr_mode(void) BOOT_ITCM;
static void itcm_clear_twl_arm9_hardware(void) BOOT_ITCM;
static void itcm_set_arm9_clock_ntr(void) BOOT_ITCM;
static void itcm_prepare_arm9_ntr_mode(void) BOOT_ITCM;
static void itcm_finish_arm9_ntr_mode(void) BOOT_ITCM;

static void itcm_stop_arm9_hardware(void) BOOT_ITCM;
static void itcm_clear_pxi(void) BOOT_ITCM;
static void itcm_ipc_sync_set(u16 value) BOOT_ITCM;
static u16 itcm_ipc_sync_remote(void) BOOT_ITCM;

static void itcm_drain_write_buffer(void) BOOT_ITCM;
static void itcm_dcache_flush_all(void) BOOT_ITCM;
static void itcm_icache_invalidate_all(void) BOOT_ITCM;
static void itcm_disable_cache_mpu(void) BOOT_ITCM;

static void itcm_final_handover(const DownloadRsaFrame *copy_control, const DownloadRsaFrame *public_control, bool switch_to_ntr) BOOT_ITCM;

/**
 * @brief Copies bytes inside ITCM-safe code, including overlapping ranges.
 *
 * This local routine avoids library calls after cache/MMU shutdown has begun.
 */
static void itcm_memmove(void *dstv, const void *srcv, u32 len) {
    u8 *dst = (u8*)dstv;
    const u8 *src = (const u8*)srcv;
    if (dst == src || !len) return;
    if (dst < src || dst >= src + len) {
        while (len--) *dst++ = *src++;
    } else {
        dst += len;
        src += len;
        while (len--) *--dst = *--src;
    }
}

/**
 * @brief Maps TWL main-memory aliases back to the NTR-visible mirror.
 */
static u32 itcm_ntr_main_memory_alias(u32 addr) {
    if (addr >= BOOT_TWL_MAIN_MEM_ALIAS_LO && addr < BOOT_TWL_MAIN_MEM_ALIAS_HI) {
        return 0x02000000u | (addr & BOOT_NTR_MAIN_MEM_ALIAS_MASK);
    }
    return addr;
}

/**
 * @brief Copies staged header/ARM9/ARM7 sections to their launch addresses.
 *
 * When switching from TWL mode, sections staged in TWL aliases are mirrored to
 * the NTR-visible 0x02000000 range before the downloaded child starts.
 */
static void itcm_copy_sections_to_actual(const DownloadRsaFrame *copy_control, bool switch_to_ntr) {
    for (unsigned i = 0; i < 3; i++) {
        const DownloadSectionEntry *sec = &copy_control->section[i];
        if (!sec->size) continue;

        if (sec->staging_addr != sec->load_addr) {
            itcm_memmove((void*)sec->load_addr, (const void*)sec->staging_addr, sec->size);
        }

        if (switch_to_ntr) {
            u32 alias = itcm_ntr_main_memory_alias(sec->load_addr);
            if (alias != sec->load_addr) {
                itcm_memmove((void*)alias, (const void*)sec->staging_addr, sec->size);
            }
        }
    }
}

/**
 * @brief Writes entrypoints, beacon handover data and user parameters for ARM7.
 *
 * The target addresses are part of the Nintendo DS Download Play handover
 * contract consumed by the child after both CPUs leave the dumper.
 */
static void itcm_write_handover_fields(const DownloadRsaFrame *control) {
    *(volatile u32*)HANDOVER_ARM9_ENTRY_ADDR = control->arm9_entrypoint;
    *(volatile u32*)HANDOVER_ARM7_ENTRY_ADDR = control->arm7_entrypoint;
    *(volatile u32*)HANDOVER_PARENT_PARAM_MAGIC_ADDR = 0;
    *(volatile u16*)HANDOVER_BEACON_MODE_ADDR = HANDOVER_BEACON_MODE_DOWNLOAD_PLAY;

    const u8 *src = (const u8*)HANDOVER_FIXED_BEACON_ADDR;
    volatile u8 *dst = (volatile u8*)HANDOVER_BEACON_PAYLOAD_ADDR;
    for (unsigned i = 0; i < BOOT_HANDOVER_SIZE; i++) dst[i] = src[i];

    src = control->download_parameter;
    dst = (volatile u8*)HANDOVER_DOWNLOAD_PARAMETER_ADDR;
    for (unsigned i = 0; i < HANDOVER_DOWNLOAD_PARAMETER_BYTES; i++) dst[i] = src[i];
}

/**
 * @brief Requests the ARM7 boot stub to switch hardware state to NTR mode.
 *
 * The status-word handshake is Pico-Loader-derived boot knowledge adapted to
 * this project's fixed-memory contract.
 */
static void itcm_request_arm7_ntr_mode(void) {
    volatile u32 *status = (volatile u32*)HANDOVER_ARM7_STATUS_ADDR;
    *status = HANDOVER_ARM9_NTR_SWITCH;
    itcm_drain_write_buffer();
    while (*status != HANDOVER_ARM7_STATUS_NTR_READY) {
    }
    itcm_drain_write_buffer();
}

/**
 * @brief Clears TWL-only ARM9 registers before running an NTR child.
 *
 * These SCFG-side writes are part of the Pico-Loader-derived TWL-to-NTR
 * compatibility sequence; clearing happens from ITCM because normal runtime
 * services are about to be disabled.
 */
static void itcm_clear_twl_arm9_hardware(void) {
    for (volatile u32 *r = BOOT_REG_TWL_CLEAR_BEGIN; r <= BOOT_REG_TWL_CLEAR_END; r++) {
        *r = 0;
    }
}

/**
 * @brief Drops ARM9 clock speed to the NTR setting while IRQ/FIQ are masked.
 *
 * The register access and short delay are derived from Pico-Loader
 * `arm9Clock.s`, rewritten here as ITCM-resident C/inline assembly.
 */
static void itcm_set_arm9_clock_ntr(void) {
    u16 clk = BOOT_REG_SCFG_CLK;
    if ((clk & 1u) == 0) return;

    u32 old_cpsr;
    __asm__ volatile("mrs %0, cpsr" : "=r"(old_cpsr));
    u32 irq_fiq_disabled = old_cpsr | 0xc0u;
    __asm__ volatile("msr cpsr_c, %0" :: "r"(irq_fiq_disabled) : "memory");
    BOOT_REG_SCFG_CLK = (u16)(clk & (u16)~1u);
    for (u32 i = 0; i < 8u; i++) {
        __asm__ volatile("");
    }
    __asm__ volatile("msr cpsr_c, %0" :: "r"(old_cpsr) : "memory");
}

/**
 * @brief Applies ARM9-side TWL-to-NTR register changes before release.
 *
 * This prepares the ARM9 half of the Pico-Loader-derived compatibility path
 * after ARM7 has acknowledged that its own NTR setup is ready.
 */
static void itcm_prepare_arm9_ntr_mode(void) {
    itcm_clear_twl_arm9_hardware();
    itcm_set_arm9_clock_ntr();
    BOOT_REG_SCFG_MC = SCFG_MC_NTR_MODE;
    BOOT_REG_SCFG_EXT = SCFG_EXT_NTR_PREPARE;
    itcm_drain_write_buffer();
}

/**
 * @brief Finalizes NTR-mode SCFG state after the ARM7 sync handshake.
 *
 * The final SCFG mask mirrors Pico-Loader's NTR-mode handoff behavior while
 * keeping this loader's simpler ARM7/ARM9 status protocol.
 */
static void itcm_finish_arm9_ntr_mode(void) {
    if (((BOOT_REG_SCFG_EXT >> 14) & 3u) == 0) {
        BOOT_REG_SCFG_EXT &= ~SCFG_EXT_NTR_ARM9_MASK;
        itcm_drain_write_buffer();
    }
}

/**
 * @brief Disables ARM9 interrupts, DMA, timers and cartridge IRQ before jump.
 *
 * Side effects are intentionally global: after this point UI, FAT, PXI service
 * recovery and normal interrupt-driven code are no longer usable.
 */
static void itcm_stop_arm9_hardware(void) {
    BOOT_REG_IME = 0;
    BOOT_REG_IE = 0;
    BOOT_REG_IF = 0xffffffffu;
    BOOT_REG_DMA0_CR = 0;
    BOOT_REG_DMA1_CR = 0;
    BOOT_REG_DMA2_CR = 0;
    BOOT_REG_DMA3_CR = 0;
    BOOT_REG_TIMER0_CR = 0;
    BOOT_REG_TIMER1_CR = 0;
    BOOT_REG_TIMER2_CR = 0;
    BOOT_REG_TIMER3_CR = 0;
    BOOT_REG_EXMEMCNT &= ~EXMEMCNT_CART_IRQ_ENABLE;
}

/**
 * @brief Drains and disables PXI so the child starts with a clean FIFO.
 *
 * The final IPCSYNC handshake is Pico-Loader-derived boot coordination adapted
 * for this project's ARM7 boot stub.
 */
static void itcm_clear_pxi(void) {
    BOOT_REG_IPCFIFOCNT = IPCFIFOCNT_CLEAR_AND_DISABLE;
    while ((BOOT_REG_IPCFIFOCNT & IPCFIFOCNT_RECV_EMPTY) == 0) {
        (void)BOOT_REG_IPCFIFORECV;
    }
    BOOT_REG_IPCSYNC = 0;
}

/**
 * @brief Writes the low IPCSYNC nibble used by the final ARM7/ARM9 handshake.
 */
static void itcm_ipc_sync_set(u16 value) {
    BOOT_REG_IPCSYNC = value;
}

/**
 * @brief Reads the remote IPCSYNC nibble during the final handshake.
 */
static u16 itcm_ipc_sync_remote(void) {
    return BOOT_REG_IPCSYNC & IPCSYNC_REMOTE_MASK;
}

/**
 * @brief Emits CP15 c7/c10/4 so prior ARM9 stores reach memory before handoff.
 *
 * The cache-maintenance primitive is derived from Pico-Loader `cache.s`.
 */
static void itcm_drain_write_buffer(void) {
    u32 zero = 0;
    __asm__ volatile("mcr p15, 0, %0, c7, c10, 4" :: "r"(zero) : "memory");
}

/**
 * @brief Cleans and invalidates every ARM9 data-cache line before MPU disable.
 *
 * The line iteration and CP15 operation are derived from Pico-Loader `cache.s`.
 */
static void itcm_dcache_flush_all(void) {
    for (u32 way = 0; way < 4; way++) {
        u32 way_bits = way << 30;
        for (u32 index = 0; index < 0x400; index += 0x20) {
            u32 line = way_bits | index;
            __asm__ volatile("mcr p15, 0, %0, c7, c14, 2" :: "r"(line) : "memory");
        }
    }
    itcm_drain_write_buffer();
}

/**
 * @brief Invalidates the ARM9 instruction cache before jumping to child code.
 *
 * The CP15 operation is derived from Pico-Loader `cache.s`.
 */
static void itcm_icache_invalidate_all(void) {
    u32 zero = 0;
    __asm__ volatile("mcr p15, 0, %0, c7, c5, 0" :: "r"(zero) : "memory");
}

/**
 * @brief Disables caches and MPU mappings so the child controls memory state.
 */
static void itcm_disable_cache_mpu(void) {
    u32 control;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(control));
    control &= ~((1u << 14) | (1u << 12) | (1u << 2) | (1u << 0));
    __asm__ volatile("mcr p15, 0, %0, c1, c0, 0" :: "r"(control) : "memory");
    itcm_drain_write_buffer();
}

/**
 * @brief Performs the irreversible ARM9 shutdown, ARM7 sync and child entry jump.
 *
 * All recoverable validation and ARM7 preparation happens before this function.
 * Once it disables interrupts/PXI/FAT-visible hardware, failure recovery is no
 * longer available, so the remaining waits are the launch handshake itself.
 *
 * The final cache/PXI/IPCSYNC ordering is Pico-Loader-derived boot knowledge
 * adapted to direct DS Download Play payload launch instead of Pico-Loader's
 * patched reset-system flow.
 */
static void itcm_final_handover(const DownloadRsaFrame *copy_control,
                                 const DownloadRsaFrame *public_control,
                                 bool switch_to_ntr) {
    /*
     * Irreversible final handover starts here: IRQs, PXI, FAT and UI recovery
     * are no longer available after hardware shutdown. All recoverable ARM7
     * preparation/copy waits are bounded before this function is entered; the
     * remaining sync loops wait for the ARM7 boot stub and the loaded program.
     */
    itcm_stop_arm9_hardware();
    itcm_dcache_flush_all();
    itcm_icache_invalidate_all();
    itcm_disable_cache_mpu();
    itcm_copy_sections_to_actual(copy_control, switch_to_ntr);

    if (switch_to_ntr) {
        itcm_request_arm7_ntr_mode();
        itcm_prepare_arm9_ntr_mode();
    }

    itcm_write_handover_fields(public_control);
    itcm_clear_pxi();
    itcm_drain_write_buffer();
    itcm_ipc_sync_set(IPCSYNC_ARM9_RELEASE);
    *(volatile u32*)HANDOVER_ARM7_STATUS_ADDR = HANDOVER_ARM9_RELEASE;
    itcm_drain_write_buffer();
    while (itcm_ipc_sync_remote() != IPCSYNC_REMOTE_ARM7_ACK) {
    }
    itcm_ipc_sync_set(IPCSYNC_ARM9_ACK);
    while (itcm_ipc_sync_remote() == IPCSYNC_REMOTE_ARM7_ACK) {
    }
    itcm_ipc_sync_set(0);
    itcm_drain_write_buffer();

    if (switch_to_ntr) {
        itcm_finish_arm9_ntr_mode();
    }

    void (*entry)(void) = (void(*)(void))public_control->arm9_entrypoint;
    entry();
    for (;;) {
    }
}

/**
 * @brief Latest saved-dump paths resolved before loading a boot image.
 */
typedef struct {
    char nds_path[256];
    char bcn_path[256];
    char title[TEXT_UTF8_BYTES(TITLE_CHARS)];
} BootLaunchPaths;

/**
 * @brief Finds the newest saved NDS dump and derives its BCN sidecar path.
 */
static bool boot_find_latest_paths(BootLaunchPaths *paths) {
    char base_name[OUTPUT_BASE_BYTES];
    if (!paths) return false;
    memset(paths, 0, sizeof(*paths));
    if (!file_find_latest_download(paths->nds_path, sizeof(paths->nds_path),
                                  base_name, sizeof(base_name),
                                  paths->title, sizeof(paths->title))) {
        return false;
    }
    return path_make_output_file(paths->bcn_path, sizeof(paths->bcn_path), base_name, ".bcn");
}

/**
 * @brief Loads and validates the launch image before fixed memory is touched.
 */
static bool boot_load_latest_image(const BootLaunchPaths *paths, BootImage *img) {
    if (!paths || !img) return false;
    if (!boot_load_nds_image(paths->nds_path, paths->bcn_path, img)) {
        ui_log("Could not launch last saved download.");
        return false;
    }
    if (!copy_order_possible(&img->copy_control)) {
        boot_image_free(img);
        ui_log("Download memory map is not bootable.");
        return false;
    }
    if (!img->handover_valid) {
        boot_image_free(img);
        ui_log("DS Download Play handover data is missing.");
        return false;
    }
    if (!verify_public_key_loaded()) {
#ifndef DEBUG_VERSION
        boot_image_free(img);
        ui_log("Launch aborted: RSA public key missing at %s.", RSA_PUBLIC_KEY_PATH);
        return false;
#endif
    } else {
        if (!verify_sections(&img->control, img->sec)) {
#ifndef DEBUG_VERSION
            boot_image_free(img);
            ui_log("Launch aborted: RSA signature verification failed.");
            return false;
#endif
        }
    }
    return true;
}

/**
 * @brief Prepares fixed memory and waits for the ARM7 boot stub copy phase.
 */
static bool boot_prepare_arm7_launch(BootImage *img, bool switch_to_ntr, u32 game_code) {
    prepare_fixed_memory(img, img->handover);
    if (!wait_arm7_boot_ready(switch_to_ntr, game_code)) {
        boot_image_free(img);
        ui_log("ARM7 boot prepare failed.");
        return false;
    }

    ipc_send_command(ARM7_CMD_BOOT, NULL, 0);
    if (!wait_arm7_sections_copied()) {
        boot_image_free(img);
        ui_log("ARM7 boot copy timed out.");
        return false;
    }
    return true;
}

/**
 * @brief Enters the irreversible ARM9 handover path for the prepared image.
 */
static void boot_launch_prepared_image(bool switch_to_ntr) {
    ui_fade_to_white();
    DC_FlushAll();
    itcm_final_handover((const DownloadRsaFrame*)HANDOVER_RUNTIME_CONTROL_ADDR,
                        (const DownloadRsaFrame*)HANDOVER_FIXED_CONTROL_ADDR,
                        switch_to_ntr);
}

/**
 * @brief Loads the newest saved dump and transfers control to the child ARM9 entrypoint.
 *
 * The function validates the saved NDS/BCN pair, prepares fixed handover memory,
 * waits for the ARM7 boot stub, and finally calls the ITCM handover path. All
 * recoverable failures are reported to the UI before hardware shutdown begins.
 */
bool boot_latest_download(void) {
    if (g_runState != RUN_SCANNING) return false;

    BootLaunchPaths paths;
    if (!boot_find_latest_paths(&paths)) return false;

    ui_log("Launching %s…", paths.title[0] ? paths.title : "last saved download");
    ui_draw_now();

    BootImage img;
    if (!boot_load_latest_image(&paths, &img)) return false;

    const bool switch_to_ntr = systemIsTwlMode();
    const u32 game_code = boot_game_code(&img);
    if (!boot_prepare_arm7_launch(&img, switch_to_ntr, game_code)) return false;

    boot_launch_prepared_image(switch_to_ntr);
    /* Successful handover does not normally return; the loaded program owns execution. */
    return true;
}
