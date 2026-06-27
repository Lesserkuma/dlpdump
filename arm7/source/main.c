/**
 * @file main.c
 * @brief Starts the ARM7 runtime, PXI thread and lid/backlight handling.
 */
#include "../../common/ipc.h"

#include <nds.h>
#include <calico/dev/blk.h>
#include <calico/system/thread.h>

void arm7_process_pxi_word(u32 msg);

static Thread s_pxiThread;
static u8 s_pxiThreadStack[2048] ALIGNED_ATTR(8);
static bool s_lidClosed;

#define BACKLIGHT_BITS (PM_BACKLIGHT_BOTTOM | PM_BACKLIGHT_TOP)

/**
 * @brief Receives user PXI mailbox words and dispatches them to ARM7 IPC.
 */
static int pxi_thread_main(void *arg) {
    (void)arg;
    Mailbox mb;
    u32 slots[8];
    mailboxPrepare(&mb, slots, sizeof(slots) / sizeof(slots[0]));
    pxiSetMailbox(PxiChannel_User0, &mb);

    for (;;) {
        u32 msg = mailboxRecv(&mb);
        arm7_process_pxi_word(msg);
    }
    return 0;
}

/**
 * @brief Applies lid-derived backlight state to both DS screens.
 */
static void set_lid_backlight(bool closed) {
    u8 control = (u8)readPowerManagement(PM_CONTROL_REG);
    if (closed) {
        control &= (u8)~BACKLIGHT_BITS;
    } else {
        control |= BACKLIGHT_BITS;
    }
    writePowerManagement(PM_CONTROL_REG, control);
    s_lidClosed = closed;
}

/**
 * @brief Polls the hinge state and updates backlights only when it changes.
 */
static void update_lid_backlight(void) {
    bool closed = (keypadGetExtState() & KEY_HINGE) != 0;
    if (closed == s_lidClosed) return;
    set_lid_backlight(closed);
}

/**
 * @brief Initializes ARM7 services and runs the low-power main loop.
 */
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    envReadNvramSettings();
    keypadStartExtServer();
    lcdSetIrqMask(DISPSTAT_IE_ALL, DISPSTAT_IE_VBLANK);
    irqEnable(IRQ_VBLANK);
    rtcInit();
    rtcSyncTime();
    pmInit();
    pmSetSleepAllowed(false);
    s_lidClosed = (keypadGetExtState() & KEY_HINGE) == 0;
    update_lid_backlight();
    blkInit();

    threadPrepare(&s_pxiThread, pxi_thread_main, NULL, &s_pxiThreadStack[sizeof(s_pxiThreadStack)], MAIN_THREAD_PRIO);
    threadStart(&s_pxiThread);

    while (pmMainLoop()) {
        update_lid_backlight();
        threadWaitForVBlank();
    }
    return 0;
}
