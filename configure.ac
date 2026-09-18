AC_INIT([Build Box NG Utils], [1.0], [tobias.koch@gmail.com],
	[build-box-do], [https://github.com/tobijk/build-box-utils])
AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])
AC_PROG_CC
AC_CONFIG_HEADERS([config.h])

# python3 runs the coverage aggregator and the Python test tier.  Not
# required to build; the tests that need it skip themselves without it.
AC_CHECK_PROGS([PYTHON], [python3], [no])
AC_SUBST([PYTHON])

# gcov instrumentation, for "make coverage".  Off by default: it is a
# measurement build, not a shipping one, and build-box-do is installed
# setuid root, so an instrumented binary must never be the one shipped.
#
# -O0 is not optional.  At -O2 gcc merges and clones basic blocks and the
# line attribution stops meaning anything, so a coverage figure from an
# optimised build measures something other than the source.
#
# The flags go on AM_CFLAGS in the Makefiles rather than onto CFLAGS
# itself, so a coverage build does not quietly discard the flags the user
# asked for -- it is visible in the Makefiles which targets are
# instrumented.  Automake emits "$(AM_CFLAGS) $(CFLAGS)", CFLAGS lands
# last and gcc takes the last -O, so the optimisation level is stripped
# from CFLAGS here, and only that: replacing CFLAGS wholesale would drop
# the user's -march, -D and warning flags with it.
AC_ARG_ENABLE([coverage],
  [AS_HELP_STRING([--enable-coverage],
    [build with gcov instrumentation for "make coverage" @<:@default=no@:>@])],
  [], [enable_coverage=no])

if test "$enable_coverage" = yes; then
    COVERAGE_CFLAGS="-O0 -g --coverage"
    COVERAGE_LDFLAGS="--coverage"
    CFLAGS=`echo "$CFLAGS" | sed -e 's/\(^\| \)-O[[0-9a-z]]*/\1/g'`
    if test "$PYTHON" = no; then
        AC_MSG_ERROR([--enable-coverage needs python3 for the aggregator])
    fi
fi
AC_SUBST([COVERAGE_CFLAGS])
AC_SUBST([COVERAGE_LDFLAGS])
AM_CONDITIONAL([COVERAGE], [test "$enable_coverage" = yes])
AC_CONFIG_FILES([
 Makefile
 c-src/Makefile
 tests/Makefile
 tests/c/Makefile
 tests/python/Makefile
])
AC_OUTPUT
