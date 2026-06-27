// SPDX-License-Identifier: ZPL-2.1
// SPDX-FileCopyrightText: Copyright fincs, devkitPro
/**
 * @file mwl_private_common.h
 * @brief Calico MWL private declarations required by local RX/MGMT overrides.
 *
 * This header is derived from Calico `source/dev/mwl/common.h` and keeps the
 * ZPL-2.1 license. It intentionally exposes only the private state, task IDs
 * and helper prototypes needed by `mwl_rx_ext.c` and `mwl_mgmt_ext.c`.
 */
#pragma once
#include <calico/types.h>
#include <calico/system/mutex.h>
#include <calico/system/tick.h>
#include <calico/system/dietprint.h>
#include <calico/dev/mwl.h>

//#define MWL_DEBUG

#ifndef MWL_DEBUG
#define dietPrint(...) ((void)0)
#endif

/**
 * @brief Calico MWL worker-task identifiers used by the shared task mask.
 */
typedef enum MwlTask {
    MwlTask_ExitThread,

    // High priority tasks
    MwlTask_TxEnd,
    MwlTask_RxEnd,

    // Medium priority tasks
    MwlTask_BeaconLost,
    MwlTask_RxMgmtCtrlFrame,

    // Other tasks
    MwlTask_MlmeProcess,
    MwlTask_RxDataFrame,

    MwlTask__Count,
} MwlTask;

/**
 * @brief Calico MLME state values mirrored by the local RX task.
 */
typedef enum MwlMlmeState {
    MwlMlmeState_Idle,
    MwlMlmeState_Preparing,

    MwlMlmeState_ScanSetup,
    MwlMlmeState_ScanBusy,

    MwlMlmeState_JoinBusy,
    MwlMlmeState_JoinDone,

    MwlMlmeState_AuthBusy,
    MwlMlmeState_AuthDone,

    MwlMlmeState_AssocBusy,
    MwlMlmeState_AssocDone,

    MwlMlmeState_OnStateLost,
} MwlMlmeState;

/**
 * @brief Calico transmit queue node retained for private MWL state layout.
 */
typedef struct MwlTxQueue {
    NetBufListNode list;
    MwlTxCallback cb;
    void* arg;
} MwlTxQueue;

/**
 * @brief Private Calico MWL driver state shared with local RX/MGMT overrides.
 *
 * The layout follows Calico so the replacement RX object can interoperate with
 * the unmodified Calico MLME, TX and task code linked from the toolchain.
 */
typedef struct MwlState {
    u32 task_mask;

    u16 mode            : 2;
    u16 status          : 2;
    u16 has_beacon_sync : 1;
    u16 is_power_save   : 1;
    u16 tx_busy         : 3;
    u16 wep_enabled     : 1;
    u16 _pad            : 6;

    u16 bssid[3];

    u16 ssid_len;
    char ssid[WLAN_MAX_SSID_LEN];

    u16 rx_pos;
    u16 tx_pos[3];
    union {
        u16 tx_reply_pos[2];
        struct {
            u16 tx_beacon_pos;
            u16 tx_cmd_pos;
        };
    };

    u16 rx_wrcsr;

    NetBufListNode rx_mgmt;
    NetBufListNode rx_data;

    u16 tx_size[3];
    Mutex tx_mutex;
    MwlTxQueue tx_queues[3];

    TickTask timeout_task;
    TickTask periodic_task;

    u16 beacon_loss_cnt;
    u16 beacon_loss_thr;

    u8 mlme_state;
    MwlMlmeCallbacks mlme_cb;
    union {
        struct {
            WlanBssScanFilter filter;
            u16 cur_ch;
            u16 ch_dwell_ticks;
            u16 update_period;
            u16 dwell_elapsed;
        } scan;

        struct {
            NetBuf* pTxPacket;
            u16 status;
        } auth;

        struct {
            u16 status;
            bool fake_cck_rates;
        } assoc;

        struct {
            u16 reason;
            MwlStatus new_class;
        } loss;
    } mlme;
} MwlState;

extern MwlState s_mwlState;

/** @brief Reads a Mitsumi Wi-Fi BBP register through Calico's private helper. */
unsigned _mwlBbpRead(unsigned reg);

/** @brief Writes a Mitsumi Wi-Fi BBP register through Calico's private helper. */
void _mwlBbpWrite(unsigned reg, unsigned value);

/** @brief Sends a Mitsumi Wi-Fi RF command through Calico's private helper. */
void _mwlRfCmd(u32 cmd);

#define _mwlPushTask(...) ({ \
    const MwlTask _task_ids[] = { __VA_ARGS__ }; \
    u32 _task_mask = 0; \
    for (unsigned _task_i = 0; _task_i < sizeof(_task_ids)/sizeof(_task_ids[0]); _task_i ++) { \
        _task_mask |= 1U << _task_ids[_task_i]; \
    } \
    _mwlPushTaskImpl(_task_mask); \
})

/** @brief Calico interrupt entry point for the MWL device. */
void _mwlIrqHandler(void);

/** @brief Pushes a precomputed task bitmask into Calico's MWL worker queue. */
MK_EXTERN32 void _mwlPushTaskImpl(u32 mask) __asm__("_mwlPushTask");

/** @brief Pops the next Calico MWL worker task. */
MK_EXTERN32 MwlTask _mwlPopTask(void);

/** @brief Clears management and data RX queues owned by the local RX override. */
void _mwlRxQueueClear(void);

/** @brief Clears one Calico TX queue. */
void _mwlTxQueueClear(unsigned qid);

/** @brief Initializes Calico's private NetBuf pools. */
void _netbufPrvInitPools(void* pktmem, const u16* tx_counts, const u16* rx_counts);

/** @brief Delivers parsed BSS information to Calico MLME scan handling. */
void _mwlMlmeOnBssInfo(WlanBssDesc* bssInfo, WlanBssExtra* bssExtra);

/** @brief Delivers a beacon to Calico MLME join handling. */
void _mwlMlmeHandleJoin(WlanBeaconHdr* beaconHdr, WlanBssDesc* bssInfo, WlanBssExtra* bssExtra);

/** @brief Delivers an authentication frame to Calico MLME. */
void _mwlMlmeHandleAuth(NetBuf* pPacket);

/** @brief Frees Calico's cached authentication challenge reply. */
void _mwlMlmeAuthFreeReply(void);

/** @brief Delivers an association response to Calico MLME. */
void _mwlMlmeHandleAssocResp(NetBuf* pPacket);

/** @brief Reports disassociation/deauthentication state loss to Calico MLME. */
void _mwlMlmeHandleStateLoss(NetBuf* pPacket, WlanMgmtType type);

/** @brief Builds a Calico-owned probe request NetBuf. */
NetBuf* _mwlMgmtMakeProbeReq(const void* bssid, const char* ssid, unsigned ssid_len);

/** @brief Builds a Calico-owned authentication NetBuf. */
NetBuf* _mwlMgmtMakeAuth(const void* target, WlanAuthHdr const* auth_hdr, const void* chal_text, unsigned chal_len);

/** @brief Builds a Calico-owned association request NetBuf. */
NetBuf* _mwlMgmtMakeAssocReq(const void* target, bool fake_cck_rates);

/** @brief Builds a Calico-owned deauthentication NetBuf. */
NetBuf* _mwlMgmtMakeDeauth(const void* target, unsigned reason);
