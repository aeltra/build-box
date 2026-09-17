#!/bin/sh
# test_isolate_proc.sh - an isolated session mounts its proc on the
# sysroot's proc directory and nowhere else.
#
# Copyright (c) 2026 Tobias Koch <tobias.koch@gmail.com>
# SPDX-License-Identifier: MIT
#
# The child of an isolated session mounted its proc by path, as root,
# after the chroot.  The sysroot is the user's tree, and with "proc"
# replaced by a symlink the mount went wherever the link pointed -- into
# the bind-mounted /dev or the real home, say.  It could not leave the
# chroot or the session's private namespace, but it was the one place
# root walked a path the user wrote.  The entry is now opened relative
# to the sysroot without following symlinks, and the mount goes through
# the descriptor.
#
# A symlink or a plain file in the proc position must make the session
# fail before anything runs, with the link's target untouched.  A proc
# mounted by a plain session earlier must still be stacked on, since
# that instance shows the host's PID namespace and not the session's;
# test_isolate.sh asserts that case.
#
# Runs inside an unprivileged user namespace, see test_no_new_privs.sh
# for why a namespace with subordinate ids is needed.

set -u

. "${srcdir:-.}/bboxlib.sh"

require_harness RUNDRV
require_tools unshare mount
enter_userns
require_setgroups

work=$(mktemp -d) || fail "mktemp failed"

cleanup() {
    cd / || :
    umount "$work/root/victim" 2>/dev/null || :
    rm -rf "$work"
}
trap cleanup EXIT

cd "$work" || fail "cd $work"

mkdir -p root/victim src
provision_shell root || skip "cannot provision a shell into the sysroot"

: > src/marker
mount --bind src root/victim || fail "bind mount of the victim"

# ── a symlink in the proc position ───────────────────────────────────
#
# Inside the chroot the absolute link resolves to the victim.  If the
# session runs at all, it reports whether the marker is still visible
# or has been covered by a proc mount.

ln -s /victim root/proc || fail "could not plant the symlink"

if out=$("$RUNDRV" --isolate root 'test -e /victim/marker && echo present || echo covered' 2>/dev/null); then
    fail "a symlink in the proc position was accepted, the session ran and found the victim $out"
fi
[ -e root/victim/marker ] || fail "the victim was mounted over in this namespace"
note "a symlink in the proc position is refused"

# ── a file in the proc position ──────────────────────────────────────

rm root/proc
: > root/proc

"$RUNDRV" --isolate root 'echo ran' >/dev/null 2>&1 \
    && fail "a plain file in the proc position was accepted"
note "a file in the proc position is refused"

exit 0
