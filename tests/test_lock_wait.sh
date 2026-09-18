#!/bin/sh
# test_lock_wait.sh - a mount call gives up on a sysroot lock that is
# never released.
#
# Copyright (c) 2026 Tobias Koch <tobias.koch@gmail.com>
# SPDX-License-Identifier: MIT
#
# bbox_lock_dir() takes an advisory flock(2) on the sysroot directory.
# Anyone who can open that directory for reading can take the same lock
# and hold it, and the lock is taken before anything is printed.  An
# unbounded wait turned that into a silent hang of the owner's login,
# run, mount and umount for as long as the other party cared to keep
# it.  The lock is now waited for with a bound, after which the call
# fails and says which directory it could not lock.
#
# The test holds the lock itself, asserts that a mount call fails within
# the bound rather than running forever, that it says why, and that
# nothing was mounted, and then that a call after the release succeeds.
#
# Runs inside an unprivileged user namespace with its own mount
# namespace, where the test is uid 0 and may bind mount.

set -u

. "${srcdir:-.}/bboxlib.sh"

require_harness MOUNTDRV
require_tools unshare mount umount flock mountpoint timeout date
enter_userns

work=$(mktemp -d) || fail "mktemp failed"

cleanup() {
    cd / || :
    umount "$work/root/dev" 2>/dev/null || :
    rm -rf "$work"
}
trap cleanup EXIT

cd "$work" || fail "cd $work"

mkdir -p root/dev src
: > src/marker

# ── a lock that is never released is given up on ────────────────────

exec 9< root || fail "could not open the sysroot"
flock -x 9 || fail "could not lock the sysroot"

started=$(date +%s)
timeout 30 "$MOUNTDRV" bind root src "" dev nosuid noexec 2>stderr
rc=$?
elapsed=$(( $(date +%s) - started ))

[ "$rc" -eq 124 ] && fail "the mount call was still waiting for the lock after 30 seconds"
[ "$rc" -eq 0 ] && fail "the mount call succeeded while the sysroot lock was held"
[ "$elapsed" -ge 2 ] || fail "the mount call gave up after $elapsed seconds without waiting"
grep -q "could not lock" stderr || fail "the mount call did not say that the lock was the problem: $(cat stderr)"
mounted root/dev && fail "the mount was made while the sysroot lock was held"
note "a mount call gives up on a held lock after $elapsed seconds and says so"

# ── a call after the release goes through ───────────────────────────

flock -u 9 || fail "could not unlock the sysroot"
"$MOUNTDRV" bind root src "" dev nosuid noexec || fail "the mount call failed once the lock was released"
mounted root/dev || fail "dev is not mounted after the lock was released"
note "the mount is made once the lock is released"

exit 0
