#!/bin/sh
# test_mount.sh - bbox_mount_bind() and bbox_mount_special() mount only
# onto a directory the caller owns, and the flags they promise are on the
# mount they made.
#
# Copyright (c) 2026 Tobias Koch <tobias.koch@gmail.com>
# SPDX-License-Identifier: MIT
#
# The mount itself always went through a descriptor.  The calls after
# it -- making the mount private, and the remount that adds nosuid and
# friends -- went by path as root, and a symlink swapped into that path
# after a concurrent unmount would have had root apply them elsewhere.
# The mount is now re-opened relative to the verified parent and
# addressed through that descriptor, and a failed remount takes the
# mount down again rather than leaving it without the flags.
#
# The race is not reproducible in a test.  What is asserted is the
# contract: the flags and private propagation are on the mount, a
# second call does not stack a second mount, and a symlink, a slash in
# the name, a missing directory or a parent owned by somebody else are
# refused without anything being mounted.
#
# Runs inside an unprivileged user namespace with its own mount, PID and
# network namespace: proc needs the PID namespace and sysfs the network
# namespace, or the kernel refuses the mount.

set -u

. "${srcdir:-.}/bboxlib.sh"

require_harness MOUNTDRV
require_tools unshare mount findmnt mountpoint stat
enter_userns

work=$(mktemp -d) || fail "mktemp failed"

cleanup() {
    cd / || :
    for _m in root/dev root/proc root/sys root/home/me/RealHome root/strict \
            strictsrc fresh/home/me/RealHome old/home/me/RealHome; do
        umount "$work/$_m" 2>/dev/null || :
    done
    rm -rf "$work"
}
trap cleanup EXIT

cd "$work" || fail "cd $work"

mkdir -p root/dev root/proc root/sys root/home/me/RealHome root/strict src \
    strictsrc victim fresh old/home/me
chmod 755 old/home/me
: > src/marker

# New mounts inherit their parent's propagation.  With everything shared,
# a mount that build-box did not make private shows up as shared.
mount --make-rshared / || fail "could not make / shared"

# options <dir> - the mount options of what is mounted on dir
options() { findmnt -no OPTIONS "$1"; }
propagation() { findmnt -no PROPAGATION "$1"; }
has_option() { case ",$(options "$1")," in *,"$2",*) return 0 ;; esac; return 1; }

# ── bind mount with restrictive flags ───────────────────────────────

"$MOUNTDRV" bind root src "" dev nosuid noexec || fail "bind mount of dev failed"
mounted root/dev || fail "dev is not mounted"
[ -e root/dev/marker ] || fail "dev does not show the source"
has_option root/dev nosuid || fail "dev lacks nosuid: $(options root/dev)"
has_option root/dev noexec || fail "dev lacks noexec: $(options root/dev)"
[ "$(propagation root/dev)" = private ] || fail "dev is not private: $(propagation root/dev)"
note "a bind mount carries the requested flags and is private"

"$MOUNTDRV" bind root src "" dev nosuid noexec || fail "mounting dev twice failed"
[ "$(findmnt -no TARGET root/dev | wc -l)" -eq 1 ] || fail "a second call stacked a mount"
note "mounting twice does not stack"

# ── special filesystems ─────────────────────────────────────────────

"$MOUNTDRV" special root proc "" proc || fail "proc mount failed"
mounted root/proc || fail "proc is not mounted"
for o in nosuid nodev noexec; do
    has_option root/proc $o || fail "proc lacks $o: $(options root/proc)"
done
[ "$(propagation root/proc)" = private ] || fail "proc is not private"
note "proc is mounted nosuid,nodev,noexec and private"

# Nothing inside a target has business writing below /sys, and a
# read-only sysfs is what a container's own /sys allows to be mounted
# below it, where a writable one is refused.
"$MOUNTDRV" special root sysfs "" sys || fail "sysfs mount failed"
mounted root/sys || fail "sys is not mounted"
for o in nosuid nodev noexec ro; do
    has_option root/sys $o || fail "sys lacks $o: $(options root/sys)"
done
[ "$(propagation root/sys)" = private ] || fail "sys is not private"
note "sysfs is mounted nosuid,nodev,noexec, read-only and private"

# ── flags the source already has are kept ───────────────────────────
#
# A bind remount replaces the flag set.  Asking for nosuid on a source
# that is ro,nodev must not hand back a mount that is neither.

mount -t tmpfs -o ro,nodev none strictsrc || fail "tmpfs mount of strictsrc"
"$MOUNTDRV" bind root strictsrc "" strict nosuid || fail "bind mount of strict failed"
for o in nosuid nodev ro; do
    has_option root/strict $o || fail "strict lacks $o: $(options root/strict)"
done
note "a remount adds to the source's flags instead of replacing them"

# ── the home mount below a relative parent path ─────────────────────

"$MOUNTDRV" bind root src /home/me RealHome nosuid nodev || fail "bind mount of RealHome failed"
mounted root/home/me/RealHome || fail "RealHome is not mounted"
has_option root/home/me/RealHome nosuid || fail "RealHome lacks nosuid"
has_option root/home/me/RealHome nodev || fail "RealHome lacks nodev"
note "a mount below a relative parent path carries its flags"

# ── the home mount creates a private per-target home ────────────────
#
# A target that predates per-target homes gets one created at first
# mount, with the mode a real home gets.  One that exists is left as it
# is: its mode is the user's decision.

"$MOUNTDRV" home fresh src /home/me || fail "home mount into a fresh sysroot failed"
mounted fresh/home/me/RealHome || fail "RealHome is not mounted in the fresh sysroot"
[ "$(stat -c %a fresh/home/me)" = 700 ] || fail "a new per-target home is mode $(stat -c %a fresh/home/me), not 700"
note "a per-target home created at mount time is private"

"$MOUNTDRV" home old src /home/me || fail "home mount into the old sysroot failed"
[ "$(stat -c %a old/home/me)" = 755 ] || fail "an existing per-target home was changed to $(stat -c %a old/home/me)"
note "an existing per-target home keeps its mode"

# ── what must be refused, with nothing mounted ──────────────────────

ln -s ../victim root/sym
"$MOUNTDRV" bind root src "" sym 2>/dev/null && fail "a symlink in the mount point position was accepted"
mounted victim && fail "the symlink's target was mounted on"
note "a symlink in the mount point position is refused"

"$MOUNTDRV" bind root src "" sym/x 2>/dev/null && fail "a name containing a slash was accepted"
note "a name containing a slash is refused"

"$MOUNTDRV" bind root src "" nope 2>/dev/null && fail "a missing mount point was accepted"
note "a missing mount point is refused"

"$MOUNTDRV" special root ext4 "" dev 2>/dev/null && fail "an unsupported filesystem type was accepted"
note "an unsupported special filesystem is refused"

if [ "${BBOX_TEST_OUTER_UID:-}" = 0 ]; then
    note "SKIP: running as root, every file is the caller's own"
else
    before=$(wc -l < /proc/self/mountinfo)
    "$MOUNTDRV" bind / src "" tmp 2>/dev/null && fail "a parent owned by somebody else was accepted"
    [ "$(wc -l < /proc/self/mountinfo)" -eq "$before" ] || fail "something was mounted regardless"
    note "a parent not owned by the caller is refused"
fi

exit 0
