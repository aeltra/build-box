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

# The unshare flags for the test sandbox: an unprivileged user namespace
# with its own mount, PID and network namespace.  Prints nothing and
# fails if none can be created -- package builds in a locked-down chroot
# cannot, and the suite must not go red over it.
#
# The network namespace is there for sysfs: the kernel only lets a user
# namespace mount sysfs when it owns the network namespace as well.
#
# --map-auto is preferred: it maps the caller's subordinate id ranges
# through newuidmap/newgidmap, and a namespace set up that way is allowed
# to call setgroups(), which build-box's run path does.  A plain -r
# namespace has setgroups denied by the kernel.  Without subordinate ids
# the plain form is used and tests needing setgroups skip themselves;
# see require_setgroups.
#
# The probe runs under a time limit and with every descriptor pointed at
# /dev/null.  Under qemu-user emulation, which is how the arm64 package
# is built, unshare with a new PID namespace comes back as a failure but
# leaves a process behind that never exits.  The limit is for a probe
# that does not return at all; the redirection is for that orphan, which
# would otherwise inherit the pipe of the $(userns_flags) around this and
# hold it open, and the caller would wait for an end of file that never
# comes.  Either way, no user namespace here.
userns_flags() {
    if timeout -k 5 10 unshare -Urmpfn --map-auto --mount-proc=/proc true >/dev/null 2>&1; then
        echo "-Urmpfn --map-auto --mount-proc=/proc"
    elif timeout -k 5 10 unshare -Urmpfn --mount-proc=/proc true >/dev/null 2>&1; then
        echo "-Urmpfn --mount-proc=/proc"
    else
        return 1
    fi
}

require_userns() {
    userns_flags >/dev/null \
        || skip "user namespaces are not available, or the probe for them did not return"
}

# Inside the namespace: skip unless setgroups() is permitted.
require_setgroups() {
    [ "$(cat /proc/self/setgroups 2>/dev/null)" = allow ] \
        || skip "setgroups is denied in this namespace; $(id -un) has no subordinate ids"
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
    _flags=$(userns_flags) || skip "user namespaces are not available"
    BBOX_TEST_INNER=1 export BBOX_TEST_INNER
    BBOX_TEST_OUTER_UID=$(id -u) export BBOX_TEST_OUTER_UID
    exec unshare $_flags sh "$0" "$@"
}

# mounted <dir> - true if something is mounted on the directory.
mounted() {
    mountpoint -q "$1"
}

# provision_shell <sysroot>
#
# Copy dash and the libraries it needs into the sysroot as /usr/bin/sh,
# which is where build-box looks for a shell.  Returns non-zero if there
# is no dash or a copy fails, so the caller can skip.
provision_shell() {
    _r=$1
    _sh=$(command -v dash) || return 1

    mkdir -p "$_r/usr/bin" || return 1
    cp "$_sh" "$_r/usr/bin/sh" || return 1

    for _lib in $(ldd "$_sh" 2>/dev/null | grep -o '/[^ ]*' | grep '\.so'); do
        [ -e "$_lib" ] || continue
        mkdir -p "$_r$(dirname "$_lib")" || return 1
        cp "$_lib" "$_r$_lib" || return 1
    done

    return 0
}
