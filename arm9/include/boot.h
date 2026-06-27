#pragma once
#include "state.h"

/**
 * @brief Boots the newest complete dump from the SD-card output directory.
 *
 * The function may only be called while the ARM9 app is in scan mode. It loads
 * the newest `.nds` that has a matching `.bcn` sidecar, asks ARM7 to prepare the
 * boot stub, copies the downloaded sections, and finally jumps into the child
 * program. A successful final handover normally does not return.
 *
 * @return true once the final handover was started; false if no bootable dump
 *         was found or a recoverable validation/preparation step failed.
 */
bool boot_latest_download(void);
