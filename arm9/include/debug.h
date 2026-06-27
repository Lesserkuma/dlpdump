#pragma once
#include "state.h"

typedef enum {
    LOG_ERROR = 0,
    LOG_WARN,
    LOG_INFO,
    LOG_DEBUG,
    LOG_TRACE,
} LogLevel;

/**
 * @brief Opens the per-download debug log under the output directory.
 *
 * @param base_name Safe output base name without extension.
 * @return true if the log file was created; false if logging should be disabled.
 */
bool debug_open(const char *base_name);

/**
 * @brief Sets the lowest severity that will be written to the debug log.
 *
 * @param level Messages below this level are ignored.
 */
void debug_set_min_level(LogLevel level);

/**
 * @brief Writes a formatted message when its severity passes the log filter.
 *
 * @param level Severity for this message.
 * @param fmt printf-style format string followed by matching arguments.
 */
void debug_log_level(LogLevel level, const char *fmt, ...);

/**
 * @brief Writes a debug-level formatted message if logging is open.
 *
 * @param fmt printf-style format string followed by matching arguments.
 */
void debug_log(const char *fmt, ...);

/**
 * @brief Flushes and closes the current debug log, if one is open.
 */
bool debug_close(void);

/** @brief Commits the closed debug-log temp file to its final path. */
bool debug_commit(void);

/** @brief Removes an uncommitted debug-log temp file. */
void debug_discard(void);

/** @brief Returns whether the current/last debug log hit an I/O error. */
bool debug_had_error(void);
