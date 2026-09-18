bin_PROGRAMS = build-box-do
build_box_do_SOURCES = bbox-do.c\
    config.c\
	init.c \
    login.c\
    mount.c\
    run.c\
    umount.c\
    util.c
noinst_HEADERS = bbox-do.h

# Empty unless configured with --enable-coverage, see configure.ac.
AM_CFLAGS = $(COVERAGE_CFLAGS)
AM_LDFLAGS = $(COVERAGE_LDFLAGS)
