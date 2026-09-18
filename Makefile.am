SUBDIRS=c-src tests

# ── coverage ─────────────────────────────────────────────────────────────
#
# Needs ./configure --enable-coverage, and its own build directory: a
# CFLAGS change does not force a recompile, so a tree that was built
# without instrumentation stays uninstrumented and the report comes out
# empty (or worse, stale).
#
#   make coverage         run the suite, report per file
#   make coverage-lines   which lines of a file no test reached
#
# A report, not a gate: there are no thresholds and nothing to regress
# against.  The C figure covers what gcov can see; login.c and run.c end
# in an exec or sit in a chroot when the process ends, so their counters
# are never written, and the shell tests that drive them are the only
# coverage those two files have.  See tests/c/coverage-report.py.
#
# The suite is run serially.  Parallel processes merging into the same
# .gcda is a race, and a race in the measurement is worse than a slow
# measurement.
COVERAGE_REPORT = $(PYTHON) $(top_srcdir)/tests/c/coverage-report.py \
	--build-dir $(top_builddir) --srcdir $(top_srcdir)

check_coverage_enabled = \
	if test -z '$(COVERAGE_CFLAGS)'; then \
	    echo "not a coverage build; reconfigure with --enable-coverage" >&2; \
	    echo "  (in a separate build directory -- a CFLAGS change does not" >&2; \
	    echo "   force a recompile, so an existing tree stays stale)" >&2; \
	    exit 1; \
	fi

# The counters are cleared first, so the figure depends on the tree and
# not on how many times the suite has run.  To accumulate deliberately,
# run "make check" as often as you like and invoke the script directly.
# A failing suite does not invalidate the numbers, so this reports either
# way -- but a red suite is a red suite, and make says so above.
#
# The Python tier is measured with pytest-cov when it is installed, from
# the source tree since that is where the package is; the data file is
# kept in the build tree.
coverage:
	@$(check_coverage_enabled)
	@$(MAKE) $(AM_MAKEFLAGS) coverage-clean
	-@$(MAKE) $(AM_MAKEFLAGS) -j1 check
	@if test -n "`find $(top_builddir) -name '*.gcda' -print -quit`"; then :; else \
	    echo "no profile data was produced" >&2; exit 1; fi
	@$(COVERAGE_REPORT) --uncovered
	@if $(PYTHON) -c 'import pytest_cov' 2>/dev/null; then \
	    cd $(top_srcdir) && COVERAGE_FILE=$(abs_top_builddir)/.coverage \
	        $(PYTHON) -m pytest -q -p no:cacheprovider \
	        --cov=lib/aeltra/buildbox --cov-report=term-missing tests/python \
	        | sed -n '/coverage:/,$$p'; \
	else \
	    echo "  (pytest-cov is not installed; no figure for the Python tier)"; \
	fi

# Which lines, rather than which files.  Reads whatever the .gcda already
# hold, so it neither clears them nor re-runs the suite: run "make
# coverage" first, then ask this about a file.
#
#   make coverage-lines FILE=c-src/util.c
#   make coverage-lines FILE="util.c mount.c" COVERAGE_LINES_FLAGS=--branches
coverage-lines:
	@$(check_coverage_enabled)
	@test -n "$(FILE)" || { echo "usage: make coverage-lines FILE=c-src/foo.c" >&2; exit 2; }
	@$(COVERAGE_REPORT) --lines $(FILE) $(COVERAGE_LINES_FLAGS)

coverage-clean:
	@find $(top_builddir) -name '*.gcda' -delete
	@rm -f $(top_builddir)/.coverage

.PHONY: coverage coverage-lines coverage-clean
