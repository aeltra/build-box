AM_CPPFLAGS = \
    -I$(top_builddir) \
    -I$(top_srcdir)/c-src \
    -I$(srcdir)

# Empty unless configured with --enable-coverage.  Every program here
# compiles the c-src files it needs into itself, so the instrumentation
# has to be on these builds, not on build-box-do.
AM_CFLAGS = $(COVERAGE_CFLAGS)
AM_LDFLAGS = $(COVERAGE_LDFLAGS)

# Unit tests run as tests; umountdrv and mountdrv are harnesses the shell
# tests drive, so they are built but never run on their own.
unit_tests = test_path_join test_copy_file test_mkdir_p \
    test_sanitize_environment test_config test_getopt test_target_name \
    test_dynamic_config

check_PROGRAMS = $(unit_tests) umountdrv mountdrv rundrv initdrv cmddrv \
    groupdrv

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

test_mkdir_p_SOURCES = \
    test_mkdir_p.c \
    $(top_srcdir)/c-src/util.c \
    $(top_srcdir)/c-src/config.c
test_mkdir_p_CPPFLAGS = $(AM_CPPFLAGS)

test_sanitize_environment_SOURCES = \
    test_sanitize_environment.c \
    $(top_srcdir)/c-src/util.c \
    $(top_srcdir)/c-src/config.c
test_sanitize_environment_CPPFLAGS = $(AM_CPPFLAGS)

test_config_SOURCES = \
    test_config.c \
    $(top_srcdir)/c-src/util.c \
    $(top_srcdir)/c-src/config.c
test_config_CPPFLAGS = $(AM_CPPFLAGS)

# The option handlers live in the command modules, so all five come
# along; bbox-do.c stays out because it has the real main().
test_getopt_SOURCES = \
    test_getopt.c \
    $(top_srcdir)/c-src/init.c \
    $(top_srcdir)/c-src/login.c \
    $(top_srcdir)/c-src/mount.c \
    $(top_srcdir)/c-src/run.c \
    $(top_srcdir)/c-src/umount.c \
    $(top_srcdir)/c-src/util.c \
    $(top_srcdir)/c-src/config.c
test_getopt_CPPFLAGS = $(AM_CPPFLAGS)

test_target_name_SOURCES = \
    test_target_name.c \
    $(top_srcdir)/c-src/util.c \
    $(top_srcdir)/c-src/config.c
test_target_name_CPPFLAGS = $(AM_CPPFLAGS)

test_dynamic_config_SOURCES = \
    test_dynamic_config.c \
    $(top_srcdir)/c-src/util.c \
    $(top_srcdir)/c-src/config.c
test_dynamic_config_CPPFLAGS = $(AM_CPPFLAGS)

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

# The command entry points themselves, driven by test_commands.sh: every
# module but bbox-do.c, which has the real main().
cmddrv_SOURCES = \
    cmddrv.c \
    $(top_srcdir)/c-src/init.c \
    $(top_srcdir)/c-src/login.c \
    $(top_srcdir)/c-src/mount.c \
    $(top_srcdir)/c-src/run.c \
    $(top_srcdir)/c-src/umount.c \
    $(top_srcdir)/c-src/util.c \
    $(top_srcdir)/c-src/config.c
cmddrv_CPPFLAGS = $(AM_CPPFLAGS)

# The group gate, driven by test_group_check.sh.
groupdrv_SOURCES = \
    groupdrv.c \
    $(top_srcdir)/c-src/util.c \
    $(top_srcdir)/c-src/config.c
groupdrv_CPPFLAGS = $(AM_CPPFLAGS)

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
    test_commands.sh \
    test_group_check.sh \
    test_init_user_dir.sh \
    test_isolate.sh \
    test_isolate_proc.sh \
    test_lock_wait.sh \
    test_main.sh \
    test_mount.sh \
    test_mount_race.sh \
    test_mount_undo.sh \
    test_no_new_privs.sh \
    test_umount_unbind.sh

EXTRA_DIST = bboxlib.sh coverage-report.py

TEST_EXTENSIONS = .sh

# Run shell tests through $(SHELL) so they work even if the executable
# bit is lost in a checkout or a release tarball.
SH_LOG_COMPILER = $(SHELL)

AM_TESTS_ENVIRONMENT = \
    srcdir='$(abs_srcdir)'; export srcdir; \
    UMOUNTDRV='$(abs_builddir)/umountdrv'; export UMOUNTDRV; \
    MOUNTDRV='$(abs_builddir)/mountdrv'; export MOUNTDRV; \
    RUNDRV='$(abs_builddir)/rundrv'; export RUNDRV; \
    INITDRV='$(abs_builddir)/initdrv'; export INITDRV; \
    CMDDRV='$(abs_builddir)/cmddrv'; export CMDDRV; \
    GROUPDRV='$(abs_builddir)/groupdrv'; export GROUPDRV; \
    BBOX_DO='$(abs_top_builddir)/c-src/build-box-do'; export BBOX_DO;

TESTS = $(unit_tests) $(dist_check_SCRIPTS)
