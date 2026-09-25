/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_ARCHRANDOM_H
#define _ASM_POWERPC_ARCHRANDOM_H

#ifdef CONFIG_ARCH_RANDOM

#include <asm/machdep.h>

<<<<<<< HEAD
static inline int arch_get_random_long(unsigned long *v)
{
	return 0;
}

static inline int arch_get_random_int(unsigned int *v)
{
	return 0;
}

static inline int arch_get_random_seed_long(unsigned long *v)
=======
static inline bool arch_get_random_long(unsigned long *v)
{
	return false;
}

static inline bool arch_get_random_int(unsigned int *v)
{
	return false;
}

static inline bool arch_get_random_seed_long(unsigned long *v)
>>>>>>> origin/android16-base
{
	if (ppc_md.get_random_seed)
		return ppc_md.get_random_seed(v);

<<<<<<< HEAD
	return 0;
}
static inline int arch_get_random_seed_int(unsigned int *v)
{
	unsigned long val;
	int rc;
=======
	return false;
}

static inline bool arch_get_random_seed_int(unsigned int *v)
{
	unsigned long val;
	bool rc;
>>>>>>> origin/android16-base

	rc = arch_get_random_seed_long(&val);
	if (rc)
		*v = val;

	return rc;
}
<<<<<<< HEAD

static inline int arch_has_random(void)
{
	return 0;
}

static inline int arch_has_random_seed(void)
{
	return !!ppc_md.get_random_seed;
}
=======
>>>>>>> origin/android16-base
#endif /* CONFIG_ARCH_RANDOM */

#ifdef CONFIG_PPC_POWERNV
int powernv_hwrng_present(void);
int powernv_get_random_long(unsigned long *v);
int powernv_get_random_real_mode(unsigned long *v);
#else
static inline int powernv_hwrng_present(void) { return 0; }
static inline int powernv_get_random_real_mode(unsigned long *v) { return 0; }
#endif

#endif /* _ASM_POWERPC_ARCHRANDOM_H */
