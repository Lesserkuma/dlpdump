// SPDX-License-Identifier: ZPL-2.1
// SPDX-FileCopyrightText: Copyright fincs, devkitPro
/**
 * @file mwl_rx_ext.c
 * @brief Calico-derived MWL RX task replacement with DS Download Play hooks.
 *
 * This file is based on Calico `source/dev/mwl/mwl_rx.c`. The local version
 * keeps Calico MLME queue ownership and RX cursor handling, while adding raw
 * beacon forwarding, Nintendo vendor-IE capture, parent-command detection and
 * DS Download Play statistics hooks.
 */
#include "mwl_private_common.h"
#include "../../common/protocol.h"
#include "../../common/dlp_wire.h"

#include <calico/arm/common.h>
#include <calico/system/thread.h>
#include <calico/nds/dma.h>

/* Hooks implemented by arm7/source/arm7.c.  They are deliberately weakly
   coupled to Calico's public API; this file replaces Calico's stock mwl_rx.o
   so the DS Download Play MP frame types can be surfaced to ARM9. */
void arm7_raw_frame(const void *frame, unsigned len, const MwlDataRxHdr *rxhdr, MwlRxType type);
void arm7_bss_info(const WlanBssDesc *bss, const u8 *vendor_ie_payload, unsigned vendor_ie_len, const void *frame, unsigned frame_len);
void arm7_mp_cmd_frame(const u8 *frame, unsigned len, const MwlDataRxHdr *rxhdr);
void arm7_mp_poll_frame(const u8 *frame, unsigned len, const MwlDataRxHdr *rxhdr);
void arm7_assoc_aid(u16 aid);

#define MWL_RX_STATIC_MEM_SZ 1024

static struct {
    NetBuf hdr;
    u8 body[MWL_RX_STATIC_MEM_SZ];
} s_mwlRxStaticMem;

/**
 * @brief Decides whether a received frame can reuse the static RX buffer.
 *
 * Management, control, and generic data frames may be queued into Calico-owned
 * queues and therefore need heap-backed NetBuf storage; synchronously consumed
 * DS Download Play MP frames can use the static scratch buffer.
 */
MK_CONSTEXPR bool _mwlRxCanUseStaticMem(MwlRxType type, unsigned len)
{
    switch (type) {
        default:
            return len <= MWL_RX_STATIC_MEM_SZ;

        case MwlRxType_IeeeMgmtOther:
        case MwlRxType_IeeeCtrl:
        case MwlRxType_IeeeData:
            return false;
    }
}

/**
 * @brief Reads an even-aligned RX payload span from the Nintendo Wi-Fi FIFO.
 *
 * @param dst Destination buffer whose size is at least `len` bytes.
 * @param len Number of bytes to copy from the hardware receive buffer.
 */
MK_NOINLINE static void _mwlRxRead(void* dst, unsigned len)
{
    u16* dst16 = (u16*)dst;
    while (len > 1) {
        *dst16++ = MWL_REG(W_RXBUF_RD_DATA);
        len -= 2;
    }
}

/**
 * @brief Recognizes host command payloads that Calico reports as raw data frames.
 *
 * The helper validates the DS Download Play command envelope length before the
 * ARM7 command parser sees the payload.
 */
static bool _mwlRxIsHostCmd(const void* frame, unsigned len)
{
    if (len <= sizeof(WlanMacHdr)) {
        return false;
    }

    const u8* payload = (const u8*)frame + sizeof(WlanMacHdr);
    unsigned payload_len = len - sizeof(WlanMacHdr);
    const u8 *msg = NULL;
    unsigned msg_len = 0;
    return dlp_host_envelope_parse(payload, payload_len, &msg, &msg_len);
}

/**
 * @brief Parses a beacon and forwards Nintendo vendor data to ARM9 scanning.
 *
 * Normal Calico MLME beacon handling is preserved, while complete beacon frames
 * and decoded vendor IE spans are surfaced for metadata, PCAP, and BCN output.
 */
MK_NOINLINE static void _mwlRxBeaconFrame(NetBuf* pPacket, MwlDataRxHdr* rxhdr, unsigned frame_len)
{
    // Pop 802.11 header
    MK_ASSUME(pPacket->len >= sizeof(WlanMacHdr));
    WlanMacHdr* dot11hdr = netbufPopHeaderType(pPacket, WlanMacHdr);
    arm7_raw_frame(dot11hdr, frame_len, rxhdr, MwlRxType_IeeeBeacon);

    // Save beacon header for later
    WlanBeaconHdr* hdr = (WlanBeaconHdr*)netbufGet(pPacket);

    // Parse beacon body
    WlanBssDesc desc;
    WlanBssExtra extra;
    wlanParseBeacon(&desc, &extra, pPacket);
    __builtin_memcpy(desc.bssid, dot11hdr->tx_addr, 6);
    desc.rssi = mwlDecodeRssi(rxhdr->rssi);

    // Apply contention-free duration if needed
    if (extra.cfp && (dot11hdr->duration & 0x8000)) {
        MWL_REG(W_CONTENTFREE) = wlanDecode16(extra.cfp->dur_remaining);
    }

    // Surface Nintendo vendor data to the DS Download Play client.
    if (extra.nin) {
        unsigned vendor_ie_len = 0x14 + extra.nin->data_sz;
        arm7_bss_info(&desc, (const u8*)extra.nin, vendor_ie_len, dot11hdr, frame_len);
    }

    // If a BSSID filter is active, ignore non-matching beacons
    if (!(s_mwlState.bssid[0] & 1) && !(rxhdr->status & (1U<<15))) {
        return;
    }

    // Forward BSS description to MLME if we are scanning or joining
    if (s_mwlState.mlme_state == MwlMlmeState_ScanBusy) {
        _mwlMlmeOnBssInfo(&desc, &extra);
    } else if (s_mwlState.mlme_state == MwlMlmeState_JoinBusy) {
        _mwlMlmeHandleJoin(hdr, &desc, &extra);
    }

    // Below section only relevant when we have joined a BSS
    if (!s_mwlState.has_beacon_sync) {
        return;
    }

    // Clear beacon loss counter for obvious reasons
    s_mwlState.beacon_loss_cnt = 0;

    // Calculate timestamp of next TBTT (target beacon transmission time).
    // Note: as per 802.11 spec, this formula accounts for beacons delayed due to
    // medium contention - next beacon is expected to be received at the usual time.
    u32 beaconPeriodUs = hdr->interval << 10;
    u64 nextTbttUs = hdr->timestamp[0] | ((u64)hdr->timestamp[1] << 32);
    nextTbttUs = ((nextTbttUs / beaconPeriodUs) + 1) * beaconPeriodUs;

    // Configure next TBTT interrupt.
    // Note: upon receipt of a beacon matching W_BSSID, the hardware automatically
    // updates our local clock (W_US_COUNT) with that of the BSS. Nice!
    MWL_REG(W_US_COMPARE3) = nextTbttUs >> 48;
    MWL_REG(W_US_COMPARE2) = nextTbttUs >> 32;
    MWL_REG(W_US_COMPARE1) = nextTbttUs >> 16;
    MWL_REG(W_US_COMPARE0) = nextTbttUs | 1;
}

/**
 * @brief Parses probe responses during active scans using Calico MLME parsing.
 *
 * Probe responses are not DS Download Play metadata sources here; they only
 * feed the join/scan state machine and are ignored outside scan-busy state.
 */
MK_NOINLINE static void _mwlRxProbeResFrame(NetBuf* pPacket, MwlDataRxHdr* rxhdr)
{
    // Ignore this packet if we are not scanning
    if (s_mwlState.mlme_state != MwlMlmeState_ScanBusy) {
        return;
    }

    // Pop 802.11 header
    MK_ASSUME(pPacket->len >= sizeof(WlanMacHdr));
    WlanMacHdr* dot11hdr = netbufPopHeaderType(pPacket, WlanMacHdr);

    dietPrint("PrbRsp BSSID=%.2X:%.2X:%.2X:%.2X:%.2X:%.2X\n",
        dot11hdr->tx_addr[0],dot11hdr->tx_addr[1],dot11hdr->tx_addr[2],
        dot11hdr->tx_addr[3],dot11hdr->tx_addr[4],dot11hdr->tx_addr[5]);

    // Parse probe response body
    WlanBssDesc desc;
    WlanBssExtra extra;
    wlanParseBeacon(&desc, &extra, pPacket);
    __builtin_memcpy(desc.bssid, dot11hdr->tx_addr, 6);
    desc.rssi = mwlDecodeRssi(rxhdr->rssi);

    // Forward BSS description to MLME
    _mwlMlmeOnBssInfo(&desc, &extra);
}

/**
 * @brief Allocates receive storage appropriate for the frame lifetime.
 *
 * @param pkt_type Calico RX classification from the hardware status word.
 * @param pkt_read_sz Rounded hardware MPDU size to read.
 * @param static_rx Receives whether the returned NetBuf is static scratch
 *        storage and must not be freed.
 * @return NetBuf ready to receive packet bytes.
 */
static NetBuf* _mwlRxAllocPacket(MwlRxType pkt_type, unsigned pkt_read_sz, bool* static_rx)
{
    *static_rx = _mwlRxCanUseStaticMem(pkt_type, pkt_read_sz);
    if_likely (*static_rx) {
        NetBuf* pPacket = &s_mwlRxStaticMem.hdr;
        pPacket->capacity = MWL_RX_STATIC_MEM_SZ;
        pPacket->pos = 0;
        pPacket->len = pkt_read_sz;
        return pPacket;
    }

    NetBuf* pPacket;
    while (!(pPacket = netbufAlloc(0, pkt_read_sz, NetBufPool_Rx))) {
        threadSleep(1000);
    }
    return pPacket;
}

/**
 * @brief Dispatches one decoded RX packet to DS Download Play hooks or Calico queues.
 *
 * Packets consumed immediately leave `*ppPacket` intact for normal cleanup.
 * Packets handed to Calico's management/data queues set `*ppPacket` to NULL so
 * the caller does not free queued ownership.
 */
static void _mwlRxDispatchFrame(NetBuf** ppPacket, MwlDataRxHdr* rxhdr,
    MwlRxType pkt_type, unsigned frame_len, bool has_dot11hdr,
    WlanMacHdr* dot11hdr, bool is_host_cmd)
{
    NetBuf* pPacket = *ppPacket;
    if (rxhdr->status & (1U<<9)) {
        dietPrint("[RX] fragmented packet!\n");
        return;
    }
    if (pkt_type != MwlRxType_IeeeCtrl && !has_dot11hdr) {
        dietPrint("[RX] missing 802.11 hdr!\n");
        return;
    }

    switch (pkt_type) {
        default: {
            if (is_host_cmd) {
                arm7_mp_cmd_frame(netbufGet(pPacket), frame_len, rxhdr);
            } else {
                dietPrint("[RX:%02X] t=%X len=%u ieee=%.4X\n",
                    mwlDecodeRssi(rxhdr->rssi), pkt_type,
                    rxhdr->mpdu_len, dot11hdr->fc.value);
            }
            break;
        }

        case MwlRxType_MpCmdFrame: {
            arm7_mp_cmd_frame(netbufGet(pPacket), frame_len, rxhdr);
            break;
        }

        case MwlRxType_IeeeMgmtOther: {
            if (dot11hdr->fc.type == WlanFrameType_Management) {
                arm7_raw_frame(dot11hdr, frame_len, rxhdr, pkt_type);
                if (dot11hdr->fc.subtype == WlanMgmtType_ProbeResp) {
                    _mwlRxProbeResFrame(pPacket, rxhdr);
                } else {
                    netbufQueueAppend(&s_mwlState.rx_mgmt, pPacket);
                    *ppPacket = NULL;
                    _mwlPushTask(MwlTask_RxMgmtCtrlFrame);
                }
            }
            break;
        }

        case MwlRxType_IeeeBeacon: {
            if (dot11hdr->fc.type == WlanFrameType_Management &&
                dot11hdr->fc.subtype == WlanMgmtType_Beacon) {
                _mwlRxBeaconFrame(pPacket, rxhdr, frame_len);
            }
            break;
        }

        case MwlRxType_IeeeData: {
            if (is_host_cmd) {
                arm7_mp_cmd_frame(netbufGet(pPacket), frame_len, rxhdr);
            } else if (dot11hdr->fc.type == WlanFrameType_Data &&
                !(dot11hdr->fc.subtype & WLAN_DATA_IS_NULL) &&
                s_mwlState.status == MwlStatus_Class3) {
                pPacket->user[0] = mwlDecodeRssi(rxhdr->rssi);
                netbufQueueAppend(&s_mwlState.rx_data, pPacket);
                *ppPacket = NULL;
                _mwlPushTask(MwlTask_RxDataFrame);
            }
            break;
        }

        case MwlRxType_MpEndFrame:
        case MwlRxType_MpReplyFrame:
        case MwlRxType_Null: {
            if (is_host_cmd) {
                arm7_mp_cmd_frame(netbufGet(pPacket), frame_len, rxhdr);
            } else {
                arm7_mp_poll_frame(netbufGet(pPacket), frame_len, rxhdr);
            }
            break;
        }
    }
}

/**
 * @brief Drains the hardware RX ring after a receive interrupt.
 *
 * Each packet is copied out before the hardware cursor is advanced, then either
 * consumed by DS Download Play command handling or queued back into Calico's
 * normal MLME/data tasks. Allocation failures block and retry, matching the
 * original Calico RX task behavior.
 */
void _mwlRxEndTask(void)
{
    for (;;) {
        // Read current read/write cursors
        unsigned rdcsr = MWL_REG(W_RXBUF_READCSR);
        unsigned wrcsr = s_mwlState.rx_wrcsr;
        armCompilerBarrier(); // avoid variable access reordering

        // If they are the same -> we're done
        if (rdcsr == wrcsr) {
            break;
        }

        // Set up RX transfer
        MWL_REG(W_RXBUF_RD_ADDR) = rdcsr*2;

        // Read hardware header
        MwlDataRxHdr rxhdr;
        _mwlRxRead(&rxhdr, sizeof(rxhdr));

        // Retrieve info from hardware header
        MwlRxType pkt_type = (MwlRxType)(rxhdr.status&0xf);
        unsigned pkt_read_sz = (rxhdr.mpdu_len + 1) &~ 1;

        // Calculate offset to next packet
        unsigned pkt_end = (rdcsr*2 + sizeof(MwlDataRxHdr) + rxhdr.mpdu_len + 3) &~ 3;
        pkt_end = pkt_end < MWL_MAC_RAM_SZ ? pkt_end : (pkt_end - (MWL_MAC_RAM_SZ-s_mwlState.rx_pos));

        bool static_rx = false;
        NetBuf* pPacket = _mwlRxAllocPacket(pkt_type, pkt_read_sz, &static_rx);

        // Slurp packet into our memory
        _mwlRxRead(netbufGet(pPacket), pkt_read_sz);

        unsigned frame_len = rxhdr.mpdu_len;
        if (frame_len > pPacket->len) {
            frame_len = pPacket->len;
        }
        // Update read cursor
        MWL_REG(W_RXBUF_READCSR) = pkt_end/2;

        // Get pointer to 802.11 header
        bool has_dot11hdr = pPacket->len >= sizeof(WlanMacHdr);
        WlanMacHdr* dot11hdr = has_dot11hdr ? (WlanMacHdr*)netbufGet(pPacket) : NULL;
        bool is_host_cmd = has_dot11hdr && dot11hdr->fc.type == WlanFrameType_Data && _mwlRxIsHostCmd(netbufGet(pPacket), frame_len);

        _mwlRxDispatchFrame(&pPacket, &rxhdr, pkt_type, frame_len,
            has_dot11hdr, dot11hdr, is_host_cmd);

        // Free packet if necessary
        if (!static_rx && pPacket) {
            netbufFree(pPacket);
        }
    }
}

/**
 * @brief Processes queued management/control frames after `_mwlRxEndTask`.
 *
 * Association, authentication, and state-loss frames are delegated back to
 * Calico MLME. Successful association also records the local AID for filtering
 * DS Download Play parent commands addressed to this child.
 */
void _mwlRxMgmtCtrlTask(void)
{
    // Dequeue packet
    NetBuf* pPacket = netbufQueueRemoveOne(&s_mwlState.rx_mgmt);
    if (!pPacket) {
        return; // Shouldn't happen
    }

    // Pop 802.11 header
    MK_ASSUME(pPacket->len >= sizeof(WlanMacHdr));
    WlanMacHdr* dot11hdr = netbufPopHeaderType(pPacket, WlanMacHdr);

    switch (dot11hdr->fc.subtype) {
        default: {
            dietPrint("[RX] MGMT type %u\n", dot11hdr->fc.subtype);
            break;
        }

        case WlanMgmtType_AssocResp: {
            if (s_mwlState.mlme_state == MwlMlmeState_AssocBusy) {
                WlanAssocRespHdr* ar = (WlanAssocRespHdr*)netbufGet(pPacket);
                if (pPacket->len >= sizeof(WlanAssocRespHdr) && ar->status == 0) {
                    u16 aid = ar->aid & 0x07ff;
                    MWL_REG(W_AID_FULL) = aid;
                    MWL_REG(W_AID_LOW) = aid & 0x000f;
                    arm7_assoc_aid(aid);
                }
                _mwlMlmeHandleAssocResp(pPacket);
            }
            break;
        }

        case WlanMgmtType_Disassoc: {
            if (s_mwlState.status >= MwlStatus_Class3) {
                _mwlMlmeHandleStateLoss(pPacket, dot11hdr->fc.subtype);
            }
            break;
        }

        case WlanMgmtType_Auth: {
            if (s_mwlState.mlme_state == MwlMlmeState_AuthBusy) {
                _mwlMlmeHandleAuth(pPacket);
            }
            break;
        }

        case WlanMgmtType_Deauth: {
            if (s_mwlState.status >= MwlStatus_Class2) {
                _mwlMlmeHandleStateLoss(pPacket, dot11hdr->fc.subtype);
            }
            break;
        }
    }

    // Free this packet and refire this task if there are more
    netbufFree(pPacket);
    if (s_mwlState.rx_mgmt.next) {
        _mwlPushTask(MwlTask_RxMgmtCtrlFrame);
    }
}

/**
 * @brief Delivers queued data frames to DS Download Play and Calico callbacks.
 *
 * The raw 802.11 bytes are first offered to the DS Download Play command path,
 * then the NetBuf is forwarded to Calico's MLME data callback or freed when no
 * callback is registered.
 */
void _mwlRxDataTask(void)
{
    // Dequeue packet
    NetBuf* pPacket = netbufQueueRemoveOne(&s_mwlState.rx_data);
    if (!pPacket) {
        return; // Shouldn't happen
    }

    // Forward to callback
    if (pPacket->len > sizeof(WlanMacHdr)) {
        unsigned raw_len = pPacket->len;
        if (raw_len > 4) raw_len -= 4;
        arm7_mp_cmd_frame(netbufGet(pPacket), raw_len, NULL);
    }

    if (s_mwlState.mlme_cb.maData) {
        s_mwlState.mlme_cb.maData(pPacket);
    } else {
        netbufFree(pPacket);
    }

    // Refire this task if there are more
    if (s_mwlState.rx_data.next) {
        _mwlPushTask(MwlTask_RxDataFrame);
    }
}

/**
 * @brief Frees all queued RX packets when scanning or association state resets.
 *
 * This prevents Calico-owned management or data NetBufs from leaking across a
 * transition back to DS Download Play scanning.
 */
void _mwlRxQueueClear(void)
{
    NetBuf* pPacket;

    while ((pPacket = netbufQueueRemoveOne(&s_mwlState.rx_mgmt))) {
        netbufFree(pPacket);
    }

    while ((pPacket = netbufQueueRemoveOne(&s_mwlState.rx_data))) {
        netbufFree(pPacket);
    }
}
