#!/bin/sh
# test_main.sh - what build-box-do does before it does anything: the
# wrapper token, the root refusal, the group gate and the dispatch.
#
# Copyright (c) 2026 Tobias Koch <tobias.koch@gmail.com>
# SPDX-License-Identifier: MIT
#
# Runs the built binary itself, unprivileged.  It is not setuid in the
# build tree, and nothing here needs it to be: every case ends before a
# privileged operation.  Whether the caller passes the group gate is a
# property of this host, so that case asserts agreement with id(1)
# rather than a fixed answer.  The root refusal is checked inside a user
# namespace, where the caller is uid 0, when one can be created.

set -u

. "${srcdir:-.}/bboxlib.sh"

[ -n "${BBOX_DO:-}" ] && [ -x "$BBOX_DO" ] || skip "BBOX_DO is not built; run make check"
require_tools id

work=$(mktemp -d) || fail "mktemp failed"
trap 'rm -rf "$work"' EXIT

unset BUILD_BOX_WRAPPER_A883DAFC

# ── the wrapper token ────────────────────────────────────────────────

"$BBOX_DO" init 2>"$work/err"
rc=$?
[ "$rc" -eq 254 ] || fail "without the token the binary exited $rc, not 254"
grep -q "not be invoked directly" "$work/err" || fail "the refusal was not explained: $(cat "$work/err")"
note "without the wrapper token nothing runs"

export BUILD_BOX_WRAPPER_A883DAFC=1

# ── the root refusal ─────────────────────────────────────────────────

if flags=$(userns_flags); then
    unshare $flags "$BBOX_DO" init 2>"$work/err"
    rc=$?
    [ "$rc" -eq 254 ] || fail "as root the binary exited $rc, not 254"
    grep -q "must not be used by root" "$work/err" || fail "the refusal was not explained: $(cat "$work/err")"
    note "root is refused"
else
    note "SKIP: no user namespace, the root refusal is not checked"
fi

# ── the group gate ───────────────────────────────────────────────────

if id -nG | tr ' ' '\n' | grep -qx build-box; then
    "$BBOX_DO" init --help >/dev/null 2>"$work/err" || fail "a member of build-box was refused: $(cat "$work/err")"
    note "a member of group build-box is admitted"
else
    "$BBOX_DO" init --help >/dev/null 2>"$work/err"
    rc=$?
    [ "$rc" -eq 254 ] || fail "a non-member exited $rc, not 254"
    grep -q "build-box" "$work/err" || fail "the refusal did not name the group: $(cat "$work/err")"
    note "a caller outside group build-box is refused"
    exit 0
fi

# ── dispatch, for a member ───────────────────────────────────────────

"$BBOX_DO" >"$work/out" 2>&1
rc=$?
[ "$rc" -eq 254 ] || fail "without a command the binary exited $rc, not 254"
grep -q "USAGE" "$work/out" || fail "without a command no usage was printed"
note "without a command the usage is printed and the exit status is 254"

"$BBOX_DO" --help >"$work/out" 2>&1 || fail "--help failed"
grep -q "USAGE" "$work/out" || fail "--help printed no usage"
"$BBOX_DO" -h >/dev/null 2>&1 || fail "-h failed"
note "--help and -h print the usage and exit 0"

"$BBOX_DO" frobnicate 2>"$work/err"
rc=$?
[ "$rc" -eq 254 ] || fail "an unknown command exited $rc, not 254"
grep -q "unknown command" "$work/err" || fail "an unknown command was not named: $(cat "$work/err")"
note "an unknown command is an invocation error"

for cmd in init login mount umount run; do
    "$BBOX_DO" $cmd --help >"$work/out" 2>&1 || fail "$cmd --help failed"
    grep -q "USAGE" "$work/out" || fail "$cmd --help printed no usage"
done
note "every command is reachable and answers --help"

exit 0
