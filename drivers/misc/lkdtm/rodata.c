// SPDX-License-Identifier: GPL-2.0
/*
 * This includes functions that are meant to live entirely in .rodata
 * (via objcopy tricks), to validate the non-executability of .rodata.
 */
#include "lkdtm.h"

<<<<<<< HEAD
void notrace lkdtm_rodata_do_nothing(void)
=======
void noinstr lkdtm_rodata_do_nothing(void)
>>>>>>> origin/android16-base
{
	/* Does nothing. We just want an architecture agnostic "return". */
}
