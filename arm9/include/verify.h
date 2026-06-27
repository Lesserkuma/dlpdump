#pragma once
#include "state.h"

/**
 * @brief Loads the DS Download Play RSA public key from the DLDI file system.
 *
 * The expected file is a 128-byte big-endian RSA modulus at
 * `RSA_PUBLIC_KEY_PATH`. The fixed public exponent is 65537.
 *
 * @return true when the public key was loaded and RSA verification is enabled.
 */
bool verify_load_public_key(void);

/**
 * @brief Reports whether the DS Download Play RSA public key is available.
 */
bool verify_public_key_loaded(void);

/**
 * @brief Verifies the received sections against the RSA control-frame digest.
 *
 * @return true only when the downloaded payload hash matches the RSA signature.
 */
bool verify_download(const Download *dl);

/**
 * @brief Verifies three loaded sections against an RSA control frame.
 *
 * @return true only when the public key is loaded, the control frame is sane,
 *         and the section hash matches the RSA signature.
 */
bool verify_sections(const DownloadRsaFrame *rsa, const Section sec[3]);
