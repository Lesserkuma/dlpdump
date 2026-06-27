/**
 * @file atomic_file.c
 * @brief Shared same-directory temporary-file commit helper.
 */
#include "atomic_file.h"
#include "path.h"

#include <time.h>

/** @brief Checks whether a file path already exists. */
bool atomic_file_exists(const char *path) {
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) return false;
    fclose(f);
    return true;
}

/** @brief Builds a non-existing same-directory temporary path. */
bool atomic_file_make_temp_path(char *out,
                                   size_t out_size,
                                   const char *final_path,
                                   const char *suffix) {
    static unsigned temp_counter;
    char ext[48];
    if (!out || !out_size || !final_path || !suffix || !suffix[0]) return false;
    for (unsigned attempt = 0; attempt < 16u; attempt++) {
        unsigned counter = ++temp_counter;
        int n = snprintf(ext, sizeof(ext), "%s%08lx%04x",
                         suffix, (unsigned long)time(NULL), counter & 0xffffu);
        if (n < 0 || (size_t)n >= sizeof(ext)) return false;
        if (!path_replace_ext(out, out_size, final_path, ext)) return false;
        if (!atomic_file_exists(out)) return true;
    }
    out[0] = 0;
    return false;
}

/** @brief Removes an output transaction path or confirms it is absent. */
bool atomic_file_remove_path(const char *path) {
    if (!path || !path[0]) return true;
    if (remove(path) == 0) return true;
    return !atomic_file_exists(path);
}

/** @brief Removes a temporary path and clears the caller-owned buffer. */
void atomic_file_discard_temp(char *tmp_path) {
    if (!tmp_path || !tmp_path[0]) return;
    (void)atomic_file_remove_path(tmp_path);
    tmp_path[0] = 0;
}

/** @brief Writes and closes a temporary output file. */
bool atomic_file_write_temp(const char *final_path,
                               const char *suffix,
                               char *tmp_path,
                               size_t tmp_path_size,
                               AtomicFileWriter writer,
                               void *ctx) {
    if (!final_path || !tmp_path || !tmp_path_size || !writer) return false;
    tmp_path[0] = 0;
    if (atomic_file_exists(final_path)) return false;
    if (!atomic_file_make_temp_path(tmp_path, tmp_path_size, final_path, suffix)) return false;

    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        tmp_path[0] = 0;
        return false;
    }
    bool ok = writer(f, ctx);
    ok = fflush(f) == 0 && ok;
    ok = fclose(f) == 0 && ok;
    if (!ok) {
        atomic_file_discard_temp(tmp_path);
        return false;
    }
    return true;
}

/** @brief Commits a completed temporary file to its final path. */
bool atomic_file_commit_temp(const char *tmp_path, const char *final_path) {
    if (!tmp_path || !tmp_path[0] || !final_path) return false;
    if (atomic_file_exists(final_path)) {
        (void)atomic_file_remove_path(tmp_path);
        return false;
    }
    if (rename(tmp_path, final_path) != 0) {
        (void)atomic_file_remove_path(tmp_path);
        return false;
    }
    return true;
}

/** @brief Writes and commits a single output file through a temp path. */
bool atomic_file_write(const char *final_path,
                          const char *suffix,
                          AtomicFileWriter writer,
                          void *ctx) {
    char tmp_path[256];
    return atomic_file_write_temp(final_path, suffix, tmp_path, sizeof(tmp_path), writer, ctx) &&
           atomic_file_commit_temp(tmp_path, final_path);
}
