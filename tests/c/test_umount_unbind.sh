#!/bin/sh
# test_umount_unbind.sh - bbox_umount_unbind() only ever unmounts a mount
# directly below a directory the caller owns.
#
# Copyright (c) 2026 Tobias Koch <tobias.koch@gmail.com>
# SPDX-License-Identifier: MIT
#
# The unmount used to be done by path, as root, after checking that
# path.  Every component below the per-user directory belongs to the
# user, who can exchange the sysroot for a symlink between the checks
# and the syscall and have root unmount whatever the symlink points at.
# The function now verifies the parent through a file descriptor and
# unmounts a single name relative to it.
#
# The race itself is not reproducible in a test.  What is asserted here
# is the contract that makes it moot: mounts directly below an owned
# parent come off, and nothing reachable only through a symlink, a
# slash in the name, or a parent owned by somebody else is touched.
#
# Runs inside an unprivileged user namespace, where the test is uid 0
# and may bind mount.  Files it owns appear owned by uid 0 there, so
# "owned by the caller" holds for everything the test creates -- and
# not for /, whose real owner has no mapping.

set -u

. "${srcdir:-.}/bboxlib.sh"

require_harness UMOUNTDRV
require_tools unshare mount mountpoint
enter_userns

work=$(mktemp -d) || fail "mktemp failed"

# The mounts vanish with the namespace, but the directory lives on the
# host and rm cannot remove a mount point, so the mounts the test leaves
# in place are taken down first.
cleanup() {
    cd / || :
    for _m in root/dev root/proc root/home/me/RealHome victim/dev; do
        umount "$work/$_m" 2>/dev/null || :
    done
    rm -rf "$work"
}
trap cleanup EXIT

cd "$work" || fail "cd $work"

mkdir -p root/dev root/proc root/plain root/home/me/RealHome src victim/dev
: > src/marker

mount --bind src root/dev            || fail "bind mount of dev"
mount --bind src root/home/me/RealHome || fail "bind mount of RealHome"
mount -t proc none root/proc         || fail "proc mount"
mount -t tmpfs none victim/dev       || fail "tmpfs mount on the victim"
: > victim/dev/INSIDE

# ── what must be unmounted ───────────────────────────────────────────

"$UMOUNTDRV" root "" dev || fail "unbinding dev failed"
mounted root/dev && fail "dev is still mounted"
note "a bind mount from the same filesystem is unmounted"

"$UMOUNTDRV" root "" proc || fail "unbinding proc failed"
mounted root/proc && fail "proc is still mounted"
note "a proc mount is unmounted"

"$UMOUNTDRV" root /home/me RealHome || fail "unbinding RealHome failed"
mounted root/home/me/RealHome && fail "RealHome is still mounted"
note "a mount below a relative parent path is unmounted"

# ── what must be a quiet success ─────────────────────────────────────

"$UMOUNTDRV" root "" dev   || fail "an already unmounted entry is an error"
"$UMOUNTDRV" root "" plain || fail "a directory with no mount is an error"
"$UMOUNTDRV" root "" nope  || fail "a missing entry is an error"
"$UMOUNTDRV" root /home/other RealHome || fail "a missing parent is an error"
note "nothing mounted, missing entry, missing parent: quiet success"

# ── what must be refused, with the victim untouched ──────────────────

ln -s ../victim/dev root/sym
"$UMOUNTDRV" root "" sym 2>/dev/null && fail "a symlink in the mount point position was accepted"
[ -e victim/dev/INSIDE ] || fail "the symlink's target was unmounted"
note "a symlink in the mount point position is refused"

ln -s ../victim root/link
"$UMOUNTDRV" root "" link/dev 2>/dev/null && fail "a name containing a slash was accepted"
[ -e victim/dev/INSIDE ] || fail "the slash walked through the symlink"
note "a name containing a slash is refused"

# The one directory guaranteed not to be the caller's is /, whose owner
# has no mapping in the namespace -- unless the suite runs as root, in
# which case it does, and there is no file on the system that is not
# root's to try instead.
if [ "${BBOX_TEST_OUTER_UID:-}" = 0 ]; then
    note "SKIP: running as root, every file is the caller's own"
else
    "$UMOUNTDRV" / "" proc 2>/dev/null && fail "a parent owned by somebody else was accepted"
    mounted /proc || fail "/proc was unmounted"
    note "a parent not owned by the caller is refused"
fi

exit 0
