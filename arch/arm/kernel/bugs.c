// SPDX-Identifier: GPL-2.0
#include <linux/init.h>
<<<<<<< HEAD
=======
#include <linux/cpu.h>
>>>>>>> origin/android16-base
#include <asm/bugs.h>
#include <asm/proc-fns.h>

void check_other_bugs(void)
{
#ifdef MULTI_CPU
	if (cpu_check_bugs)
		cpu_check_bugs();
#endif
}

<<<<<<< HEAD
void __init check_bugs(void)
=======
void __init arch_cpu_finalize_init(void)
>>>>>>> origin/android16-base
{
	check_writebuffer_bugs();
	check_other_bugs();
}
