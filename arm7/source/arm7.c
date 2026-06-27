/**
 * @file arm7.c
 * @brief Owns ARM7 Wi-Fi state, IPC command dispatch and Calico MLME lifecycle.
 */
#include "arm7_internal.h"

/**
 * @brief ARM7-side runtime state shared by IPC, scan, reply and boot modules.
 */
IpcShared *g_ipc;
bool g_scan_enabled;
bool g_raw_capture_enabled;
bool g_netbuf_ready;
bool g_wifi_hw_started;
u16 g_assoc_aid;
ConnectParams g_active_parent;
u16 g_reply_buf_index;
u16 g_tx_seq;
u8 g_name_reply_step;
u8 g_scan_focus_channel;
u8 g_scan_current_channel;
ReplyParams g_pending_reply;
bool g_have_pending_reply;
u8 g_client_msg_tail[7];
bool g_have_client_msg_tail;
bool g_seen_rsa_cmd;
u16 g_fast_next_packet;
u16 g_fast_received_packets;
u16 g_fast_expected_total;
u16 g_fast_last_packet;
u16 g_fast_complete_next;
u16 g_fast_complete_total;
u8 g_fast_gotall_replies;
bool g_fast_data_complete;
u32 g_event_id;

/**
 * @brief NetBuf backing storage and packet bitmaps owned by the ARM7 process.
 */
u8 s_netbuf_pool_mem[NETBUF_POOL_MEM_SZ] ALIGNED_ATTR(32);
u8 s_fast_received_bits[(FAST_MAX_PACKETS + 7u) / 8u];
const u16 s_netbuf_tx_counts[5] = {0, 4, 0, 0, 0};
const u16 s_netbuf_rx_counts[5] = {0, 4, 0, 0, 0};

/**
 * @brief Focused scan target and recently completed focus history.
 */
Arm7ScanFocus g_scan_focus;
Arm7ScanFocus g_scan_focus_done[SCAN_FOCUS_DONE_COUNT];
u8 g_scan_focus_done_next;

static void arm7_graceful_stop(void);

/**
 * @brief Clears the queued child reply and protocol tail cached for retransmit.
 */
void arm7_clear_pending_reply(void) {
    memset(&g_pending_reply, 0, sizeof(g_pending_reply));
    g_have_pending_reply = false;
    memset(g_client_msg_tail, 0, sizeof(g_client_msg_tail));
    g_have_client_msg_tail = false;
    g_seen_rsa_cmd = false;
    if (g_wifi_hw_started) MWL_REG(W_TXBUF_REPLY1) = 0;
}

/**
 * @brief Forces short preamble in Calico state and the Mitsumi Wi-Fi register.
 */
static void arm7_set_short_preamble(void) {
    mwlDevSetPreamble(true);
    MWL_REG(W_PREAMBLE) |= 0x0006;
}

/**
 * @brief Updates Calico and hardware Wi-Fi power-save state for child replies.
 */
void arm7_set_power_state(u16 state) {
    s_mwlState.is_power_save = (state >> 1) & 1u;
    MWL_REG(W_POWERSTATE) = state;
}

/**
 * @brief Initializes Calico NetBuf pools once and verifies both pool types work.
 */
static bool arm7_init_netbuf_pools(void) {
    if (!g_netbuf_ready) {
        _netbufPrvInitPools(s_netbuf_pool_mem, s_netbuf_tx_counts, s_netbuf_rx_counts);
        g_netbuf_ready = true;
    }

    NetBuf *tx = netbufAlloc(0, 0x100, NetBufPool_Tx);
    NetBuf *rx = netbufAlloc(0, 0x100, NetBufPool_Rx);
    if (tx) netbufFree(tx);
    if (rx) netbufFree(rx);
    return tx && rx;
}

/**
 * @brief Builds the six-byte local-guest association SSID from parent IDs.
 */
static void arm7_make_assoc_ssid(char *ssid, u32 game_group_id, u16 temporary_group_id) {
    memset(ssid, 0, ASSOC_SSID_LEN);
    ssid[0] = (char)game_group_id;
    ssid[1] = (char)(game_group_id >> 8);
    ssid[2] = (char)(game_group_id >> 16);
    ssid[3] = (char)(game_group_id >> 24);
    ssid[4] = (char)temporary_group_id;
    ssid[5] = (char)(temporary_group_id >> 8);
}

/**
 * @brief Converts the shared ARM9 BSS descriptor into Calico's descriptor type.
 */
static void copy_bss_from_common(WlanBssDesc *dst, const ScanBssDesc *src) {
    memset(dst, 0, sizeof(*dst));
    memcpy(dst->bssid, src->bssid, 6);
    dst->ssid_len = src->ssid_len <= 32 ? src->ssid_len : 32;
    memcpy(dst->ssid, src->ssid, dst->ssid_len);
    dst->ieee_caps = src->ieee_caps;
    dst->ieee_basic_rates = src->ieee_basic_rates;
    dst->ieee_all_rates = src->ieee_all_rates;
    dst->auth_type = (WlanBssAuthType)src->auth_type;
    dst->rssi = src->rssi;
    dst->channel = src->channel;
}

/**
 * @brief Continues association after MLME join, or reports join failure to ARM9.
 */
static void arm7_on_join_end(bool ok) {
    if (!ok) {
        arm7_push_event(EVENT_CONNECT_FAILED, CONNECT_FAIL_JOIN_TIMEOUT, NULL, 0);
        return;
    }
    s_mwlState.has_beacon_sync = true;
    arm7_set_short_preamble();
    if (!mwlMlmeAuthenticate(1000)) {
        arm7_push_event(EVENT_CONNECT_FAILED, CONNECT_FAIL_AUTH_START, NULL, 0);
    }
}

/**
 * @brief Continues association after authentication, or reports auth failure.
 */
static void arm7_on_auth_end(unsigned status) {
    if (status != 0) {
        arm7_push_event(EVENT_CONNECT_FAILED,
                        (u16)(CONNECT_FAIL_AUTH_STATUS | (status & CONNECT_FAIL_STATUS_MASK)),
                        NULL, 0);
        return;
    }
    arm7_set_short_preamble();
    if (!mwlMlmeAssociate(2000, false)) {
        arm7_push_event(EVENT_CONNECT_FAILED, CONNECT_FAIL_ASSOC_START, NULL, 0);
    }
}

/**
 * @brief Finalizes association and publishes the parent context to ARM9.
 */
static void arm7_on_assoc_end(unsigned status) {
    if (status != 0) {
        arm7_push_event(EVENT_CONNECT_FAILED,
                        (u16)(CONNECT_FAIL_ASSOC_STATUS | (status & CONNECT_FAIL_STATUS_MASK)),
                        NULL, 0);
        return;
    }
    ReplyParams p;
    memset(&p, 0, sizeof(p));
    p.reply_type = REPLY_DUMMY;
    arm7_write_reply_frame(&p);
    arm7_push_event(EVENT_CONNECTED, g_assoc_aid, &g_active_parent, sizeof(g_active_parent));
}

/**
 * @brief Converts Calico state loss into a disconnect event for ARM9.
 */
static void arm7_on_state_lost(MwlStatus new_class, unsigned reason) {
    (void)new_class;
    arm7_clear_pending_reply();
    arm7_push_event(EVENT_DISCONNECTED, (u16)reason, NULL, 0);
}

/**
 * @brief Installs the ARM7 callbacks used by Calico MLME scan/join state.
 */
static void arm7_install_mlme_callbacks(void) {
    MwlMlmeCallbacks *cb = mwlMlmeGetCallbacks();
    memset(cb, 0, sizeof(*cb));
    cb->onBssInfo = arm7_on_bss_info;
    cb->onScanEnd = arm7_on_scan_end;
    cb->onJoinEnd = arm7_on_join_end;
    cb->onAuthEnd = arm7_on_auth_end;
    cb->onAssocEnd = arm7_on_assoc_end;
    cb->onStateLost = arm7_on_state_lost;
}

/**
 * @brief Stops Calico Wi-Fi activity while preserving local callback ownership.
 */
static void arm7_graceful_stop(void) {
    mwlDevGracefulStop();
    arm7_install_mlme_callbacks();
}

/**
 * @brief Hard-stops Wi-Fi and powers it down after game-card removal.
 */
static void arm7_shutdown_wifi_for_eject(void) {
    g_scan_enabled = false;
    g_raw_capture_enabled = false;
    g_assoc_aid = 0;
    scan_focus_clear();
    arm7_clear_pending_reply();
    arm7_fast_reset_packets(0);

    if (g_wifi_hw_started) {
        /* Hard stop: do not send deauth/cancel frames after the card is gone. */
        mwlDevStop();
        _mwlRxQueueClear();
        _mwlTxQueueClear(0);
        _mwlTxQueueClear(1);
        _mwlTxQueueClear(2);
        MWL_REG(W_TXBUF_REPLY1) = 0;
        MWL_REG(W_TXBUF_REPLY2) = 0;
        mwlDevShutdown();
        g_wifi_hw_started = false;
    }

    pmPowerOff(POWCNT_WL_MITSUMI);
    pmSetPowerLed(PmLedMode_Steady);
}

/**
 * @brief Clears pending replies and attempts a deauthentication cancel frame.
 */
static void arm7_child_cancel(void) {
    arm7_clear_pending_reply();
    arm7_fast_reset_packets(0);
    if (mwlMlmeDeauthenticate()) return;

    arm7_log("Could not send child cancel deauth.");
    mwlDevStop();
    arm7_push_event(EVENT_DISCONNECTED, 0, NULL, 0);
}

/**
 * @brief Starts Calico local-guest join/auth/assoc for the selected parent.
 */
static void arm7_connect(const ConnectParams *params) {
    g_scan_enabled = false;
    g_raw_capture_enabled = true;
    g_active_parent = *params;
    g_assoc_aid = 0;
    g_name_reply_step = 0;
    arm7_clear_pending_reply();
    arm7_fast_reset_packets(0);
    mwlDevStop();
    _mwlRxQueueClear();
    _mwlTxQueueClear(0);
    _mwlTxQueueClear(1);
    _mwlTxQueueClear(2);
    mwlDevSetMode(MwlMode_LocalGuest);
    arm7_set_power_state(1);

    WlanBssDesc bss;
    copy_bss_from_common(&bss, &params->bss);
    bss.ssid_len = ASSOC_SSID_LEN;
    arm7_make_assoc_ssid(bss.ssid, params->game_group_id, params->temporary_group_id);
    bss.auth_type = WlanBssAuthType_Open;
    mwlDevSetAuth(bss.auth_type, NULL);
    if (!mwlMlmeJoin(&bss, 2000)) {
        arm7_push_event(EVENT_CONNECT_FAILED, CONNECT_FAIL_JOIN_START, NULL, 0);
    } else {
    }
}

/**
 * @brief Executes one ARM9-to-ARM7 command from the shared IPC command slot.
 *
 * The command ID is read after invalidating the cache line, then dispatched to
 * Wi-Fi initialization, scan, association, reply queuing, boot preparation, or
 * shutdown paths. Failures that the ARM9 UI can report are converted into IPC
 * events; unknown command IDs are ignored so stale kicks cannot corrupt state.
 */
static void arm7_handle_command(void) {
    if (!g_ipc || g_ipc->magic != IPC_MAGIC) return;
    ipc_invalidate(&g_ipc->command, sizeof(g_ipc->command));
    u32 id = g_ipc->command.id;

    switch (id) {
        case ARM7_CMD_WIFI_INIT: {
            bool ok = mwlCalibLoad();
            if (ok && systemIsTwlMode()) gpioSetWlModule(GpioWlModule_Mitsumi);
            if (ok) {
                if (!arm7_init_netbuf_pools()) {
                    arm7_push_event(EVENT_ERROR, 0, "Could not initialize netbuf pool.", sizeof("Could not initialize netbuf pool."));
                    break;
                }
                pmSetPowerLed(PmLedMode_BlinkFast);
                pmPowerOn(POWCNT_WL_MITSUMI);
                REG_EXMEMCNT2 = 0x30;
                mwlDevWakeUp();
                mwlDevReset();
                mwlDevSetMode(MwlMode_LocalGuest);
                g_wifi_hw_started = true;

                arm7_install_mlme_callbacks();

                arm7_push_event(EVENT_ARM7_READY, 0, NULL, 0);
            } else {
                arm7_push_event(EVENT_ERROR, 0, "Could not load Wi-Fi calibration.", sizeof("Could not load Wi-Fi calibration."));
            }
            break;
        }
        case ARM7_CMD_SCAN_START:
            arm7_start_scan();
            break;
        case ARM7_CMD_SCAN_STOP:
            g_scan_enabled = false;
            mwlDevStop();
            break;
        case ARM7_CMD_CONNECT:
            arm7_connect(&g_ipc->command.arg.connect);
            break;
        case ARM7_CMD_QUEUE_REPLY:
            arm7_write_reply_frame(&g_ipc->command.arg.reply);
            break;
        case ARM7_CMD_RAW_CAPTURE:
            g_raw_capture_enabled = g_ipc->command.arg.raw_capture.enabled != 0;
            break;
        case ARM7_CMD_RESET_TO_SCAN:
            g_raw_capture_enabled = false;
            g_scan_enabled = true;
            arm7_graceful_stop();
            arm7_start_scan();
            break;
        case ARM7_CMD_BOOT_PREPARE:
            arm7_prepare_boot_stub(g_ipc->command.arg.boot_prepare.switch_to_ntr != 0,
                                  g_ipc->command.arg.boot_prepare.game_code);
            break;
        case ARM7_CMD_BOOT:
            arm7_boot_downloaded_program();
            break;
        case ARM7_CMD_CHILD_CANCEL:
            arm7_child_cancel();
            break;
        case ARM7_CMD_WIFI_SHUTDOWN:
            arm7_shutdown_wifi_for_eject();
            break;
        default:
            break;
    }
}

/**
 * @brief Binds the shared IPC memory block supplied by ARM9.
 */
void arm7_bind_ipc(IpcShared *ipc) {
    g_ipc = ipc;
    if (g_ipc) {
        g_ipc->event_r = 0;
        g_ipc->event_w = 0;
        g_ipc->dropped_events = 0;
    }
}

/**
 * @brief Routes one PXI word from ARM9 as either initial bind or command kick.
 */
void arm7_process_pxi_word(u32 msg) {
    if (!g_ipc) {
        arm7_bind_ipc((IpcShared*)(msg << 2));
        return;
    }
    if (msg == PXI_CMD_KICK) {
        arm7_handle_command();
    }
}
