// SPDX-License-Identifier: GPL-2.0
#include "blacklist.h"

<<<<<<< HEAD
const char __initdata *const blacklist_hashes[] = {
=======
const char __initconst *const blacklist_hashes[] = {
>>>>>>> origin/4.19.325
#include CONFIG_SYSTEM_BLACKLIST_HASH_LIST
	, NULL
};
