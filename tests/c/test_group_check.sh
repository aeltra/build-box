#!/bin/sh
# test_group_check.sh - membership in group "build-box" is what admits a
# caller, by primary or supplementary group, and nothing else does.
#
# Copyright (c) 2026 Tobias Koch <tobias.koch@gmail.com>
# SPDX-License-Identifier: MIT
#
# The check is the authorization gate of the setuid binary.  The group
# database is under the test's control here: a file of ours is bound
# over /etc/group inside the user namespace, and the files backend of
# the C library reads it fresh on every lookup.  The caller is uid 0 in
# the namespace with primary group 0, so a "build-box" line with gid 0
# is membership by primary group; a line with another gid is membership
# only through setgroups(), which the kernel permits in a namespace set
# up through the subordinate id ranges and which the harness does on
# request.

set -u

. "${srcdir:-.}/bboxlib.sh"

require_harness GROUPDRV
require_tools unshare mount awk
enter_userns

work=$(mktemp -d) || fail "mktemp failed"

cleanup() {
    umount /etc/group 2>/dev/null || :
    rm -rf "$work"
}
trap cleanup EXIT

# groups <lines...> - install a group file with exactly these lines
groups() {
    umount /etc/group 2>/dev/null || :
    printf '%s\n' "$@" > "$work/group"
    mount --bind "$work/group" /etc/group || fail "could not bind the group file"
}

# ── the group has to exist ───────────────────────────────────────────

groups "root:x:0:" "users:x:100:"
"$GROUPDRV" 2>"$work/err" && fail "a missing build-box group admitted the caller"
grep -q "not found" "$work/err" || fail "a missing group was not named as the reason: $(cat "$work/err")"
note "without a build-box group nobody is admitted"

# ── membership by primary group ──────────────────────────────────────

groups "build-box:x:0:"
"$GROUPDRV" || fail "membership by primary group was refused"
note "the primary group admits the caller"

# ── another gid is not membership ────────────────────────────────────

groups "root:x:0:" "build-box:x:4242:"
"$GROUPDRV" 2>"$work/err" && fail "a caller outside the group was admitted"
grep -q "not in group" "$work/err" || fail "the refusal was not explained: $(cat "$work/err")"
note "a caller outside the group is refused"

# ── membership by supplementary group ────────────────────────────────

if [ "$(cat /proc/self/setgroups 2>/dev/null)" != allow ]; then
    note "SKIP: setgroups is denied in this namespace, supplementary membership not checked"
    exit 0
fi

"$GROUPDRV" 4242 || fail "membership by supplementary group was refused"
note "a supplementary group admits the caller"

"$GROUPDRV" 100 200 300 && fail "other supplementary groups admitted the caller"
note "other supplementary groups do not"

# The lookup buffer starts at 1 KiB and grows on ERANGE.  A member list
# well past that exercises the growth rather than a first-try hit.
members=$(awk 'BEGIN { for (i = 0; i < 400; i++) printf "%smember%03d", (i ? "," : ""), i }')
groups "root:x:0:" "build-box:x:4242:$members"
"$GROUPDRV" 4242 || fail "a group with a long member list was not found"
note "a group entry larger than the first lookup buffer is read"

exit 0
