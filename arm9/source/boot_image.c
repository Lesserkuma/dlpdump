/**
 * @file boot_image.c
 * @brief Loads saved NDS/BCN files into the validated boot image structure.
 */
#include "boot_internal.h"
#include "download_rsa.h"
#include "handover_bss.h"

/** @brief Reads an exact byte range from a seekable NDS image file. */
static bool read_at(FILE *f, u32 off, void *dst, u32 len) {
    if (fseek(f, (long)off, SEEK_SET) != 0) return false;
    return fread(dst, 1, len, f) == len;
}

/** @brief Returns a u32-sized file length and rejects empty or too-large files. */
static bool file_size(FILE *f, u32 *out) {
    if (!f || !out) return false;
    *out = 0;
    if (fseek(f, 0, SEEK_END) != 0) return false;
    long pos = ftell(f);
    if (pos <= 0) return false;
    if ((unsigned long)pos > 0xfffffffful) return false;
    *out = (u32)pos;
    return true;
}

/** @brief Releases section buffers owned by a loaded boot image. */
void boot_image_free(BootImage *img) {
    if (!img) return;
    for (unsigned i = 0; i < 3; i++) {
        free(img->sec[i].data);
        img->sec[i].data = NULL;
    }
}

/** @brief Extracts the four-byte game code from the loaded NDS header section. */
u32 boot_game_code(const BootImage *img) {
    if (!img || !img->sec[0].data || img->sec[0].size < ROM_HEADER_GAME_CODE_OFF + ROM_HEADER_GAME_CODE_BYTES) return 0;
    return le32(img->sec[0].data + ROM_HEADER_GAME_CODE_OFF);
}

/** @brief Allocates and reads one section described by the saved control frame. */
static bool load_section(FILE *f, u32 file_len, u32 off, Section *sec) {
    u32 end;
    if (!sec || !memory_map_add_u32(off, sec->size, &end) || end > file_len) return false;
    sec->data = (u8*)malloc(sec->size ? sec->size : 1);
    if (!sec->data) return false;
    return read_at(f, off, sec->data, sec->size);
}

/** @brief Loads download parameters and reconstructs handover data from `.bcn`. */
static bool load_broadcast_context_file(const char *bcn_path,
                                        DownloadRsaFrame *out,
                                        u8 out_handover[BOOT_HANDOVER_SIZE],
                                        bool *handover_valid) {
    if (!bcn_path || !bcn_path[0] || !out || !out_handover || !handover_valid) return false;
    memset(out_handover, 0, BOOT_HANDOVER_SIZE);
    *handover_valid = false;

    FILE *f = fopen(bcn_path, "rb");
    if (!f) return false;

    bool ok = false;
    if (fseek(f, 0, SEEK_END) == 0 && ftell(f) >= (long)BOOT_BCN_CONTEXT_SIZE &&
        fseek(f, -(long)BOOT_BCN_CONTEXT_SIZE, SEEK_END) == 0) {
        u8 context[BOOT_BCN_CONTEXT_SIZE];
        if (fread(context, 1, sizeof(context), f) == sizeof(context)) {
            u32 game_group_id = le32(context + HANDOVER_BCN_GAME_GROUP_ID_OFF);
            if (le32(context + HANDOVER_BCN_RESERVED_OFF) != 0) {
                fclose(f);
                return false;
            }
            memcpy(out->download_parameter, context + HANDOVER_BCN_DOWNLOAD_PARAMETER_OFF, BOOT_DOWNLOAD_PARAMETER_SIZE);
            handover_bss_write_minimal(out_handover, game_group_id);
            *handover_valid = handover_bss_valid(out_handover);
            ok = true;
        }
    }
    fclose(f);
    return ok;
}

/** @brief Rebuilds the RSA-style control frame from NDS header plus sidecar. */
static bool load_control_frame(FILE *f, u32 rsa_off, const char *bcn_path,
                               const u8 header[ROM_HEADER_READ_BYTES], DownloadRsaFrame *out,
                               u8 handover[BOOT_HANDOVER_SIZE],
                               bool *handover_valid) {
    memset(out, 0, sizeof(*out));

    out->arm9_entrypoint = le32(header + ROM_HEADER_ARM9_ENTRY_OFF);
    out->arm7_entrypoint = le32(header + ROM_HEADER_ARM7_ENTRY_OFF);

    out->section[0].staging_addr = BOOT_FIXED_HEADER_ADDR;
    out->section[0].load_addr = BOOT_FIXED_HEADER_ADDR;
    out->section[0].size = BOOT_HEADER_SECTION_SIZE;
    out->section[0].flags = 0;
    out->section[1].staging_addr = le32(header + ROM_HEADER_ARM9_LOAD_OFF);
    out->section[1].load_addr = le32(header + ROM_HEADER_ARM9_LOAD_OFF);
    out->section[1].size = le32(header + ROM_HEADER_ARM9_SIZE_OFF);
    out->section[1].flags = 0;
    out->section[2].staging_addr = le32(header + ROM_HEADER_ARM7_LOAD_OFF);
    out->section[2].load_addr = le32(header + ROM_HEADER_ARM7_LOAD_OFF);
    out->section[2].size = le32(header + ROM_HEADER_ARM7_SIZE_OFF);
    out->section[2].flags = 1;

    if (!read_at(f, rsa_off, out->signature_id, ROM_RSA_SIGNATURE_BLOCK_BYTES)) return false;
    if (!load_broadcast_context_file(bcn_path, out, handover, handover_valid)) return false;
    return download_rsa_matches_nds_header(out, header, BOOT_HEADER_SECTION_SIZE,
                                             out->section[1].size, out->section[2].size);
}

/** @brief Loads and validates the saved RSA/control data referenced by the header. */
static bool load_saved_control_frame(FILE *f, u32 file_len, const char *bcn_path,
                                     const u8 header[ROM_HEADER_READ_BYTES],
                                     DownloadRsaFrame *out,
                                     u8 handover[BOOT_HANDOVER_SIZE],
                                     bool *handover_valid) {
    if (!out || !handover || !handover_valid) return false;
    *handover_valid = false;

    u32 rsa_off = le32(header + ROM_HEADER_RSA_OFFSET_OFF);
    u32 rsa_end;
    if (!memory_map_add_u32(rsa_off, ROM_RSA_SIGNATURE_BLOCK_BYTES, &rsa_end) || rsa_end > file_len) return false;

    return load_control_frame(f, rsa_off, bcn_path, header, out, handover,
                              handover_valid);
}

/**
 * @brief Loads a saved `.nds` plus matching `.bcn` into a bootable image object.
 *
 * On success, `img->sec[]` owns heap buffers for header, ARM9 and ARM7 sections,
 * and `img->copy_control` points at those staging buffers. On failure, no
 * partially allocated section memory is left owned by `img`.
 */
bool boot_load_nds_image(const char *path, const char *bcn_path,
                           BootImage *img) {
    if (!path || !bcn_path || !img) return false;
    memset(img, 0, sizeof(*img));

    FILE *f = fopen(path, "rb");
    if (!f) return false;
    u32 len = 0;
    if (!file_size(f, &len)) {
        fclose(f);
        return false;
    }
    u8 header[ROM_HEADER_READ_BYTES];
    bool ok = len >= sizeof(header) && read_at(f, 0, header, sizeof(header));
    if (!ok) {
        fclose(f);
        return false;
    }

    u32 off9 = le32(header + ROM_HEADER_ARM9_ROM_OFF_OFF);
    u32 off7 = le32(header + ROM_HEADER_ARM7_ROM_OFF_OFF);

    if (!load_saved_control_frame(f, len, bcn_path, header, &img->control,
                                  img->handover, &img->handover_valid)) {
        fclose(f);
        return false;
    }
    for (unsigned i = 0; i < 3; i++) {
        img->sec[i].load_addr = img->control.section[i].load_addr;
        img->sec[i].size = img->control.section[i].size;
        img->sec[i].flags = img->control.section[i].flags;
    }

    ok = load_section(f, len, 0, &img->sec[0]) &&
         load_section(f, len, off9, &img->sec[1]) &&
         load_section(f, len, off7, &img->sec[2]);
    if (!ok) {
        fclose(f);
        boot_image_free(img);
        return false;
    }

    img->copy_control = img->control;
    for (unsigned i = 0; i < 3; i++) {
        img->sec[i].staging_addr = (u32)img->sec[i].data;
        img->copy_control.section[i].staging_addr = img->sec[i].staging_addr;
        img->copy_control.section[i].load_addr = img->sec[i].load_addr;
        img->copy_control.section[i].size = img->sec[i].size;
        img->copy_control.section[i].flags = img->sec[i].flags;
    }

    fclose(f);
    return true;
}
