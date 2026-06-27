#pragma once

/**
 * @file config.h
 * @brief Project-wide runtime configuration constants.
 */

#ifndef OUTPUT_DIR
#define OUTPUT_DIR "/dlpdump"
#endif
#ifndef RSA_PUBLIC_KEY_PATH
#define RSA_PUBLIC_KEY_PATH "/dlpdump/pubkey.bin"
#endif
#define APP_NAME "dlpdump"
#define VERSION "v1.0"
#if DEBUG_VERSION
#define APP_VERSION VERSION " (debug)"
#else
#define APP_VERSION VERSION
#endif
#define COMM_TIMEOUT_SECONDS 10u
#define TRANSFER_START_TIMEOUT_SECONDS 30u
#define FINAL_WAIT_TIMEOUT_SECONDS 10u
#define REPEAT_DOWNLOAD_COOLDOWN_SECONDS 8u
