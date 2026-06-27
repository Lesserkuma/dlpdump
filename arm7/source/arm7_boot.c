/**
 * @file arm7_boot.c
 * @brief Copies and launches the ARM7 boot stub used for child handoff.
 */
#include "arm7_internal.h"

/**
 * @brief Quiesces ARM7 Wi-Fi state and installs the copied boot stub.
 *
 * When booting from TWL mode, this also runs the Pico-Loader-derived ARM7
 * DS-mode compatibility preparation before publishing `EVENT_BOOT_READY`.
 */
void arm7_prepare_boot_stub(bool switch_to_ntr, u32 game_code) {
    g_scan_enabled = false;
    g_raw_capture_enabled = false;
    arm7_clear_pending_reply();
    mwlDevGracefulStop();
    _mwlRxQueueClear();
    _mwlTxQueueClear(0);
    _mwlTxQueueClear(1);
    _mwlTxQueueClear(2);

    if (switch_to_ntr && systemIsTwlMode()) {
        arm7_prepare_ds_mode_for_boot(game_code);
    }

    u32 len = (u32)(arm7BootStubEnd - arm7BootStubStart);
    memcpy((void*)BOOT_ARM7_STUB_ADDR, arm7BootStubStart, len);
    arm7_push_event(EVENT_BOOT_READY, 0, NULL, 0);
}

/**
 * @brief Jumps into the ARM7 boot stub installed at the fixed boot address.
 *
 * The stub copies the downloaded ARM7 section and synchronizes with ARM9 via
 * the shared boot status word; this call normally does not return.
 */
void arm7_boot_downloaded_program(void) {
    Arm7BootStubFn fn = (Arm7BootStubFn)BOOT_ARM7_STUB_ADDR;
    fn((const void*)BOOT_FIXED_CONTROL_ADDR, (volatile u32*)BOOT_ARM7_STATUS_ADDR);
}
