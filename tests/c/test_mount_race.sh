#!/bin/sh
# test_mount_race.sh - concurrent mount calls on one sysroot serialize.
#
# Copyright (c) 2026 Tobias Koch <tobias.koch@gmail.com>
# SPDX-License-Identifier: MIT
#
# bbox_mount_bind() checks that nothing is mounted on the target and
# then mounts through a descriptor it opened before the check.  Two
# invocations that both pass the check both mount, and the kernel stacks
# the second mount on top of the first.  The code that follows finds the
# mount by name, so only the top one is made private and remounted with
# nosuid and friends; the one underneath keeps whatever flags the source
# had, and a single umount only exposes it.
#
# The mount functions now hold an exclusive flock(2) on the sysroot
# directory from before the check until the mount is finished.  The test
# takes that lock itself and asserts that a mount call waits for it, and
# has the harness release a batch of callers at once and asserts that
# exactly one mount is the result.
#
# Runs inside an unprivileged user namespace with its own mount
# namespace, where the test is uid 0 and may bind mount.

set -u

. "${srcdir:-.}/bboxlib.sh"

require_harness MOUNTDRV
require_tools unshare mount umount flock mountpoint awk
enter_userns

work=$(mktemp -d) || fail "mktemp failed"

cleanup() {
    cd / || :
    # A stacked mount comes off one layer at a time.
    for _i in 1 2 3 4 5 6 7 8 9; do
        umount "$work/root/dev" 2>/dev/null || break
    done
    rm -rf "$work"
}
trap cleanup EXIT

cd "$work" || fail "cd $work"

mkdir -p root/dev src
: > src/marker

# mount_count <dir> - how many mounts are stacked on the directory
mount_count() {
    awk -v t="$1" '$5 == t' /proc/self/mountinfo | wc -l
}

# ── a mount call waits for the sysroot lock ─────────────────────────

exec 9< root || fail "could not open the sysroot"
flock -x 9 || fail "could not lock the sysroot"

"$MOUNTDRV" bind root src "" dev nosuid noexec &
drv=$!

sleep 1

kill -0 "$drv" 2>/dev/null || fail "the mount call did not wait for the lock"
mounted root/dev && fail "the mount was made while the sysroot lock was held"
note "a mount call waits while the sysroot is locked"

flock -u 9 || fail "could not unlock the sysroot"
wait "$drv" || fail "the mount call failed once the lock was released"
mounted root/dev || fail "dev is not mounted after the lock was released"
note "the mount is made once the lock is released"

umount root/dev || fail "could not unmount dev"

# ── callers released together make one mount, not a stack ───────────

"$MOUNTDRV" race 8 bind root src "" dev nosuid noexec || fail "a concurrent mount call failed"
n=$(mount_count "$work/root/dev")
[ "$n" -eq 1 ] || fail "8 concurrent callers left $n mounts stacked on dev"
note "concurrent callers make a single mount"

umount root/dev || fail "could not unmount dev"
mounted root/dev && fail "dev is still mounted after one umount"
note "one umount takes it down"

exit 0
