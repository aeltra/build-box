# The Python wrapper's tests, run with pytest from the source directory.
# run.sh skips itself (exit 77) when pytest is not installed, the way the
# shell tests in ../c skip a missing tool.

dist_check_SCRIPTS = run.sh

EXTRA_DIST = \
    conftest.py \
    test_cli.py \
    test_distribution.py \
    test_generator.py \
    test_paths.py \
    test_sysroot.py \
    test_target.py \
    test_wrapper.py

TEST_EXTENSIONS = .sh
SH_LOG_COMPILER = $(SHELL)

AM_TESTS_ENVIRONMENT = \
    srcdir='$(abs_srcdir)'; export srcdir;

TESTS = $(dist_check_SCRIPTS)

clean-local:
	rm -rf .pytest_cache $(srcdir)/.pytest_cache $(srcdir)/__pycache__
