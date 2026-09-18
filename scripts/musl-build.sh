#!/bin/sh -e
# musl-build.sh - build and test build-box against musl libc, in a container.
#
# Copyright (c) 2026 Tobias Koch <tobias.koch@gmail.com>
# SPDX-License-Identifier: MIT
#
# Standalone on purpose: nothing in the build system calls this, and it
# writes nothing into the tree.  Run it by hand after touching code whose
# behaviour could differ between libcs -- option parsing, anything around
# the environment, a libc interface the targets' musl 1.2.2 may lack.
#
# Alpine is the musl distribution here.  Its musl is newer than the one
# in the Aeltra targets, so this is a run against musl, not a run against
# that exact musl; the syntax check against a target's headers is the
# other half.
#
# The source goes in through "git ls-files": exactly the tracked and
# untracked-but-not-ignored files, so no build artifact rides along and no
# --exclude list has to be kept in step.  The host tree is only read, so a
# musl run cannot disturb the working copy's configure state.
#
# What this covers is the C: build-box-do itself and the unit tests.  The
# shell tests need a user namespace, which a container gets only when its
# runtime allows it, and skip otherwise; DOCKER_RUN_FLAGS is the hook for
# a runtime that does allow it.  The Python tier imports the aeltra
# packages, which Alpine does not have, and skips too.

IMAGE=build-box-musl-build
ALPINE_VERSION=3.24

die() {
    echo "error: $1" >&2
    exit 1
}

usage() {
    cat <<USAGE
usage: $0 [--rebuild] [--quiet] [--shell] [-- CONFIGURE_ARGS...]

  --rebuild   rebuild the container image even if it exists
  --quiet     print only the summary, not the build and test output
  --shell     drop into a shell in the built tree instead of running the suite
  --          pass the remaining arguments to configure

  DOCKER_RUN_FLAGS in the environment is passed to "docker run", for a
  runtime that can be told to allow user namespaces.
USAGE
}

REBUILD=no
SHELL_MODE=no
QUIET=no

while [ $# -gt 0 ]; do
    case "$1" in
        --rebuild) REBUILD=yes; shift ;;
        --quiet)   QUIET=yes; shift ;;
        --shell)   SHELL_MODE=yes; shift ;;
        -h|--help) usage; exit 0 ;;
        --)        shift; break ;;
        *)         usage >&2; die "unknown option: $1" ;;
    esac
done

command -v docker >/dev/null 2>&1 || die "docker is not installed"
docker info >/dev/null 2>&1 || die "cannot talk to the docker daemon"

cd "$(dirname "$0")/.." || die "cannot find the top of the tree"
git rev-parse --git-dir >/dev/null 2>&1 || die "this needs a git checkout"

if [ "$REBUILD" = yes ] || ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    echo "building $IMAGE (alpine $ALPINE_VERSION)..."
    # Fed on stdin so the build context is empty: the image fetches
    # everything it needs, and there is nothing here to send.
    docker build -t "$IMAGE" \
        --build-arg "ALPINE_VERSION=$ALPINE_VERSION" - <<'DOCKERFILE' || die "image build failed"
ARG ALPINE_VERSION
FROM alpine:${ALPINE_VERSION}
# util-linux for unshare, mount, findmnt, flock and mountpoint, which the
# shell tests ask for before deciding whether they can run; dash because
# provision_shell copies it into a sysroot.
RUN apk add --no-cache \
        build-base autoconf automake \
        python3 py3-pytest dash diffutils git util-linux coreutils
DOCKERFILE
fi

# The source is mounted as a tarball rather than piped in, so that stdin
# stays free -- --shell needs it for the terminal.
TARBALL=$(mktemp) || die "mktemp failed"
trap 'rm -f "$TARBALL"' EXIT INT TERM
git ls-files -z --cached --others --exclude-standard \
    | tar --null -T - -cf "$TARBALL"

# Everything from here runs under busybox ash: POSIX only, no arrays, no
# PIPESTATUS, no [[ ]].
BUILD='
set -e
exec 5>&1

# Run a command with its output going to the terminal AND to a log,
# returning the exit status of that command.  A plain pipe would return
# the status of tee instead, and ash has no PIPESTATUS: fd 3 carries the
# status out through the command substitution while fd 5 carries the
# output to the real stdout.  The log is what the warning count and the
# failure excerpts are read from afterwards.
run_tee() {
    _log=$1
    shift
    _rc=$( { { "$@" 2>&1; echo $? >&3; } | tee "$_log" >&5; } 3>&1 )
    return "$_rc"
}

quiet() { [ "$QUIET" = yes ]; }
step() { echo; echo "=== $* ==="; }

mkdir -p /work
tar -C /work -xf /src.tar
cd /work

step autoreconf
if quiet; then autoreconf -i >/dev/null 2>&1; else run_tee /tmp/autoreconf.log autoreconf -i; fi

mkdir -p build
cd build

step configure $CONFIGURE_ARGS
if quiet; then
    ../configure $CONFIGURE_ARGS >/tmp/configure.log 2>&1 || {
        tail -30 /tmp/configure.log; echo "configure failed" >&2; exit 1; }
else
    run_tee /tmp/configure.log ../configure $CONFIGURE_ARGS || {
        echo "configure failed" >&2; exit 1; }
fi

step make
if quiet; then
    make -j"$(nproc)" >/tmp/make.log 2>&1 || {
        grep -E "error:" /tmp/make.log | head -30; echo "build failed" >&2; exit 1; }
else
    run_tee /tmp/make.log make -j"$(nproc)" || { echo "build failed" >&2; exit 1; }
fi

warnings=$(grep -c "warning:" /tmp/make.log || true)
echo
echo "built against musl $(apk info musl 2>/dev/null | head -1 | cut -d- -f2): $warnings warnings"
[ "$warnings" = 0 ] || grep "warning:" /tmp/make.log | head -20
'

CHECK='
step "make check"
if quiet; then
    if make check >/tmp/check.log 2>&1; then rc=0; else rc=$?; fi
else
    run_tee /tmp/check.log make check && rc=0 || rc=$?
fi
echo
grep -E "^# (TOTAL|PASS|SKIP|FAIL|ERROR):" /tmp/check.log || true
skipped=$(grep -E "^SKIP:" /tmp/check.log || true)
[ -z "$skipped" ] || { echo; echo "$skipped"; }
failed=$(grep -E "^(FAIL|ERROR):" /tmp/check.log || true)
if [ -n "$failed" ]; then
    echo
    echo "$failed"
    for t in $(echo "$failed" | sed -e "s/^[A-Z]*: //" -e "s/\.sh$//"); do
        for log in tests/c/$t.log tests/python/$t.log; do
            [ -f "$log" ] || continue
            echo "--- $log ---"
            tail -25 "$log"
        done
    done
fi
exit $rc
'

SHELL_IN='
echo
echo "--- musl shell; the build is in /work/build.  ^D to leave. ---"
exec sh
'

if [ "$SHELL_MODE" = yes ]; then
    [ -t 0 ] || die "--shell needs a terminal on stdin"
    RUN_FLAGS="-it"
    SCRIPT="$BUILD$SHELL_IN"
else
    RUN_FLAGS="-i"
    SCRIPT="$BUILD$CHECK"
fi

exec docker run --rm $RUN_FLAGS ${DOCKER_RUN_FLAGS:-} \
    -v "$TARBALL:/src.tar:ro" \
    -e "CONFIGURE_ARGS=$*" \
    -e "QUIET=$QUIET" \
    "$IMAGE" sh -c "$SCRIPT"
