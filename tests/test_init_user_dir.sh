#!/bin/sh
# test_init_user_dir.sh - the per-user directory is created private.
#
# Copyright (c) 2026 Tobias Koch <tobias.koch@gmail.com>
# SPDX-License-Identifier: MIT
#
# /var/lib/build-box/users/<uid> holds the user's targets, and each target
# holds a per-target home with dotfiles and shell history.  It used to be
# created 0755, so every local user could read another user's history
# and whatever credentials tools had written there.  It is now created
# 0700.  A directory that already exists keeps its mode: tightening it
# behind the user's back could undo a deliberate choice.
#
# The path is compiled into build-box-do, so the test runs in a user
# namespace and mounts a tmpfs over /var/lib there.

set -u

. "${srcdir:-.}/bboxlib.sh"

require_harness INITDRV
require_tools unshare mount stat
enter_userns

mount -t tmpfs none /var/lib || fail "could not mount a tmpfs over /var/lib"
mkdir -p /var/lib/build-box/users

dir=/var/lib/build-box/users/$(id -u)

"$INITDRV" || fail "init failed"
[ -d "$dir" ] || fail "$dir was not created"
[ "$(stat -c %a "$dir")" = 700 ] || fail "$dir is mode $(stat -c %a "$dir"), not 700"
[ "$(stat -c %u "$dir")" = "$(id -u)" ] || fail "$dir is not owned by the caller"
note "the per-user directory is created 0700 and owned by the caller"

"$INITDRV" || fail "a second init failed"
[ "$(stat -c %a "$dir")" = 700 ] || fail "a second init changed the mode"
note "a second init is a no-op"

rm -rf "$dir"
mkdir -m 755 "$dir"
"$INITDRV" || fail "init with an existing directory failed"
[ "$(stat -c %a "$dir")" = 755 ] || fail "an existing directory was changed to $(stat -c %a "$dir")"
note "an existing directory keeps its mode"

exit 0
