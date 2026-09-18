#!/bin/sh
# test_no_new_privs.sh - a session inside a target cannot gain privileges
# through exec.
#
# Copyright (c) 2026 Tobias Koch <tobias.koch@gmail.com>
# SPDX-License-Identifier: MIT
#
# The sysroot is a tree the user wrote, on the same filesystem as /usr.
# Without fs.protected_hardlinks a root-owned setuid binary can be
# hard-linked into it, and after chroot() it reads the user's own
# passwd, shadow, PAM configuration and libc.  build-box now sets
# PR_SET_NO_NEW_PRIVS before executing anything in the chroot, so no
# exec in the session grants privileges, whatever the tree contains.
#
# The flag shows up as "NoNewPrivs: 1" in /proc/self/status of every
# process in the session.  That is asserted through the real run path,
# bbox_runas_user_chrooted(), in both its plain and its isolated mode,
# with a shell provisioned into a throwaway sysroot.  A control run
# outside confirms the flag is not simply inherited from the harness.
#
# Runs inside an unprivileged user namespace: chroot() and mounting
# proc are permitted there, and the sysroot the test creates belongs to
# uid 0, which is what the run path requires of the caller.  The run
# path also resets the supplementary groups, which the kernel only
# permits in a namespace set up through the subordinate id ranges.

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

mkdir -p root/proc
provision_shell root || skip "cannot provision a shell into the sysroot"

# Pure shell, so nothing but /usr/bin/sh needs to exist in the sysroot.
# Placed inside it as /nnp.sh, so the nested case below can invoke it
# through another shell without any quoting.
cat > root/nnp.sh <<'EOF'
while read l; do
    case $l in
        NoNewPrivs*) echo "$l" ;;
    esac
done < /proc/self/status
EOF

# ── control: the flag is off before build-box sets it ────────────────

case $(sh root/nnp.sh) in
    *0) ;;
    *) skip "no_new_privs is already set in the test environment" ;;
esac

# ── plain mode ───────────────────────────────────────────────────────

mount -t proc none root/proc || fail "proc mount"

out=$("$RUNDRV" root "sh /nnp.sh") || fail "the plain run failed: $out"
case $out in
    NoNewPrivs*1) ;;
    *) fail "no_new_privs is not set in a plain session: '$out'" ;;
esac
note "a plain session runs with no_new_privs"

umount root/proc || fail "umount proc"

# ── isolated mode ────────────────────────────────────────────────────
#
# The child mounts its own proc inside the PID namespace, so none is
# mounted here.

out=$("$RUNDRV" --isolate root "sh /nnp.sh") || fail "the isolated run failed: $out"
case $out in
    NoNewPrivs*1) ;;
    *) fail "no_new_privs is not set in an isolated session: '$out'" ;;
esac
note "an isolated session runs with no_new_privs"

# ── and it is inherited by everything the session starts ─────────────

out=$("$RUNDRV" --isolate root "sh -c 'sh /nnp.sh'") || fail "the nested run failed: $out"
case $out in
    NoNewPrivs*1) ;;
    *) fail "no_new_privs is not inherited by a grandchild: '$out'" ;;
esac
note "the flag is inherited across exec"

exit 0
