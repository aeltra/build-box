#!/bin/sh
# test_inherited_fds.sh - a session cannot use a directory descriptor it
# inherited to reach outside the target.
#
# Copyright (c) 2026 Tobias Koch <tobias.koch@gmail.com>
# SPDX-License-Identifier: MIT
#
# build-box-do runs whatever it is given with the descriptors of its
# caller.  A directory descriptor opened outside the target survives
# chroot(), and through /proc/self/fd/N, or fchdir(), it leads back out
# to the host's tree.  The caller is usually a shell or a CI agent that
# leaked it by accident, while the command in the session is a build
# that should not see the host.
#
# build-box-do closes such descriptors before it enters the target, in
# both the plain and the isolated run path.  Every other descriptor is
# passed through: a pipe, a file or a socket gives access to that one
# object, and a caller may pass one on purpose, e.g. "3>build.log".
#
# The test opens a directory outside the sysroot as descriptor 3, a log
# file as descriptor 4 and, when python3 is available, the same
# directory as an O_PATH descriptor 5, then runs a session through
# bbox_runas_user_chrooted().  The session must not be able to read a
# file in that directory, and must still be able to write to the log.
#
# Runs inside an unprivileged user namespace, like test_no_new_privs.sh.

set -u

. "${srcdir:-.}/bboxlib.sh"

require_harness RUNDRV
require_tools unshare mount
enter_userns
require_setgroups

work=$(mktemp -d) || fail "mktemp failed"

cleanup() {
    cd / || :
    umount "$work/root/proc" 2>/dev/null || :
    rm -rf "$work"
}
trap cleanup EXIT

cd "$work" || fail "cd $work"

mkdir -p root/proc outside
provision_shell root || skip "cannot provision a shell into the sysroot"
echo "host secret" > outside/secret

# Pure shell, placed in the sysroot.  Prints what it can read through
# each descriptor, and writes to the log through descriptor 4.  There
# is no /dev in the sysroot, so a failed read complains on stderr.
cat > root/probe.sh <<'EOF'
for fd in 3 5; do
    if read l < /proc/self/fd/$fd/secret; then
        echo "fd $fd: $l"
    fi
done
echo "written from the session" >&4
EOF

# run_probe [--isolate] - run the probe with the descriptors open.
run_probe() {
    : > log
    if command -v python3 >/dev/null 2>&1; then
        python3 - "$RUNDRV" "$@" root "sh /probe.sh" 3<outside 4>>log <<'EOF'
import os, sys
fd = os.open("outside", os.O_PATH | os.O_DIRECTORY)
os.dup2(fd, 5)
os.set_inheritable(5, True)
os.execv(sys.argv[1], sys.argv[1:])
EOF
    else
        "$RUNDRV" "$@" root "sh /probe.sh" 3<outside 4>>log
    fi
}

# ── control: the descriptors do lead outside without build-box ───────

mount -t proc none root/proc || fail "proc mount"

out=$(sh -c 'read l < /proc/self/fd/3/secret && echo "$l"' 3<outside)
[ "$out" = "host secret" ] || skip "a directory descriptor does not lead outside here: '$out'"

# ── plain mode ───────────────────────────────────────────────────────

out=$(run_probe) || fail "the plain run failed: $out"
[ -z "$out" ] || fail "a plain session read outside the target: '$out'"
[ "$(cat log)" = "written from the session" ] \
    || fail "a plain session could not write to an inherited file: '$(cat log)'"
note "a plain session gets no directory descriptor, but keeps the log"

# A directory on stdin is replaced by /dev/null rather than closed, so
# the next file the session opens does not become its stdin.
out=$("$RUNDRV" root 'read l < /proc/self/fd/0/secret; echo "$l"' <outside) \
    || fail "the run with a directory on stdin failed: $out"
[ -z "$out" ] || fail "a session read outside the target through stdin: '$out'"
note "a directory on stdin does not lead outside either"

umount root/proc || fail "umount proc"

# ── isolated mode ────────────────────────────────────────────────────
#
# The child mounts its own proc inside the PID namespace.

out=$(run_probe --isolate) || fail "the isolated run failed: $out"
[ -z "$out" ] || fail "an isolated session read outside the target: '$out'"
[ "$(cat log)" = "written from the session" ] \
    || fail "an isolated session could not write to an inherited file: '$(cat log)'"
note "an isolated session gets no directory descriptor, but keeps the log"

exit 0
