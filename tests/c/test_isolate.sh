#!/bin/sh
# test_isolate.sh - an isolated session's mounts stay inside its namespace.
#
# Copyright (c) 2026 Tobias Koch <tobias.koch@gmail.com>
# SPDX-License-Identifier: MIT
#
# unshare(2) leaves the new namespace's mounts in the host's peer
# groups.  On a systemd host the root mount is shared, so the proc the
# isolated child mounts over the chroot's /proc would propagate back to
# the host and outlive the session.  It did not, but only because
# bbox_mount_special() marks the mount it lands on private in a
# different code path.  The isolate path now makes its whole tree
# private itself.
#
# The sandbox's mounts are made shared first, so that a mount which is
# not private shows up outside.  The sysroot's proc is mounted by hand
# rather than through bbox_mount_special(), so that the isolate path is
# the only thing standing between the child's mount and this namespace.

set -u

. "${srcdir:-.}/bboxlib.sh"

require_harness RUNDRV
require_tools unshare mount findmnt
enter_userns
require_setgroups

work=$(mktemp -d) || fail "mktemp failed"

cleanup() {
    cd / || :
    umount "$work/root/proc" 2>/dev/null || :
    umount "$work/root/proc" 2>/dev/null || :
    rm -rf "$work"
}
trap cleanup EXIT

cd "$work" || fail "cd $work"

mkdir -p root/proc
provision_shell root || skip "cannot provision a shell into the sysroot"

mount --make-rshared / || fail "could not make / shared"
mount -t proc none root/proc || fail "proc mount"

# What the child mounts is a proc of its own PID namespace; inside, PID 1
# is the child itself.  Outside, the sysroot's proc must still be the one
# mounted above, and there must still be exactly one mount there.

before=$(findmnt -no TARGET root/proc | wc -l)
[ "$before" -eq 1 ] || fail "expected one mount on root/proc before the run, found $before"

out=$("$RUNDRV" --isolate root 'echo pid=$$') || fail "the isolated run failed: $out"
case $out in
    pid=1) ;;
    *) fail "the isolated command did not run as PID 1: '$out'" ;;
esac
note "the isolated command runs in its own PID namespace"

after=$(findmnt -no TARGET root/proc | wc -l)
[ "$after" -eq 1 ] || fail "the child's proc mount leaked into this namespace: $after mounts on root/proc"
note "the child's proc mount did not propagate out"

exit 0
