#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

<<<<<<< HEAD
echo "int foo(void) { char X[200]; return 3; }" | $* -S -x c -c -m32 -O0 -fstack-protector - -o - 2> /dev/null | grep -q "%gs"
=======
echo "int foo(void) { char X[200]; return 3; }" | $* -S -x c -m32 -O0 -fstack-protector - -o - 2> /dev/null | grep -q "%gs"
>>>>>>> origin/android16-base
