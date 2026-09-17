AM_CPPFLAGS = \
    -I$(top_builddir) \
    -I$(top_srcdir)/c-src \
    -I$(srcdir)

# Unit tests run as tests; umountdrv and mountdrv are harnesses the shell
# tests drive, so they are built but never run on their own.
unit_tests = test_path_join test_copy_file

check_PROGRAMS = $(unit_tests) umountdrv mountdrv rundrv initdrv

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

test_copy_file_SOURCES = \
    test_copy_file.c \
    $(top_srcdir)/c-src/util.c \
    $(top_srcdir)/c-src/config.c
test_copy_file_CPPFLAGS = $(AM_CPPFLAGS)

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

# The run path, driven by test_no_new_privs.sh.  run.c needs mount.c for
# bbox_mount_any() and umount.c comes along for the mount helpers.
rundrv_SOURCES = \
    rundrv.c \
    $(top_srcdir)/c-src/run.c \
    $(top_srcdir)/c-src/mount.c \
    $(top_srcdir)/c-src/umount.c \
    $(top_srcdir)/c-src/util.c \
    $(top_srcdir)/c-src/config.c
rundrv_CPPFLAGS = $(AM_CPPFLAGS)

# The per-user directory setup, driven by test_init_user_dir.sh.
initdrv_SOURCES = \
    initdrv.c \
    $(top_srcdir)/c-src/init.c \
    $(top_srcdir)/c-src/util.c \
    $(top_srcdir)/c-src/config.c
initdrv_CPPFLAGS = $(AM_CPPFLAGS)

# The same for bbox_mount_bind() and bbox_mount_special(), driven by
# test_mount.sh.
mountdrv_SOURCES = \
    mountdrv.c \
    $(top_srcdir)/c-src/mount.c \
    $(top_srcdir)/c-src/umount.c \
    $(top_srcdir)/c-src/util.c \
    $(top_srcdir)/c-src/config.c
mountdrv_CPPFLAGS = $(AM_CPPFLAGS)

# Shell tests skip themselves (exit 77) when what they need is missing:
# user namespaces, or a tool.
dist_check_SCRIPTS = \
    test_init_user_dir.sh \
    test_isolate.sh \
    test_mount.sh \
    test_no_new_privs.sh \
    test_umount_unbind.sh

EXTRA_DIST = bboxlib.sh

TEST_EXTENSIONS = .sh

# Run shell tests through $(SHELL) so they work even if the executable
# bit is lost in a checkout or a release tarball.
SH_LOG_COMPILER = $(SHELL)

AM_TESTS_ENVIRONMENT = \
    srcdir='$(abs_srcdir)'; export srcdir; \
    UMOUNTDRV='$(abs_builddir)/umountdrv'; export UMOUNTDRV; \
    MOUNTDRV='$(abs_builddir)/mountdrv'; export MOUNTDRV; \
    RUNDRV='$(abs_builddir)/rundrv'; export RUNDRV; \
    INITDRV='$(abs_builddir)/initdrv'; export INITDRV;

TESTS = $(unit_tests) $(dist_check_SCRIPTS)
