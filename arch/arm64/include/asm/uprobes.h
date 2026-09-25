/*
 * Copyright (C) 2014-2016 Pratyush Anand <panand@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ASM_UPROBES_H
#define _ASM_UPROBES_H

#include <asm/debug-monitors.h>
#include <asm/insn.h>
#include <asm/probes.h>

<<<<<<< HEAD
#define MAX_UINSN_BYTES		AARCH64_INSN_SIZE

#define UPROBE_SWBP_INSN	BRK64_OPCODE_UPROBES
#define UPROBE_SWBP_INSN_SIZE	AARCH64_INSN_SIZE
#define UPROBE_XOL_SLOT_BYTES	MAX_UINSN_BYTES

typedef u32 uprobe_opcode_t;
=======
#define UPROBE_SWBP_INSN	cpu_to_le32(BRK64_OPCODE_UPROBES)
#define UPROBE_SWBP_INSN_SIZE	AARCH64_INSN_SIZE
#define UPROBE_XOL_SLOT_BYTES	AARCH64_INSN_SIZE

typedef __le32 uprobe_opcode_t;
>>>>>>> origin/android16-base

struct arch_uprobe_task {
};

struct arch_uprobe {
	union {
<<<<<<< HEAD
		u8 insn[MAX_UINSN_BYTES];
		u8 ixol[MAX_UINSN_BYTES];
=======
		__le32 insn;
		__le32 ixol;
>>>>>>> origin/android16-base
	};
	struct arch_probe_insn api;
	bool simulate;
};

#endif
