/**
 * @file download_rsa.c
 * @brief Parses RSA control frames and allocates transfer buffers.
 */
#include "download_internal.h"
#include "download_rsa.h"

/**
 * @brief Frees dynamic packet buffers owned by a download state object.
 */
void download_free_allocations(Download *download) {
    if (!download) return;
    for (unsigned i = 0; i < 3; i++) {
        free(download->sec[i].data);
        download->sec[i].data = NULL;
    }
    free(download->received_bits);
    download->received_bits = NULL;
}

/**
 * @brief Clears transfer-only fields after freeing or rejecting RSA state.
 */
void download_reset_transfer_fields(Download *download) {
    if (!download) return;
    download->have_rsa = false;
    download->rsa_ack_deferred = false;
    download->transfer_started = false;
    download->transfer_wait_start_time = 0;
    download->all_packets_time = 0;
    download->data_start_time = 0;
    download->save_progress_valid = false;
    download->save_percent = 0;
    download->received_packets = 0;
    download->total_packets = 0;
}

/**
 * @brief Frees all active transfer buffers and clears transfer counters.
 */
void download_free_buffers(void) {
    download_free_allocations(&g_download);
    download_reset_transfer_fields(&g_download);
}

/**
 * @brief Seeds a candidate download object from the current association state.
 */
static void init_candidate_from_current(Download *candidate, const u8 *data) {
    const ContentSlot *slot = g_download.slot;

    memset(candidate, 0, sizeof(*candidate));
    candidate->active = true;
    candidate->scan_slot = g_download.scan_slot;
    if (slot) {
        candidate->slot_snapshot = *slot;
        candidate->slot = &candidate->slot_snapshot;
    }
    candidate->id = g_download.id;
    candidate->expected_file_no = g_download.expected_file_no;
    candidate->assoc_aid = g_download.assoc_aid;
    candidate->start_temporary_group_id = g_download.start_temporary_group_id;
    candidate->user = g_download.user;
    candidate->beacon_pcap_written = g_download.beacon_pcap_written;
    candidate->ipc_drop_baseline = g_download.ipc_drop_baseline;
    candidate->ipc_drop_seen = g_download.ipc_drop_seen;
    candidate->last_comm_time = g_download.last_comm_time;
    candidate->start_time = g_download.start_time;
    memcpy(candidate->output_base, g_download.output_base, sizeof(candidate->output_base));
    candidate->output_base[sizeof(candidate->output_base) - 1u] = 0;
    memcpy(&candidate->rsa, data, DOWNLOAD_RSA_FRAME_SIZE);
}

/**
 * @brief Normalizes parent/child packet limits from beacon and RSA context.
 */
static bool configure_candidate_packet_limits(Download *candidate) {
    u16 raw_parent_max = candidate->slot && candidate->slot->parent_packet_max_bytes
        ? candidate->slot->parent_packet_max_bytes
        : g_download.parent_packet_max_bytes;
    if (!beacon_parent_packet_max_normalize(raw_parent_max, &candidate->parent_packet_max_bytes)) {
        return false;
    }
    candidate->child_packet_max_bytes = candidate->slot && candidate->slot->child_packet_max_bytes
        ? candidate->slot->child_packet_max_bytes
        : CHILD_MAX_DEFAULT;
    return download_rsa_parent_payload_bytes(candidate->parent_packet_max_bytes,
                                                &candidate->max_payload);
}

/**
 * @brief Allocates target section buffers after centralized RSA validation.
 */
static bool allocate_candidate_sections(Download *candidate) {
    for (unsigned i = 0; i < 3; i++) {
        DownloadSectionEntry *e = &candidate->rsa.section[i];
        candidate->sec[i].staging_addr = e->staging_addr;
        candidate->sec[i].load_addr = e->load_addr;
        candidate->sec[i].size = e->size;
        candidate->sec[i].flags = e->flags;
        candidate->sec[i].data = (u8*)malloc(e->size ? e->size : 1);
        if (!candidate->sec[i].data) {
            return false;
        }
        memset(candidate->sec[i].data, 0, e->size);
    }
    return true;
}

/**
 * @brief Allocates the received-packet bitset and initializes transfer timing.
 */
static bool allocate_candidate_received_bits(Download *candidate) {
    candidate->stats.expected_data_packets = candidate->total_packets;
    candidate->received_bits = (u8*)calloc((candidate->total_packets + 7u) / 8u, 1);
    if (!candidate->received_bits) {
        return false;
    }
    candidate->received_packets = 0;
    candidate->have_rsa = true;
    candidate->transfer_started = false;
    candidate->transfer_wait_start_time = download_now_seconds();
    candidate->all_packets_time = 0;
    return true;
}

/**
 * @brief Resets debug throttling state for a newly accepted RSA frame.
 */
static void reset_rsa_debug_state(void) {
    s_debug_last_received_log = 0;
    s_debug_last_final_missing = 0xffff;
    s_debug_last_final_received = 0xffff;
    s_debug_last_correction_missing = 0xffff;
    s_debug_last_correction_received = 0xffff;
    s_debug_duplicate_mismatches = 0;
    s_next_missing_cursor = 0xffffffffu;
    s_last_signal_update_frame = 0;
}

/**
 * @brief Validates an RSA control frame and installs it as active transfer state.
 *
 * The candidate object is only moved into `g_download` after signature,
 * section, packet-count and allocation checks have all succeeded.
 */
bool download_parse_rsa_frame(const u8 *data, unsigned len) {
    if (len < DOWNLOAD_RSA_FRAME_SIZE) return false;

    Download candidate;
    init_candidate_from_current(&candidate, data);
    if (!configure_candidate_packet_limits(&candidate) ||
        !download_rsa_frame_valid(&candidate.rsa,
                                     candidate.parent_packet_max_bytes,
                                     &candidate.total_packets) ||
        !allocate_candidate_sections(&candidate) ||
        !allocate_candidate_received_bits(&candidate)) {
        download_free_allocations(&candidate);
        return false;
    }

    download_free_buffers();
    g_download = candidate;
    if (g_download.slot) g_download.slot = &g_download.slot_snapshot;
    reset_rsa_debug_state();
    debug_log("rsa ok max_payload=%u total=%u sections=%lu,%lu,%lu parent_max=%u child_max=%u",
               g_download.max_payload, g_download.total_packets,
               (unsigned long)g_download.sec[0].size,
               (unsigned long)g_download.sec[1].size,
               (unsigned long)g_download.sec[2].size,
               g_download.parent_packet_max_bytes, g_download.child_packet_max_bytes);
    return true;
}
