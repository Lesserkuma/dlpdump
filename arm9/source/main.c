/**
 * @file main.c
 * @brief Starts the ARM9 application loop and handles fatal removal states.
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

bool g_repeatDownloads;

static volatile bool s_gameCardRemoved;
static bool s_gameCardWifiShutdownSent;

/**
 * @brief Returns whether the global exit hotkey is currently held.
 */
static bool exit_hotkey_held(u32 held) {
    return (held & KEY_DOWN) && (held & KEY_A) && (held & KEY_B) &&
           (held & KEY_L) && (held & KEY_R);
}

/**
 * @brief Marks the game card as removed from the IRQ callback.
 */
static void game_card_removed_irq(void) {
    s_gameCardRemoved = true;
    irqDisable(IRQ_CARD_LINE);
}

/**
 * @brief Installs the game-card-removal interrupt handler.
 */
static void game_card_removed_init(void) {
    /*
     * Detect DS-card removal in NTR mode through the Slot-1 IREQ/card-line
     * interrupt. Do not use SCFG_MC here: that register is DSi/TWL-only and is
     * unavailable when the ROM is launched as a normal DS title.
     */
    irqSet(IRQ_CARD_LINE, game_card_removed_irq);
    irqEnable(IRQ_CARD_LINE);
}

/**
 * @brief Returns whether the card-removal IRQ has fired.
 */
static bool game_card_removed(void) {
    return s_gameCardRemoved;
}

/**
 * @brief Stops Wi-Fi activity after the game card is removed.
 */
static void game_card_removed_stop_wifi(void) {
    if (s_gameCardWifiShutdownSent) return;
    s_gameCardWifiShutdownSent = true;

    /*
     * The Slot-1 IRQ can fire before the ARM7 Wi-Fi service is initialized
     * (for example if the card is already out while FAT/DLDI is starting).
     * Only send the shutdown command once the shared IPC block has been bound.
     */
    if (g_ipc.magic == IPC_MAGIC) {
        ipc_send_command(ARM7_CMD_WIFI_SHUTDOWN, NULL, 0);
    }
}

/**
 * @brief Shows the fatal removal screen and waits forever.
 */
static int game_card_removed_loop(void) {
    game_card_removed_stop_wifi();
    ui_show_game_card_removed();
    ui_draw_now();
    while (pmMainLoop()) {
        swiWaitForVBlank();
        ui_frame();
    }
    return 1;
}

/**
 * @brief Shows a fatal startup error and waits forever.
 */
static void fatal_loop(void) {
    while (pmMainLoop()) {
        swiWaitForVBlank();
        if (game_card_removed()) {
            game_card_removed_loop();
            return;
        }
        scanKeys();
        u32 down = keysDown();
        u32 held = keysHeld();
        if (exit_hotkey_held(held)) break;
        if (down & KEY_START) break;
        ui_frame();
    }
}

/**
 * @brief Initializes ARM9 services and runs scan, download, save and boot UI loops.
 */
int main(void) {
    game_card_removed_init();
    pmSetSleepAllowed(false);
    ui_init();
    ui_log("Running in %s mode.", systemIsTwlMode() ? "TWL" : "NTR");
    ui_log("Initializing file system…");
    ui_draw_now();

    if (game_card_removed()) return game_card_removed_loop();

    if (!fatInitDefault()) {
        ui_log("File system initialization failed.");
        fatal_loop();
        return 1;
    }

    ui_log("Preparing dump folder " OUTPUT_DIR "…");
    ui_draw_now();

    if (!file_ensure_output_dir()) {
        ui_log("Could not create " OUTPUT_DIR ".");
        fatal_loop();
        return 1;
    }

    if (verify_load_public_key()) {
        ui_log("Loaded RSA public key from %s.", RSA_PUBLIC_KEY_PATH);
    } else {
        ui_log("RSA public key not found at %s.", RSA_PUBLIC_KEY_PATH);
    }
    ui_draw_now();

    scan_reset();
    ipc_init();
    ui_log("Scanner is running. Waiting for beacons…");

    while (pmMainLoop()) {
        swiWaitForVBlank();
        g_frameCounter++;
        if (game_card_removed()) return game_card_removed_loop();
        scanKeys();
        u32 down = keysDown();
        u32 held = keysHeld();
        if (exit_hotkey_held(held)) {
            break;
        }
        if (down & KEY_B) {
            download_request_user_abort();
        }
        if (down & KEY_START) {
            boot_latest_download();
        }
        if (down & KEY_X) {
            g_repeatDownloads = !g_repeatDownloads;
            if (g_repeatDownloads) {
                for (unsigned i = 0; i < CONTENT_SLOT_COUNT; i++) g_slots[i].tried = false;
            } else {
                ui_deduplicate_history();
            }
            ui_log("Repeat downloads %s.", g_repeatDownloads ? "enabled" : "disabled");
            ui_mark_dirty();
        }

        ipc_poll();
        download_check_timeout();

        if (g_runState == RUN_SCANNING) {
            ContentSlot *s = scan_pick_next();
            if (s) download_start(s);
        }

        ui_frame();
        ipc_poll();
    }

    download_free();
    pcap_close();
    return 0;
}
