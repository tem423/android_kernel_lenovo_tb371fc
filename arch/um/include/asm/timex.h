/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_TIMEX_H
#define __UM_TIMEX_H

<<<<<<< HEAD
typedef unsigned long cycles_t;

static inline cycles_t get_cycles (void)
{
	return 0;
}

#define CLOCK_TICK_RATE (HZ)

=======
#define CLOCK_TICK_RATE (HZ)

#include <asm-generic/timex.h>

>>>>>>> origin/android16-base
#endif
