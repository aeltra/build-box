#!/bin/sh
# test_mount_undo.sh - a mount call that fails after the mount is made
# takes the mount down again.
#
# Copyright (c) 2026 Tobias Koch <tobias.koch@gmail.com>
# SPDX-License-Identifier: MIT
#
# bbox_mount_bind() and bbox_mount_special() make the mount first and
# apply propagation and the restrictive flags afterwards, each step
# through a descriptor.  Every one of those steps can fail, and the
# caller can make them fail at will: rlimits survive the setuid exec, so
# a descriptor budget that runs out right after mount(2) is a choice.
# Only a failed remount used to take the mount down again.  A failed
# re-open, a failed mount id read or a failed MS_PRIVATE reported an
# error and left the mount behind, shared and carrying only the source's
# flags, and the next call found something mounted and returned success
# without repairing it.
#
# The test runs the harness under every descriptor limit from one that
# fails the first open() up to the one that lets the call succeed, and
# asserts that no failing run leaves a mount behind.  Which step a given
# limit trips is an implementation detail; that every failing step
# cleans up after itself is the contract.
#
# Runs inside an unprivileged user namespace with its own mount and PID
# namespace, where the test is uid 0 and may mount, and proc needs the
# PID namespace or the kernel refuses the mount.

set -u

. "${srcdir:-.}/bboxlib.sh"

require_harness MOUNTDRV
require_tools unshare mount umount mountpoint findmnt
enter_userns

work=$(mktemp -d) || fail "mktemp failed"

cleanup() {
    cd / || :
    for _m in root/dev root/proc; do
        umount "$work/$_m" 2>/dev/null || :
    done
    rm -rf "$work"
}
trap cleanup EXIT

cd "$work" || fail "cd $work"

mkdir -p root/dev root/proc src
: > src/marker

# New mounts inherit their parent's propagation.  With everything shared,
# a mount left behind shows up as shared.
mount --make-rshared / || fail "could not make / shared"

# limited <n> <command...> - run the command with a descriptor limit of n
limited() {
    _n=$1; shift
    ( ulimit -n "$_n" && exec "$@" )
}

# sweep <what> <mountpoint> <command...>
#
# Run the command under descriptor limits from 3 upwards until it
# succeeds.  A run that fails must not leave anything mounted, and the
# one that succeeds must have mounted.  Gives up if no limit below 32
# lets the call succeed, which would mean the harness itself is broken.
sweep() {
    _what=$1; _mp=$2; shift 2
    _n=3
    while [ "$_n" -lt 32 ]; do
        if limited "$_n" "$@" 2>/dev/null; then
            mounted "$_mp" || fail "$_what: the call succeeded with $_n descriptors but nothing is mounted"
            note "$_what: no failing call left a mount behind, the call succeeds with $_n descriptors"
            return 0
        fi
        mounted "$_mp" && fail "$_what: the call failed with $_n descriptors and left the mount behind: $(findmnt -no OPTIONS,PROPAGATION "$_mp")"
        _n=$((_n + 1))
    done
    fail "$_what: no descriptor limit below 32 let the call succeed"
}

# ── a bind mount ────────────────────────────────────────────────────

sweep "bind" root/dev "$MOUNTDRV" bind root src "" dev nosuid noexec
umount root/dev || fail "could not unmount dev"

# ── a special filesystem ────────────────────────────────────────────

sweep "proc" root/proc "$MOUNTDRV" special root proc "" proc
umount root/proc || fail "could not unmount proc"

exit 0
