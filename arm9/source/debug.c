/**
 * @file debug.c
 * @brief Writes per-dump debug logs through temporary files and atomic commit.
 */
#include "state.h"
#include "atomic_file.h"
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
#include "path.h"

#include <stdarg.h>
#include <time.h>

#if DEBUG_VERSION

static FILE *s_debug_file;
static char s_debug_buffer[32768];
static LogLevel s_debug_min_level = LOG_DEBUG;
static bool s_debug_error;
static char s_debug_final_path[256];
static char s_debug_temp_path[256];

/** @brief Returns the compact text prefix for a debug log level. */
static const char *debug_level_name(LogLevel level) {
    switch (level) {
        case LOG_ERROR: return "ERROR";
        case LOG_WARN: return "WARN";
        case LOG_INFO: return "INFO";
        case LOG_DEBUG: return "DEBUG";
        case LOG_TRACE: return "TRACE";
        default: return "DEBUG";
    }
}

/** @brief Writes one already-started debug log entry. */
static void debug_vlog(LogLevel level, const char *fmt, va_list ap) {
    if (!s_debug_file || !fmt || level > s_debug_min_level) return;

    if (fprintf(s_debug_file, "%lu %s ", (unsigned long)g_frameCounter, debug_level_name(level)) < 0 ||
        vfprintf(s_debug_file, fmt, ap) < 0 ||
        fputc('\n', s_debug_file) == EOF) {
        s_debug_error = true;
    }
}

/**
 * @brief Opens a temporary debug log for the current download attempt.
 */
bool debug_open(const char *base_name) {
    (void)debug_close();
    debug_discard();
    if (!base_name || !base_name[0]) return false;

    if (!path_make_output_file(s_debug_final_path, sizeof(s_debug_final_path), base_name, ".log")) return false;
    if (atomic_file_exists(s_debug_final_path)) return false;
    if (!atomic_file_make_temp_path(s_debug_temp_path, sizeof(s_debug_temp_path), s_debug_final_path, ".log.tmp")) return false;
    s_debug_file = fopen(s_debug_temp_path, "wb");
    if (!s_debug_file) return false;
    s_debug_error = false;
    setvbuf(s_debug_file, s_debug_buffer, _IOFBF, sizeof(s_debug_buffer));

    debug_log("debug log start base=%s", base_name);
    return true;
}

/**
 * @brief Sets the minimum severity level written to the debug log.
 */
void debug_set_min_level(LogLevel level) {
    if (level > LOG_TRACE) level = LOG_TRACE;
    s_debug_min_level = level;
}

/**
 * @brief Writes one formatted debug message when its level is enabled.
 */
void debug_log_level(LogLevel level, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    debug_vlog(level, fmt, ap);
    va_end(ap);
}

/**
 * @brief Writes one formatted informational debug message.
 */
void debug_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    debug_vlog(LOG_DEBUG, fmt, ap);
    va_end(ap);
}

/**
 * @brief Flushes and closes the temporary debug-log stream.
 */
bool debug_close(void) {
    bool ok = !s_debug_error;
    if (!s_debug_file) return ok;
    if (fflush(s_debug_file) != 0) ok = false;
    if (fclose(s_debug_file) != 0) ok = false;
    s_debug_file = NULL;
    if (!ok) s_debug_error = true;
    return ok;
}

/**
 * @brief Renames the temporary debug log to its final reserved path.
 */
bool debug_commit(void) {
    if (s_debug_file && !debug_close()) return false;
    if (s_debug_error || !s_debug_temp_path[0] || !s_debug_final_path[0]) return false;
    if (atomic_file_exists(s_debug_final_path)) {
        debug_discard();
        s_debug_error = true;
        return false;
    }
    if (!atomic_file_commit_temp(s_debug_temp_path, s_debug_final_path)) {
        debug_discard();
        s_debug_error = true;
        return false;
    }
    s_debug_temp_path[0] = 0;
    s_debug_final_path[0] = 0;
    return true;
}

/**
 * @brief Removes the temporary debug log after a failed attempt.
 */
void debug_discard(void) {
    if (s_debug_file) (void)debug_close();
    atomic_file_discard_temp(s_debug_temp_path);
    s_debug_final_path[0] = 0;
}

/**
 * @brief Returns whether debug-log open, write, close or commit failed.
 */
bool debug_had_error(void) {
    return s_debug_error;
}

#else

bool debug_open(const char *base_name) {
    (void)base_name;
    return true;
}

void debug_set_min_level(LogLevel level) {
    (void)level;
}

void debug_log_level(LogLevel level, const char *fmt, ...) {
    (void)level;
    (void)fmt;
}

void debug_log(const char *fmt, ...) {
    (void)fmt;
}

bool debug_close(void) {
    return true;
}

bool debug_commit(void) {
    return true;
}

void debug_discard(void) {
}

bool debug_had_error(void) {
    return false;
}

#endif
