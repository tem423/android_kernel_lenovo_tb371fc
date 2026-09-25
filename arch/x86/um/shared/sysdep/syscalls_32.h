/* 
 * Copyright (C) 2000 - 2008 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <asm/unistd.h>
#include <sysdep/ptrace.h>

<<<<<<< HEAD
typedef long syscall_handler_t(struct pt_regs);
=======
typedef long syscall_handler_t(struct syscall_args);
>>>>>>> origin/android16-base

extern syscall_handler_t *sys_call_table[];

#define EXECUTE_SYSCALL(syscall, regs) \
<<<<<<< HEAD
	((long (*)(struct syscall_args)) \
	 (*sys_call_table[syscall]))(SYSCALL_ARGS(&regs->regs))
=======
	((*sys_call_table[syscall]))(SYSCALL_ARGS(&regs->regs))
>>>>>>> origin/android16-base
