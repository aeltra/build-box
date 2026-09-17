AM_CPPFLAGS = \
    -I$(top_builddir) \
    -I$(top_srcdir)/c-src \
    -I$(srcdir)

# Unit tests run as tests; umountdrv is a harness the shell tests drive,
# so it is built but never run on its own.
unit_tests = test_path_join

check_PROGRAMS = $(unit_tests) umountdrv

noinst_HEADERS = test.h

# util.c reaches into the configuration accessors, so config.c comes
# along wherever util.c does.
#
# The per-target CPPFLAGS carry nothing extra.  They are there so that
# automake gives each program its own object names: the sources in
# c-src/ are also built by c-src/Makefile, and without the rename both
# would write the same util.o.
test_path_join_SOURCES = \
    test_path_join.c \
    $(top_srcdir)/c-src/util.c \
    $(top_srcdir)/c-src/config.c
test_path_join_CPPFLAGS = $(AM_CPPFLAGS)

# Calls bbox_umount_unbind() with the arguments it is given.  The
# privileged half of build-box-do cannot be unit-tested as a plain user,
# but inside a user namespace uid 0 is the caller and bind mounts are
# permitted, which is where test_umount_unbind.sh runs it.
umountdrv_SOURCES = \
    umountdrv.c \
    $(top_srcdir)/c-src/umount.c \
    $(top_srcdir)/c-src/util.c \
    $(top_srcdir)/c-src/config.c
umountdrv_CPPFLAGS = $(AM_CPPFLAGS)

# Shell tests skip themselves (exit 77) when what they need is missing:
# user namespaces, or a tool.
dist_check_SCRIPTS = \
    test_umount_unbind.sh

EXTRA_DIST = bboxlib.sh

TEST_EXTENSIONS = .sh

# Run shell tests through $(SHELL) so they work even if the executable
# bit is lost in a checkout or a release tarball.
SH_LOG_COMPILER = $(SHELL)

AM_TESTS_ENVIRONMENT = \
    srcdir='$(abs_srcdir)'; export srcdir; \
    UMOUNTDRV='$(abs_builddir)/umountdrv'; export UMOUNTDRV;

TESTS = $(unit_tests) $(dist_check_SCRIPTS)
