/**
 * @file file.c
 * @brief Saves verified NDS/BCN outputs and adds the optional text report.
 */
#include "file_internal.h"
#include "handover_bss.h"

/**
 * @brief Creates the output directory if it is missing.
 */
bool file_ensure_output_dir(void) {
    if (mkdir(OUTPUT_DIR, 0777) == 0) return true;
    if (errno != EEXIST) return false;

    struct stat st;
    if (stat(OUTPUT_DIR, &st) != 0) return false;
#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#endif
    return S_ISDIR(st.st_mode);
}

/**
 * @brief Tracks byte progress while streaming one output file.
 */
typedef struct {
    u32 written;
    u32 total;
    unsigned last_percent;
} SaveProgress;

static u64 s_report_hash_bytes;
static u64 s_report_hash_total;

#define FILE_IO_CHUNK   32768u
#define FILE_HASH_CHUNK 4096u

static u8 s_zero_fill[FILE_IO_CHUNK];
static char s_file_buffer[FILE_IO_CHUNK];
static u8 s_hash_buffer[FILE_HASH_CHUNK];

typedef bool (*FileWriter)(FILE *f, void *ctx);

/**
 * @brief Context passed to the atomic BCN sidecar writer.
 */
typedef struct {
    const u8 *fixed;
    unsigned fixed_len;
    const u8 *context;
    unsigned context_len;
} BeaconWriteCtx;

/**
 * @brief Context passed to the atomic text-report writer.
 */
typedef struct {
    const Download *dl;
    const char *base_name;
    const u8 *nds_header;
    u32 nds_header_size;
    const FileDigest *nds_digest;
    const FileDigest *bcn_digest;
} ReportWriteCtx;

/**
 * @brief Initializes save-progress tracking for a file hash pass.
 */
static void report_hash_progress_begin(u64 total) {
    s_report_hash_total = total;
    s_report_hash_bytes = 0;
}

/**
 * @brief Advances UI progress while hashing an output file.
 */
static void report_hash_progress_step(u32 bytes) {
    if (!s_report_hash_total) return;
    s_report_hash_bytes += (u64)bytes;
    if (s_report_hash_bytes > s_report_hash_total) s_report_hash_bytes = s_report_hash_total;
    unsigned percent = (unsigned)((s_report_hash_bytes * 100u) / s_report_hash_total);
    download_update_save_progress(percent);
}

/**
 * @brief Returns the byte length of an open file without changing its final position.
 */
static u64 file_size_bytes(const char *path) {
    if (!path || !path[0]) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    long long size = ftell(f);
    fclose(f);
    if (size < 0) return 0;
    return (u64)size;
}

/**
 * @brief Maps a save phase and byte counter into UI progress percent.
 */
static void report_save_progress(SaveProgress *progress) {
    if (!progress || !progress->total) return;
    unsigned percent = (progress->written >= progress->total) ? 100u :
                       (unsigned)((progress->written * 100u) / progress->total);
    if (percent != progress->last_percent) {
        progress->last_percent = percent;
        download_update_save_progress(percent);
    }
}

typedef enum {
    FILE_HASH_FAILED = 0,
    FILE_HASH_OK,
    FILE_HASH_SKIPPED,
} FileHashResult;

/**
 * @brief Returns whether the current save flow asked to skip optional hashing.
 */
static bool report_hash_skip_requested(void) {
    scanKeys();
    return (keysDown() & KEY_B) != 0;
}

/**
 * @brief Hashes one saved output file while reporting progress and honoring skip requests.
 */
static FileHashResult hash_file(const char *path, FileDigest *digest) {
    if (!path || !digest) return FILE_HASH_FAILED;
    FILE *f = fopen(path, "rb");
    if (!f) return FILE_HASH_FAILED;

    HashCtx hash;
    hash_init(&hash);
    bool ok = true;
    for (;;) {
        if (report_hash_skip_requested()) {
            fclose(f);
            return FILE_HASH_SKIPPED;
        }

        size_t n = fread(s_hash_buffer, 1, sizeof(s_hash_buffer), f);
        if (n) {
            hash_update(&hash, s_hash_buffer, (u32)n);
            report_hash_progress_step((u32)n);
        }
        if (n < sizeof(s_hash_buffer)) {
            if (ferror(f)) ok = false;
            break;
        }
    }

    if (ok) hash_final(&hash, digest);
    ok = fclose(f) == 0 && ok;
    return ok ? FILE_HASH_OK : FILE_HASH_FAILED;
}

/**
 * @brief Writes a bounded byte span to a file and advances save progress.
 */
static bool write_bytes(FILE *f, const void *data, u32 count, SaveProgress *progress) {
    const u8 *p = (const u8*)data;
    while (count) {
        u32 n = count > FILE_IO_CHUNK ? FILE_IO_CHUNK : count;
        if (fwrite(p, 1, n, f) != n) return false;
        if (progress) {
            progress->written += n;
            report_save_progress(progress);
        }
        p += n;
        count -= n;
    }
    return true;
}

/**
 * @brief Writes zero padding bytes to a file and advances save progress.
 */
static bool write_zeros(FILE *f, u32 count, SaveProgress *progress) {
    while (count) {
        u32 n = count > sizeof(s_zero_fill) ? sizeof(s_zero_fill) : count;
        if (fwrite(s_zero_fill, 1, n, f) != n) return false;
        if (progress) {
            progress->written += n;
            report_save_progress(progress);
        }
        count -= n;
    }
    return true;
}

/** @brief Writes a file via temp path, flush, close and final rename. */
static bool write_temp_file_for_final(const char *final_path, char *tmp_path, size_t tmp_path_size,
                                      FileWriter writer, void *ctx) {
    return atomic_file_write_temp(final_path, ".tmp", tmp_path, tmp_path_size, writer, ctx);
}

/** @brief Commits a fully written temp file without replacing existing output. */
static bool commit_temp_file(const char *tmp_path, const char *final_path) {
    return atomic_file_commit_temp(tmp_path, final_path);
}

/** @brief Writes and immediately commits one output file through a temp path. */
static bool write_file_atomically(const char *final_path, FileWriter writer, void *ctx) {
    return atomic_file_write(final_path, ".tmp", writer, ctx);
}

/**
 * @brief Returns whether two file ranges overlap.
 */
static bool ranges_overlap(u32 a0, u32 a1, u32 b0, u32 b1) {
    return a0 < b1 && b0 < a1;
}

/**
 * @brief Checks rebuilt NDS header offsets against RSA section layout.
 */
static bool header_offsets_sane(const u8 *h, u32 hlen, u32 arm9_size, u32 arm7_size, u32 *o9, u32 *o7) {
    if (hlen < ROM_HEADER_ARM7_SIZE_OFF + 4u) return false;
    *o9 = le32(h + ROM_HEADER_ARM9_ROM_OFF_OFF);
    *o7 = le32(h + ROM_HEADER_ARM7_ROM_OFF_OFF);
    if (*o9 < ROM_HEADER_ARM9_ROM_OFF_MIN || *o7 < ROM_HEADER_ARM7_ROM_OFF_MIN) return false;
    u32 e9 = *o9 + arm9_size;
    u32 e7 = *o7 + arm7_size;
    if (e9 < *o9 || e7 < *o7) return false;
    if (ranges_overlap(0, align_up_u32(hlen, ROM_FILE_ALIGNMENT_BYTES), *o9, e9)) return false;
    if (ranges_overlap(0, align_up_u32(hlen, ROM_FILE_ALIGNMENT_BYTES), *o7, e7)) return false;
    if (ranges_overlap(*o9, e9, *o7, e7)) return false;
    return true;
}

/**
 * @brief One sorted ROM output segment used while rebuilding the NDS image.
 */
typedef struct {
    u32 off;
    u32 len;
    const void *data;
} WriteSeg;

#define FILE_HEADER_SECTION_SIZE ROM_HEADER_MIN_BYTES
#define FILE_FIXED_HEADER_ADDR   HANDOVER_FIXED_HEADER_ADDR
#define FILE_USER_PARAMS_SIZE    HANDOVER_DOWNLOAD_PARAMETER_BYTES
#define FILE_BCN_RESERVED_SIZE   HANDOVER_BCN_RESERVED_BYTES
#define FILE_BCN_CONTEXT_SIZE    HANDOVER_BCN_CONTEXT_BYTES
/**
 * @brief Validates that RSA control metadata matches the rebuilt NDS header.
 */
static bool control_matches_nds_header(const Download *dl, const u8 *header,
                                       u32 header_size, u32 arm9_size,
                                       u32 arm7_size) {
    return dl && dl->have_rsa &&
           download_rsa_matches_nds_header(&dl->rsa, header, header_size,
                                              arm9_size, arm7_size);
}

/**
 * @brief Writes header, ARM9 and ARM7 segments at their target file offsets.
 */
static bool write_segments(FILE *f, const WriteSeg *seg, unsigned count, u32 pos, SaveProgress *progress) {
    for (unsigned i = 0; i < count; i++) {
        if (seg[i].off < pos) return false;
        if (seg[i].off > pos) {
            if (!write_zeros(f, seg[i].off - pos, progress)) return false;
            pos = seg[i].off;
        }
        if (seg[i].len && !write_bytes(f, seg[i].data, seg[i].len, progress)) return false;
        pos += seg[i].len;
    }
    return true;
}

/**
 * @brief Selects the game-group ID used in saved beacon context.
 */
static u32 slot_game_group_id_for_broadcast_context(const ContentSlot *slot) {
    if (!slot) return 0;

    if (slot->handover_valid) {
        u32 game_group_id = handover_bss_game_group_id(slot->handover);
        if (game_group_id) return game_group_id;
    }

    return slot->game_group_id;
}

/** @brief Streams a broadcast-context sidecar payload. */
static bool write_beacon_payload(FILE *f, void *opaque) {
    BeaconWriteCtx *ctx = (BeaconWriteCtx*)opaque;
    return ctx &&
           write_bytes(f, ctx->fixed, ctx->fixed_len, NULL) &&
           write_bytes(f, ctx->context, ctx->context_len, NULL);
}

/**
 * @brief Writes the BCN handover file to a same-directory temp path without committing it.
 */
static bool write_beacon_temp_file(const ContentSlot *slot, const char *final_path,
                                   char *tmp_path, size_t tmp_path_size,
                                   const u8 user_params[FILE_USER_PARAMS_SIZE],
                                   u32 game_group_id) {
    if (!slot || !user_params || !final_path || !tmp_path || !tmp_path_size) return false;
    u8 fixed[BEACON_FIXED_INFO_MAX];
    unsigned fixed_len = 0;
    if (!meta_build_fixed_info(slot, fixed, &fixed_len) || !fixed_len) {
        debug_log("save bcn metadata failed attr=%u complete=%u info=%u frag_mask=%04x frame_mask=%04x len0=%u total0=%u",
                   (unsigned)slot->beacon_data_attr,
                   slot->complete ? 1u : 0u,
                   slot->info_valid ? 1u : 0u,
                   slot->fragment_mask,
                   slot->beacon_frame_mask,
                   (unsigned)slot->fragment_len[0],
                   (slot->fragment_len[0] >= BEACON_FRAGMENT_HEADER_BYTES) ? (unsigned)slot->fragments[0][1] : 0u);
        return false;
    }

    u8 context[FILE_BCN_CONTEXT_SIZE];
    memset(context, 0, sizeof(context));
    stle32(context + HANDOVER_BCN_GAME_GROUP_ID_OFF, game_group_id);
    memcpy(context + HANDOVER_BCN_DOWNLOAD_PARAMETER_OFF, user_params, FILE_USER_PARAMS_SIZE);

    BeaconWriteCtx ctx = { fixed, fixed_len, context, sizeof(context) };
    return write_temp_file_for_final(final_path, tmp_path, tmp_path_size, write_beacon_payload, &ctx);
}

/**
 * @brief Complete temp/final path and segment plan for committing a dump set.
 */
typedef struct {
    u8 *header;
    u32 header_write;
    u32 output_end;
    WriteSeg seg[3];
    char fallback[OUTPUT_BASE_BYTES];
    const char *base_name;
    char nds_path[256];
    char bcn_path[256];
    char txt_path[256];
} FileSavePlan;

/** @brief Sorts output segments by ROM offset before streaming them. */
static void sort_write_segments(WriteSeg *seg, unsigned count) {
    for (unsigned i = 0; i < count; i++) {
        for (unsigned j = i + 1; j < count; j++) {
            if (seg[j].off < seg[i].off) {
                WriteSeg tmp = seg[i];
                seg[i] = seg[j];
                seg[j] = tmp;
            }
        }
    }
}

/** @brief Validates the RSA signature area placement in the output image. */
static bool rsa_signature_range_sane(u32 rsa_off, u32 header_write,
                                     u32 off9, u32 arm9_end,
                                     u32 off7, u32 arm7_end,
                                     u32 *rsa_end) {
    *rsa_end = rsa_off + ROM_RSA_SIGNATURE_BLOCK_BYTES;
    return rsa_off >= ROM_FILE_ALIGNMENT_BYTES &&
           *rsa_end >= rsa_off &&
           !ranges_overlap(0, header_write, rsa_off, *rsa_end) &&
           !ranges_overlap(off9, arm9_end, rsa_off, *rsa_end) &&
           !ranges_overlap(off7, arm7_end, rsa_off, *rsa_end);
}

/** @brief Builds the validated header copy and sorted section write plan. */
static bool prepare_save_plan(const Download *dl, FileSavePlan *plan) {
    const Section *hdrsec = &dl->sec[0];
    const Section *arm9 = &dl->sec[1];
    const Section *arm7 = &dl->sec[2];
    if (hdrsec->size < ROM_HEADER_ARM7_SIZE_OFF + 4u) return false;

    memset(plan, 0, sizeof(*plan));
    plan->header_write = hdrsec->size < ROM_HEADER_READ_BYTES
        ? ROM_HEADER_READ_BYTES
        : align_up_u32(hdrsec->size, ROM_FILE_ALIGNMENT_BYTES);
    plan->header = (u8*)calloc(1, plan->header_write);
    if (!plan->header) return false;
    memcpy(plan->header, hdrsec->data, hdrsec->size);

    u32 off9 = 0, off7 = 0;
    bool ok = header_offsets_sane(plan->header, hdrsec->size, arm9->size, arm7->size, &off9, &off7) &&
              control_matches_nds_header(dl, plan->header, hdrsec->size, arm9->size, arm7->size);
    u32 arm9_end = off9 + arm9->size;
    u32 arm7_end = off7 + arm7->size;
    u32 rsa_end = 0;
    u32 rsa_off = le32(plan->header + ROM_HEADER_RSA_OFFSET_OFF);
    ok = ok && rsa_signature_range_sane(rsa_off, plan->header_write, off9, arm9_end, off7, arm7_end, &rsa_end);
    if (!ok) {
        free(plan->header);
        plan->header = NULL;
        return false;
    }

    plan->output_end = plan->header_write;
    if (arm9_end > plan->output_end) plan->output_end = arm9_end;
    if (arm7_end > plan->output_end) plan->output_end = arm7_end;
    if (rsa_end > plan->output_end) plan->output_end = rsa_end;
    plan->seg[0] = (WriteSeg){ off9, arm9->size, arm9->data };
    plan->seg[1] = (WriteSeg){ off7, arm7->size, arm7->data };
    plan->seg[2] = (WriteSeg){ rsa_off, ROM_RSA_SIGNATURE_BLOCK_BYTES, dl->rsa.signature_id };
    sort_write_segments(plan->seg, ARRAY_COUNT(plan->seg));
    return true;
}

/** @brief Resolves the base name and output paths for NDS and BCN files. */
static bool resolve_save_paths(const Download *dl, FileSavePlan *plan) {
    plan->base_name = dl->output_base;
    if (!plan->base_name[0]) {
        if (!file_make_output_base(plan->fallback, sizeof(plan->fallback), dl->slot->title)) return false;
        plan->base_name = plan->fallback;
    }
    return path_make_output_file(plan->nds_path, sizeof(plan->nds_path), plan->base_name, ".nds") &&
           path_make_output_file(plan->bcn_path, sizeof(plan->bcn_path), plan->base_name, ".bcn") &&
           path_make_output_file(plan->txt_path, sizeof(plan->txt_path), plan->base_name, ".txt");
}

/** @brief Streams the NDS output image from the validated save plan. */
static bool write_nds_payload(FILE *f, void *ctx) {
    const FileSavePlan *plan = (const FileSavePlan*)ctx;
    if (!plan) return false;
    setvbuf(f, s_file_buffer, _IOFBF, sizeof(s_file_buffer));
    SaveProgress progress = {0, plan->output_end, 101u};
    download_update_save_progress(0);
    bool ok = write_bytes(f, plan->header, plan->header_write, &progress);
    ok = ok && write_segments(f, plan->seg, ARRAY_COUNT(plan->seg), plan->header_write, &progress);
    return ok;
}

/** @brief Writes the NDS output image to a same-directory temp path without committing it. */
static bool write_nds_temp_from_plan(FileSavePlan *plan, char *tmp_path, size_t tmp_path_size) {
    return plan && write_temp_file_for_final(plan->nds_path, tmp_path, tmp_path_size,
                                             write_nds_payload, plan);
}

/** @brief Streams the text report to an atomically committed temp file. */
static bool write_report_payload(FILE *f, void *ctx) {
    const ReportWriteCtx *r = (const ReportWriteCtx*)ctx;
    return r && report_write_to_file(f, r->dl, r->base_name,
                                    r->nds_header, r->nds_header_size,
                                    r->nds_digest, r->bcn_digest);
}

/** @brief Writes the report file from already computed digests. */
static bool write_report_file_from_plan(FileSavePlan *plan, const Download *dl,
                                        const FileDigest *nds_digest,
                                        const FileDigest *bcn_digest) {
    ReportWriteCtx ctx = {
        dl,
        plan->base_name,
        plan->header,
        plan->header_write,
        nds_digest,
        bcn_digest,
    };
    return write_file_atomically(plan->txt_path, write_report_payload, &ctx);
}

/** @brief Enters report mode and initializes progress for digest generation. */
static void begin_report_generation(const FileSavePlan *plan) {
    g_runState = RUN_CREATING_REPORT;
    g_download.save_progress_valid = true;
    g_download.save_percent = 0;
    report_hash_progress_begin(file_size_bytes(plan->nds_path) + file_size_bytes(plan->bcn_path));
    download_update_save_progress(0);
    ui_log("[%08lx] Creating Dump Report…", (unsigned long)g_download.id);
    ui_draw_now();
}

/** @brief Hashes one saved output file while honoring the report-skip shortcut. */
static bool hash_saved_file(const char *path, FileDigest *digest, bool *skipped) {
    FileHashResult result = hash_file(path, digest);
    if (result == FILE_HASH_SKIPPED) {
        *skipped = true;
        ui_log("[%08lx] Dump Report skipped.", (unsigned long)g_download.id);
        return true;
    }
    return result == FILE_HASH_OK;
}

/**
 * @brief Writes and commits verified NDS/BCN outputs, with report as best-effort.
 */
bool file_save_download(const Download *dl) {
    if (!dl || !dl->slot || !dl->sec[0].data || !dl->sec[1].data || !dl->sec[2].data) return false;
    FileSavePlan plan;
    FileDigest nds_digest;
    FileDigest bcn_digest;
    bool skipped = false;

    if (!prepare_save_plan(dl, &plan)) return false;
    memset(&bcn_digest, 0, sizeof(bcn_digest));
    memset(&nds_digest, 0, sizeof(nds_digest));
    char nds_tmp_path[256];
    char bcn_tmp_path[256];
    nds_tmp_path[0] = 0;
    bcn_tmp_path[0] = 0;

    bool ok = resolve_save_paths(dl, &plan) &&
              write_nds_temp_from_plan(&plan, nds_tmp_path, sizeof(nds_tmp_path));
    if (!ok) {
        debug_log("save nds failed");
        atomic_file_discard_temp(nds_tmp_path);
        free(plan.header);
        return false;
    }

    ok = write_beacon_temp_file(dl->slot, plan.bcn_path, bcn_tmp_path, sizeof(bcn_tmp_path),
                                dl->rsa.download_parameter,
                                slot_game_group_id_for_broadcast_context(dl->slot));
    if (!ok) {
        debug_log("save bcn failed");
        atomic_file_discard_temp(nds_tmp_path);
        atomic_file_discard_temp(bcn_tmp_path);
        free(plan.header);
        return false;
    }

    ok = commit_temp_file(bcn_tmp_path, plan.bcn_path);
    if (!ok) {
        debug_log("save bcn commit failed");
        atomic_file_discard_temp(nds_tmp_path);
        free(plan.header);
        return false;
    }

    ok = commit_temp_file(nds_tmp_path, plan.nds_path);
    if (!ok) {
        debug_log("save nds commit failed; removing committed bcn sidecar");
        (void)atomic_file_remove_path(plan.bcn_path);
        free(plan.header);
        return false;
    }

    begin_report_generation(&plan);
    bool report_ready =
        hash_saved_file(plan.nds_path, &nds_digest, &skipped) &&
        (skipped || hash_saved_file(plan.bcn_path, &bcn_digest, &skipped));
    if (report_ready && !skipped) {
        report_ready = write_report_file_from_plan(&plan, dl, &nds_digest, &bcn_digest);
    }
    if (!report_ready && !skipped) {
        ui_log("[%08lx] Dump Report failed; saving NDS/BCN only.", (unsigned long)g_download.id);
        debug_log("report failed; committing core outputs only");
    }
    free(plan.header);
    return true;
}
