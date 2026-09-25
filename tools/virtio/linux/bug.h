/* SPDX-License-Identifier: GPL-2.0 */
<<<<<<< HEAD
#ifndef BUG_H
#define BUG_H

#define BUG_ON(__BUG_ON_cond) assert(!(__BUG_ON_cond))

#define BUILD_BUG_ON(x)

#define BUG() abort()

#endif /* BUG_H */
=======
#ifndef _LINUX_BUG_H
#define _LINUX_BUG_H

#define BUG_ON(__BUG_ON_cond) assert(!(__BUG_ON_cond))

#define BUG() abort()

#endif /* _LINUX_BUG_H */
>>>>>>> origin/android16-base
