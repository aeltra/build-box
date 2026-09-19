#!/bin/sh
# test_commands.sh - the command entry points, end to end: init, mount,
# umount, run and login on a real target below the real per-user path.
#
# Copyright (c) 2026 Tobias Koch <tobias.koch@gmail.com>
# SPDX-License-Identifier: MIT
#
# The harness tests below this one call the functions beneath the
# commands.  This one calls the commands themselves, the way main()
# does after dispatching, so the target lookup, the name validation,
# the option handling, the exit codes and the order of operations are
# exercised together, and a session's shell is watched from outside.
#
# Runs inside an unprivileged user namespace where the caller is uid 0.
# The per-user path under /var/lib and the caller's home are the real
# ones as far as the code can tell: a tmpfs sits over /var/lib, and the
# caller's home from the password database, /root, has a directory of
# ours bound over it so that it is owned by the caller.  The run and
# login paths reset the supplementary groups, which the kernel permits
# only in a namespace set up through the subordinate id ranges.

set -u

. "${srcdir:-.}/bboxlib.sh"

require_harness CMDDRV
require_tools unshare mount umount mountpoint findmnt stat
enter_userns
require_setgroups

[ -d /root ] || skip "there is no /root to bind the caller's home over"

work=$(mktemp -d) || fail "mktemp failed"
t=/var/lib/build-box/users/$(id -u)/targets/t

cleanup() {
    cd / || :
    for _m in "$t/home/root/RealHome" "$t/sys" "$t/proc" "$t/dev"; do
        umount "$_m" 2>/dev/null || :
    done
    umount /root 2>/dev/null || :
    umount /dev 2>/dev/null || :
    umount "$work/dev/null" 2>/dev/null || :
    rm -rf "$work"
}
trap cleanup EXIT

mount -t tmpfs none /var/lib || fail "could not mount a tmpfs over /var/lib"
mkdir -p /var/lib/build-box/users

mkdir "$work/home"
mount --bind "$work/home" /root || fail "could not bind the caller's home over /root"

# The dev mount is a non-recursive bind of /dev.  In a user namespace
# the kernel refuses that for a mount whose child mounts are locked,
# which the host's pts, shm and mqueue below /dev are, so /dev is
# replaced with a directory of ours first.  It keeps a /dev/null, which
# is all the sessions below need.
mkdir "$work/dev"
: > "$work/dev/null"
mount --bind /dev/null "$work/dev/null" || fail "could not bind /dev/null"
mount --bind "$work/dev" /dev || fail "could not put a /dev of our own in place"

# ── init ─────────────────────────────────────────────────────────────

"$CMDDRV" init || fail "init failed"
[ "$(stat -c %a "/var/lib/build-box/users/$(id -u)")" = 700 ] \
    || fail "init did not create the per-user directory 0700"
note "init creates the per-user directory"

"$CMDDRV" init --bogus 2>/dev/null && fail "init accepted an unknown option"
note "init refuses an unknown option"

# ── a target ─────────────────────────────────────────────────────────

mkdir -p "$t/dev" "$t/proc" "$t/sys" "$t/etc"
provision_shell "$t" || skip "cannot provision a shell into the target"
echo hello > "$t/marker"

# ── mount and umount ─────────────────────────────────────────────────

"$CMDDRV" mount t || fail "mount failed"
for _m in dev proc sys home/root/RealHome; do
    mounted "$t/$_m" || fail "$_m is not mounted after mount"
done
[ -e "$t/home/root/RealHome/../../../marker" ] || fail "sanity"
[ "$(stat -c %a "$t/home/root")" = 700 ] || fail "the per-target home is not 0700"
[ "$(findmnt -no PROPAGATION "$t/dev")" = private ] || fail "dev is not private"
case ",$(findmnt -no OPTIONS "$t/sys")," in
    *,ro,*) ;;
    *) fail "sys is not read-only: $(findmnt -no OPTIONS "$t/sys")" ;;
esac
note "mount mounts dev, proc, sys and the home and makes them private"

"$CMDDRV" mount t || fail "a second mount failed"
[ "$(findmnt -no TARGET "$t/dev" | wc -l)" -eq 1 ] || fail "a second mount stacked"
note "a second mount is a no-op"

"$CMDDRV" umount -m dev t || fail "umount -m dev failed"
mounted "$t/dev" && fail "dev is still mounted after umount -m dev"
mounted "$t/proc" || fail "umount -m dev took proc down as well"
note "umount -m takes down only what was named"

"$CMDDRV" umount t || fail "umount failed"
for _m in dev proc sys home/root/RealHome; do
    mounted "$t/$_m" && fail "$_m is still mounted after umount"
done
note "umount takes everything down"

"$CMDDRV" umount t || fail "umount with nothing mounted failed"
note "umount with nothing mounted is a no-op"

# ── what is refused, with the invocation exit status ─────────────────

for args in "mount nope" "mount ../t" "mount" "mount --bogus t" "mount -m ext4 t" \
        "umount nope" "login nope" "run nope -- true" "run t"; do
    set -- $args
    "$CMDDRV" "$@" 2>/dev/null
    rc=$?
    [ "$rc" -eq 254 ] || fail "'$args' exited $rc, not 254"
done
note "a missing target, a bad name, a bad option and a missing command are invocation errors"

# ── run ──────────────────────────────────────────────────────────────

# Only the shell is provisioned into the target, so every command below
# is a shell with builtins.
out=$("$CMDDRV" run --no-file-copy t sh -c 'read l < /marker; echo $l') || fail "run failed: $out"
[ "$out" = hello ] || fail "run did not run inside the target: '$out'"
mounted "$t/proc" || fail "run did not mount the target first"
note "run mounts the target and runs the command inside it"

"$CMDDRV" run --no-mount --no-file-copy t sh -c 'exit 7'
[ $? -eq 7 ] || fail "the command's exit status was not passed through"
note "run passes the command's exit status through"

# "-c" is sh's option, standing after the target; it must reach sh.
out=$("$CMDDRV" run --no-mount --no-file-copy t sh -c 'echo ok') || fail "run with the command's own option failed"
[ "$out" = ok ] || fail "the command's own option did not reach it: '$out'"
out=$("$CMDDRV" run --no-mount --no-file-copy t -- sh -c 'echo ok') || fail "run with -- failed"
[ "$out" = ok ] || fail "the -- form did not reach the command: '$out'"
note "the command keeps its own options, with and without --"

out=$("$CMDDRV" run --no-mount --no-file-copy t sh -c 'echo $HOME; pwd') || fail "run for HOME failed"
[ "$out" = "/home/root
/home/root" ] || fail "HOME or the working directory is not the in-target home: '$out'"
note "a session starts in the in-target home with HOME set"

"$CMDDRV" run --no-mount t true || fail "run with the file copy failed"
grep -q "^root:x:0:0:.*:/home/root:" "$t/etc/passwd" \
    || fail "the target's passwd does not carry the in-target home for the caller"
[ -f "$t/etc/hosts" ] || fail "hosts was not copied"
note "run copies the host databases with the caller's home rewritten"

[ -L "$t/.pkg-cache" ] || fail "the package cache link was not created"
[ -d "$work/home/.aeltra/cache/aeltra" ] || fail "the package cache directory was not created behind the link"
note "a session creates the package cache link and its directory"

# A single argument runs in the session shell itself, which is the
# first process of the new PID namespace; with several, that shell
# forks a child for the command and the child is PID 2.
out=$("$CMDDRV" run --isolate --no-file-copy t 'echo $$') || fail "the isolated run failed: $out"
[ "$out" = 1 ] || fail "the isolated session is not PID 1 of its own namespace: '$out'"
note "an isolated session gets its own PID namespace"

"$CMDDRV" run --isolate --no-mount --no-file-copy t sh -c 'exit 5'
[ $? -eq 5 ] || fail "the isolated session's exit status was not passed through"
note "an isolated session passes the exit status through"

# ── login ────────────────────────────────────────────────────────────

out=$(echo 'read l < /marker; echo $l; pwd; exit 3' | "$CMDDRV" login --no-mount --no-file-copy t)
rc=$?
[ "$rc" -eq 3 ] || fail "login did not pass the shell's exit status through: $rc"
[ "$out" = "hello
/home/root" ] || fail "login did not start a shell in the target's home: '$out'"
note "login starts a shell inside the target, in the home, and passes its status through"

echo 'exit 0' | "$CMDDRV" login t || fail "login with mounts failed"
mounted "$t/home/root/RealHome" || fail "login did not mount the home"
note "login mounts the target first"

exit 0
