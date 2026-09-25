/* SPDX-License-Identifier: GPL-2.0 */
<<<<<<< HEAD
	.section .piggydata,#alloc
=======
	.section .piggydata, "a"
>>>>>>> origin/android16-base
	.globl	input_data
input_data:
	.incbin	"arch/arm/boot/compressed/piggy_data"
	.globl	input_data_end
input_data_end:
