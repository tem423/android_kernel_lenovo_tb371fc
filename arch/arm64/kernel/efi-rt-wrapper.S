/*
 * Copyright (C) 2018 Linaro Ltd <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>
<<<<<<< HEAD
=======
#include <asm/assembler.h>
>>>>>>> origin/android16-base

ENTRY(__efi_rt_asm_wrapper)
	stp	x29, x30, [sp, #-32]!
	mov	x29, sp

	/*
	 * Register x18 is designated as the 'platform' register by the AAPCS,
	 * which means firmware running at the same exception level as the OS
	 * (such as UEFI) should never touch it.
	 */
	stp	x1, x18, [sp, #16]

<<<<<<< HEAD
=======
	ldr_l	x16, efi_rt_stack_top
	mov	sp, x16
#ifdef CONFIG_SHADOW_CALL_STACK
	str	x18, [sp, #-16]!
#endif

>>>>>>> origin/android16-base
	/*
	 * We are lucky enough that no EFI runtime services take more than
	 * 5 arguments, so all are passed in registers rather than via the
	 * stack.
	 */
	mov	x8, x0
	mov	x0, x2
	mov	x1, x3
	mov	x2, x4
	mov	x3, x5
	mov	x4, x6
	blr	x8

<<<<<<< HEAD
=======
	mov	sp, x29
>>>>>>> origin/android16-base
	ldp	x1, x2, [sp, #16]
	cmp	x2, x18
	ldp	x29, x30, [sp], #32
	b.ne	0f
	ret
0:
<<<<<<< HEAD
#ifdef CONFIG_SHADOW_CALL_STACK
=======
>>>>>>> origin/android16-base
	/*
	 * Restore x18 before returning to instrumented code. This is
	 * safe because the wrapper is called with preemption disabled and
	 * a separate shadow stack is used for interrupts.
	 */
<<<<<<< HEAD
	mov	x18, x2
#endif
=======
#ifdef CONFIG_SHADOW_CALL_STACK
	ldr_l	x18, efi_rt_stack_top
	ldr	x18, [x18, #-16]
#endif

>>>>>>> origin/android16-base
	b	efi_handle_corrupted_x18	// tail call
ENDPROC(__efi_rt_asm_wrapper)
