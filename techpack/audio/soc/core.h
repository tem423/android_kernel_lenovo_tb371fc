/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Compatibility wrapper for the Qualcomm WCD pinctrl driver.
 *
 * The driver includes "core.h" from techpack/audio/soc, while the
 * pinctrl core private header is maintained under drivers/pinctrl.
 */

#include "../../../drivers/pinctrl/core.h"
