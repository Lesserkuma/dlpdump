// SPDX-License-Identifier: ZPL-2.1
// SPDX-FileCopyrightText: Copyright fincs, devkitPro
/**
 * @file mwl_mgmt_ext.c
 * @brief Calico-derived management-frame builders tuned for DS Download Play.
 *
 * This file follows Calico `source/dev/mwl/mwl_mgmt_frame.16.c` for NetBuf
 * ownership and 802.11 frame construction. Local changes keep the supported
 * rate set and association fields compatible with DS Download Play parents.
 */
#include "arm7_internal.h"

#include <string.h>

#define WLAN_CAP_ESS             0x0001u
#define WLAN_CAP_PRIVACY         0x0010u
#define WLAN_CAP_SHORT_PREAMBLE  0x0020u

/**
 * @brief Allocates an empty Calico TX NetBuf for one management frame.
 */
static NetBuf *mgmt_alloc(void) {
    NetBuf *nb = netbufAlloc(0, 0x100, NetBufPool_Tx);
    if (nb) {
        nb->pos = 0;
        nb->len = 0;
    }
    return nb;
}

/**
 * @brief Initializes the common 802.11 management header.
 *
 * The target is copied to receiver and BSSID fields so probe/auth/assoc/deauth
 * frames match the parent BSSID selected by the scanner.
 */
static WlanMacHdr *mgmt_init_hdr(NetBuf *nb, u16 fc, const void *target) {
    WlanMacHdr *hdr = (WlanMacHdr*)netbufGet(nb);
    memset(hdr, 0, sizeof(*hdr));
    hdr->fc.value = fc;
    memcpy(hdr->rx_addr, target, 6);
    memcpy(hdr->tx_addr, mwlGetCalibData()->mac_addr, 6);
    memcpy(hdr->xtra_addr, target, 6);
    return hdr;
}

/**
 * @brief Reserves the management body and returns its write pointer.
 */
static u8 *mgmt_body(NetBuf *nb, unsigned body_len) {
    nb->len = sizeof(WlanMacHdr) + body_len;
    return (u8*)netbufGet(nb) + sizeof(WlanMacHdr);
}

/**
 * @brief Appends one 802.11 information element and returns the next pointer.
 */
static u8 *mgmt_append_ie(u8 *p, u8 id, const void *data, unsigned len) {
    *p++ = id;
    *p++ = (u8)len;
    if (len) {
        memcpy(p, data, len);
        p += len;
    }
    return p;
}

/**
 * @brief Mirrors one locally transmitted management frame into active PCAP capture.
 */
static void mgmt_mirror_tx_frame(NetBuf *nb) {
    if (!g_raw_capture_enabled || !nb) return;
    arm7_push_raw_event(RAW_TX, netbufGet(nb), nb->len);
}

/**
 * @brief Builds a probe request for Calico MLME scanning.
 *
 * Unlike Calico's default helper, this local version advertises only the basic
 * 1/2 Mbps rates used by DS Download Play discovery instead of fake CCK rates.
 */
NetBuf *_mwlMgmtMakeProbeReq(const void *bssid, const char *ssid, unsigned ssid_len) {
    static const u8 basic_probe_rates[] = { 0x82, 0x84 };
    static const u8 broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

    if (ssid_len > WLAN_MAX_SSID_LEN) ssid_len = WLAN_MAX_SSID_LEN;

    NetBuf *nb = mgmt_alloc();
    if (!nb) return NULL;

    const void *target = bssid ? bssid : broadcast;
    mgmt_init_hdr(nb, 0x0040, target);

    unsigned body_len = 2 + ssid_len + 2 + sizeof(basic_probe_rates);
    u8 *p = mgmt_body(nb, body_len);
    p = mgmt_append_ie(p, WlanEid_SSID, ssid, ssid_len);
    mgmt_append_ie(p, WlanEid_SupportedRates, basic_probe_rates, sizeof(basic_probe_rates));
    return nb;
}

/**
 * @brief Builds an authentication frame and optional challenge-text response.
 */
NetBuf *_mwlMgmtMakeAuth(const void *target, WlanAuthHdr const *auth_hdr,
                         const void *chal_text, unsigned chal_len) {
    NetBuf *nb = mgmt_alloc();
    if (!nb) return NULL;

    mgmt_init_hdr(nb, 0x00b0, target);

    unsigned body_len = sizeof(WlanAuthHdr);
    if (chal_len) body_len += 2 + chal_len;

    u8 *p = mgmt_body(nb, body_len);
    memcpy(p, auth_hdr, sizeof(*auth_hdr));
    p += sizeof(*auth_hdr);
    if (chal_len) mgmt_append_ie(p, WlanEid_ChallengeText, chal_text, chal_len);
    mgmt_mirror_tx_frame(nb);
    return nb;
}

/**
 * @brief Builds an association request for the selected DS Download Play parent.
 *
 * `fake_cck_rates` preserves Calico's optional 5.5/11 Mbps advertisement path,
 * but DS Download Play callers normally keep it disabled.
 */
NetBuf *_mwlMgmtMakeAssocReq(const void *target, bool fake_cck_rates) {
    NetBuf *nb = mgmt_alloc();
    if (!nb) return NULL;

    mgmt_init_hdr(nb, 0x0000, target);

    u8 rates[4] = {0x82, 0x84, 0x0b, 0x16};
    unsigned rates_len = fake_cck_rates ? 4 : 2;
    unsigned ssid_len = s_mwlState.ssid_len;
    if (ssid_len > WLAN_MAX_SSID_LEN) ssid_len = WLAN_MAX_SSID_LEN;

    unsigned body_len = sizeof(WlanAssocReqHdr) + 2 + ssid_len + 2 + rates_len;
    u8 *p = mgmt_body(nb, body_len);

    WlanAssocReqHdr *ar = (WlanAssocReqHdr*)p;
    ar->capabilities = WLAN_CAP_ESS | WLAN_CAP_SHORT_PREAMBLE;
    if (s_mwlState.wep_enabled) ar->capabilities |= WLAN_CAP_PRIVACY;
    ar->interval = 1;
    p += sizeof(*ar);

    p = mgmt_append_ie(p, WlanEid_SSID, s_mwlState.ssid, ssid_len);
    mgmt_append_ie(p, WlanEid_SupportedRates, rates, rates_len);
    mgmt_mirror_tx_frame(nb);
    return nb;
}

/**
 * @brief Builds a deauthentication frame with a little-endian reason code.
 */
NetBuf *_mwlMgmtMakeDeauth(const void *target, unsigned reason) {
    NetBuf *nb = mgmt_alloc();
    if (!nb) return NULL;

    mgmt_init_hdr(nb, 0x00c0, target);
    u8 *p = mgmt_body(nb, sizeof(u16));
    p[0] = (u8)reason;
    p[1] = (u8)(reason >> 8);
    mgmt_mirror_tx_frame(nb);
    return nb;
}
