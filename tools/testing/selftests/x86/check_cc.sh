#!/bin/sh
# check_cc.sh - Helper to test userspace compilation support
# Copyright (c) 2015 Andrew Lutomirski
# GPL v2

CC="$1"
TESTPROG="$2"
shift 2

<<<<<<< HEAD
if "$CC" -o /dev/null "$TESTPROG" -O0 "$@" 2>/dev/null; then
=======
if [ -n "$CC" ] && $CC -o /dev/null "$TESTPROG" -O0 "$@" 2>/dev/null; then
>>>>>>> origin/android16-base
    echo 1
else
    echo 0
fi

exit 0
