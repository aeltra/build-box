#!/bin/sh
# run.sh - run the Python tests with pytest.
#
# Copyright (c) 2026 Tobias Koch <tobias.koch@gmail.com>
# SPDX-License-Identifier: MIT
#
# Started by make check with srcdir pointing at this directory, which
# may differ from the build directory.  pytest finds pyproject.toml above
# the tests and takes its options from there.  Skips (exit 77) when
# pytest is not installed, so a build without it stays green but says so.

srcdir=${srcdir:-.}

python3 -c 'import pytest' 2>/dev/null || { echo "SKIP: pytest is not available"; exit 77; }

# The wrapper imports the other aeltra packages, which come from the host
# installation rather than from this tree.  Without them every test would
# fail at import, on a machine that tells nothing about build-box -- the
# musl container, for one.
python3 -c 'import aeltra.error, aeltra.osimage.sysroot, aeltra.distro.config.distroinfo' 2>/dev/null \
    || { echo "SKIP: the aeltra Python packages are not installed"; exit 77; }

exec python3 -m pytest -p no:cacheprovider "$srcdir"
