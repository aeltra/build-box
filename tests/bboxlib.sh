# bboxlib.sh - shared helpers for build-box shell tests
#
# Copyright (c) 2026 Tobias Koch <tobias.koch@gmail.com>
# SPDX-License-Identifier: MIT
#
# Sourced by test_*.sh, never executed directly.

skip() { echo "SKIP: $*"; exit 77; }
fail() { echo "FAIL: $*"; exit 1; }
note() { echo "# $*"; }

# Skip the test unless every named tool is on PATH.
require_tools() {
    for _t in "$@"; do
        command -v "$_t" >/dev/null 2>&1 || skip "$_t is not available"
    done
}

# require_harness <VARNAME>
#
# Skip unless the named variable points at a built harness.  The
# harnesses are check_PROGRAMS, so a plain "make" does not build them.
require_harness() {
    eval "_h=\${$1:-}"
    [ -n "$_h" ] && [ -x "$_h" ] || skip "$1 is not built; run make check"
}

# Skip unless an unprivileged user namespace with its own mount, PID and
# network namespace can be created.  Package builds in a locked-down
# chroot cannot, and the suite must not go red over it.
#
# The network namespace is there for sysfs: the kernel only lets a user
# namespace mount sysfs when it owns the network namespace as well.
require_userns() {
    unshare -Urmpfn --mount-proc=/proc true 2>/dev/null \
        || skip "user namespaces are not available"
}

# Re-execute the calling script inside a user namespace, once.  Inside,
# the caller is uid 0, files it owns appear owned by uid 0, and mounting
# is permitted.  BBOX_TEST_INNER marks the inner run.
#
# BBOX_TEST_OUTER_UID records who ran the suite.  A package build runs it
# as root, and root's namespace maps uid 0 to uid 0, so a file the real
# root owns is "owned by the caller" there.  A test that relies on some
# file NOT being the caller's has to check this and note the case as
# skipped -- see test_umount_unbind.sh.
#
# The namespace gets a fresh /proc: build-box-do reads
# /proc/self/fdinfo, and the outer instance answers for the inner PID
# namespace only by accident.
enter_userns() {
    [ "${BBOX_TEST_INNER:-}" = 1 ] && return 0
    require_userns
    BBOX_TEST_INNER=1 export BBOX_TEST_INNER
    BBOX_TEST_OUTER_UID=$(id -u) export BBOX_TEST_OUTER_UID
    exec unshare -Urmpfn --mount-proc=/proc sh "$0" "$@"
}

# mounted <dir> - true if something is mounted on the directory.
mounted() {
    mountpoint -q "$1"
}
