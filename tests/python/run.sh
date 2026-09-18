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

exec python3 -m pytest -p no:cacheprovider "$srcdir"
