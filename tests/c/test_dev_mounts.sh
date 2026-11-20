#!/bin/sh
# test_dev_mounts.sh - a target's dev comes with a shared memory file
# system and pseudo-terminals of its own, and they go again with it.
#
# Copyright (c) 2026 Tobias Koch <tobias.koch@gmail.com>
# SPDX-License-Identifier: MIT
#
# The dev mount is a non-recursive bind of /dev, so what the host has
# mounted below /dev does not come along.  A target used to see the
# empty directories underneath: no shm_open() or sem_open() for an
# ordinary user, and no pseudo-terminal for anybody.  A tmpfs is now
# mounted on dev/shm and a devpts instance on dev/pts.
#
# These two are the only mounts build-box makes below a directory that
# is not the caller's: their parent is the host's /dev.  What stands in
# for the ownership check is that the parent is a mount of /dev sitting
# directly on the sysroot's dev entry.  Asserted here, next to the
# mounts and their options: nothing reaches the host's /dev, be it
# through propagation or through a symlink in the place of dev; the
# two come off before dev does; and a busy one is reported and not
# detached.
#
# Runs inside an unprivileged user namespace, on a real target below
# the real per-user path, set up the way test_commands.sh does it.

set -u

. "${srcdir:-.}/bboxlib.sh"

require_harness CMDDRV
require_tools unshare mount umount mountpoint findmnt stat
enter_userns

[ -d /root ] || skip "there is no /root to bind the caller's home over"

work=$(mktemp -d) || fail "mktemp failed"
t=/var/lib/build-box/users/$(id -u)/targets/t

cleanup() {
    cd / || :
    for _m in "$t/dev/pts" "$t/dev/shm" "$t/dev" "$t/proc" /dev/pts \
            /dev/shm /root /dev "$work/dev/null"; do
        umount "$_m" 2>/dev/null || :
    done
    rm -rf "$work"
}
trap cleanup EXIT

mount -t tmpfs none /var/lib || fail "could not mount a tmpfs over /var/lib"
mkdir -p /var/lib/build-box/users

mkdir "$work/home"
mount --bind "$work/home" /root || fail "could not bind the caller's home over /root"

# A /dev of our own, see test_commands.sh.  A user namespace can neither
# make the device node /dev/ptmx nor use a bind of the host's, which
# does not find the devpts next to it.  So ptmx is the link into pts
# that the /dev of a container has.
mkdir "$work/dev" "$work/dev/shm" "$work/dev/pts" "$work/other" "$work/other/shm"
: > "$work/dev/null"
ln -s pts/ptmx "$work/dev/ptmx"
mount --bind /dev/null "$work/dev/null" || fail "could not bind /dev/null"
mount --bind "$work/dev" /dev || fail "could not put a /dev of our own in place"

"$CMDDRV" init || fail "init failed"
mkdir -p "$t/dev" "$t/proc" "$t/etc/build-box"
: > "$t/etc/build-box/target-ready"

# options <dir> - the mount options of what is mounted on dir
options() { findmnt -no OPTIONS "$1"; }
fstype() { findmnt -no FSTYPE "$1"; }
propagation() { findmnt -no PROPAGATION "$1"; }
has_option() { case ",$(options "$1")," in *,"$2",*) return 0 ;; esac; return 1; }

# host_mounted <name> - true if something is mounted on /dev/<name>.
#
# What the host has mounted below its /dev is hidden beneath the /dev put
# in place above, but it is still in the mount table, and mountpoint(1)
# goes by that.  The device number tells what is really there.
host_mounted() {
    [ "$(stat -c %d "/dev/$1")" != "$(stat -c %d /dev)" ]
}

# ── what is mounted below dev ────────────────────────────────────────

"$CMDDRV" mount -m dev t || fail "mount failed"
mounted "$t/dev" || fail "dev is not mounted"

[ "$(fstype "$t/dev/shm")" = tmpfs ] || fail "there is no tmpfs on dev/shm"
[ "$(stat -c %a "$t/dev/shm")" = 1777 ] \
    || fail "dev/shm has mode $(stat -c %a "$t/dev/shm"), not 1777"
for o in nosuid nodev; do
    has_option "$t/dev/shm" "$o" || fail "dev/shm lacks $o: $(options "$t/dev/shm")"
done
[ "$(propagation "$t/dev/shm")" = private ] \
    || fail "dev/shm is not private: $(propagation "$t/dev/shm")"
note "dev/shm is a tmpfs with mode 1777, nosuid,nodev and private"

[ "$(fstype "$t/dev/pts")" = devpts ] || fail "there is no devpts on dev/pts"
for o in nosuid noexec mode=600 ptmxmode=666; do
    has_option "$t/dev/pts" "$o" || fail "dev/pts lacks $o: $(options "$t/dev/pts")"
done
[ "$(propagation "$t/dev/pts")" = private ] \
    || fail "dev/pts is not private: $(propagation "$t/dev/pts")"
note "dev/pts is a devpts with mode 600, nosuid,noexec and private"

[ -e "$t/dev/ptmx" ] || fail "the target's ptmx leads nowhere"
exec 3<>"$t/dev/ptmx"
[ -e "$t/dev/pts/0" ] || fail "the pseudo-terminal is not in the target's dev/pts"
exec 3>&-
note "a pseudo-terminal opened through the target's ptmx is in the target's dev/pts"

host_mounted shm && fail "the host's /dev/shm got a mount"
host_mounted pts && fail "the host's /dev/pts got a mount"
note "nothing is mounted below the host's /dev"

"$CMDDRV" mount -m dev t || fail "a second mount failed"
for _m in dev dev/shm dev/pts; do
    [ "$(findmnt -no TARGET "$t/$_m" | wc -l)" -eq 1 ] \
        || fail "a second mount stacked on $_m"
done
note "a second mount is a no-op"

# ── umount ───────────────────────────────────────────────────────────

"$CMDDRV" mount -m proc t || fail "mounting proc failed"
"$CMDDRV" umount -m dev t || fail "umount -m dev failed"
for _m in dev/pts dev/shm dev; do
    mounted "$t/$_m" && fail "$_m is still mounted after umount -m dev"
done
mounted "$t/proc" || fail "umount -m dev took proc down as well"
note "umount -m dev takes down dev and what is mounted below it"

"$CMDDRV" mount -m dev t || fail "mounting dev again failed"
"$CMDDRV" umount t || fail "umount failed"
grep -q " $t/" /proc/mounts && fail "something is still mounted below the target"
note "umount leaves nothing mounted below the target"

"$CMDDRV" mount -m dev t || fail "mounting dev again failed"
exec 3>"$t/dev/shm/busy"
"$CMDDRV" umount t 2>/dev/null && fail "umount reported success with a file open on dev/shm"
mounted "$t/dev/shm" || fail "a busy dev/shm was detached"
mounted "$t/dev" || fail "dev was detached with a busy dev/shm below it"
exec 3>&-
"$CMDDRV" umount t || fail "umount failed after the file was closed"
mounted "$t/dev" && fail "dev is still mounted after umount"
note "a busy mount below dev makes umount fail and dev stays until it is free"

# ── the host's /dev is out of reach ──────────────────────────────────

# A bind of a shared mount is in the same peer group, and a mount below
# one of them shows up below all of them.
mount --make-shared /dev || fail "could not make /dev shared"
mount --bind /dev "$t/dev" || fail "could not bind /dev by hand"
[ "$(propagation "$t/dev")" = shared ] || fail "the bind made by hand is not shared"
"$CMDDRV" mount -m dev t || fail "mount below a shared dev failed"
mounted "$t/dev/shm" || fail "dev/shm is not mounted below a shared dev"
mounted "$t/dev/pts" || fail "dev/pts is not mounted below a shared dev"
host_mounted shm && fail "the mount on dev/shm propagated to the host's /dev"
host_mounted pts && fail "the mount on dev/pts propagated to the host's /dev"
"$CMDDRV" umount t || fail "umount of the dev made by hand failed"
note "a dev mount that was left shared is made private before anything is mounted below it"

# The other way around: what the host mounts below its /dev shows up
# below the shared bind, and an unmount there would take the host's
# mount along.
mount --bind /dev "$t/dev" || fail "could not bind /dev by hand"
mount -t tmpfs none /dev/shm || fail "could not mount a tmpfs on the host's /dev/shm"
mounted "$t/dev/shm" || fail "the host's mount did not show up below the shared dev"
"$CMDDRV" umount t || fail "umount below a shared dev failed"
mounted "$t/dev" && fail "the shared dev is still mounted"
host_mounted shm || fail "the unmount below a shared dev took the host's /dev/shm along"
umount /dev/shm || fail "could not unmount the host's /dev/shm"
mount --make-private /dev || fail "could not make /dev private again"
note "a dev mount that was left shared is made private before anything is unmounted below it"

mount -t tmpfs none /dev/shm || fail "could not mount a tmpfs on the host's /dev/shm"
rmdir "$t/dev" || fail "could not remove the target's dev"
ln -s /dev "$t/dev" || fail "could not put a symlink in the place of dev"
"$CMDDRV" umount t 2>/dev/null
host_mounted shm || fail "the host's /dev/shm was unmounted through a symlink"
"$CMDDRV" mount -m dev t 2>/dev/null && fail "a symlink in the place of dev was accepted"
host_mounted pts && fail "a devpts was mounted on the host's /dev/pts through a symlink"
rm "$t/dev" || fail "could not remove the symlink"
mkdir "$t/dev" || fail "could not put the target's dev back"
umount /dev/shm || fail "could not unmount the host's /dev/shm"
note "a symlink to /dev in the place of dev leads neither mount nor umount to the host's /dev"

mount --bind "$work/other" "$t/dev" || fail "could not bind another directory on dev"
"$CMDDRV" mount -m dev t 2>/dev/null && fail "a mount on dev that is not /dev was accepted"
mounted "$t/dev/shm" && fail "a tmpfs was mounted below a mount that is not /dev"
"$CMDDRV" umount t || fail "umount did not take down the mount that is not /dev"
mounted "$t/dev" && fail "the mount that is not /dev is still there"
note "nothing is mounted below a dev that is not a mount of /dev, and umount still takes it down"

# ── a /dev without the directories ───────────────────────────────────

rm "$work/dev/ptmx" || fail "could not remove ptmx"
rmdir "$work/dev/pts" || fail "could not remove pts"
out=$("$CMDDRV" mount -m dev t 2>&1) || fail "mount failed on a /dev without pts: $out"
mounted "$t/dev/shm" || fail "dev/shm is not mounted on a /dev without pts"
case "$out" in
    *"warning: "*"dev/pts"*) ;;
    *) fail "no warning about the missing pts: '$out'" ;;
esac
"$CMDDRV" umount t || fail "umount failed on a /dev without pts"
mounted "$t/dev" && fail "dev is still mounted on a /dev without pts"
note "a directory the host's /dev lacks is skipped with a warning"

exit 0
