/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Annotations for marking C functions as exception handlers.
 *
 * These should only be used for C functions that are called from the low
 * level exception entry code and not any intervening C code.
 */
#ifndef __ASM_ARM_EXCEPTION_H
#define __ASM_ARM_EXCEPTION_H

#include <linux/interrupt.h>

<<<<<<< HEAD
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
#define __exception_irq_entry	__irq_entry
#else
#define __exception_irq_entry
#endif
=======
#define __exception_irq_entry	__irq_entry
>>>>>>> origin/android16-base

#endif /* __ASM_ARM_EXCEPTION_H */
