/*
 * test_getopt.c - the option handlers of the five commands
 *
 * Copyright (c) 2026 Tobias Koch <tobias.koch@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Each handler returns the index of the first positional argument, -1
 * when it printed help or usage and wants a clean exit, and -2 for an
 * invocation error. What matters is which flags end up set: a missing
 * -m means everything, a given -m means exactly that, and for umount
 * the meaning is inverted, since there a set bit is something to keep.
 *
 * Where an option may stand differs by command, see the macros in
 * bbox-do.h: the four leaves permute, so an option after the target is
 * still an option; run is a dispatcher and stops at the target, so
 * everything after it is the command and none of it is read. Both rules
 * have to hold with several parsers run in one process, which is what
 * this test does, and whatever POSIXLY_CORRECT says.
 *
 * The handlers are module-internal and not in bbox-do.h, hence the
 * prototypes below. The usage text goes to stdout, which is where the
 * TAP lines go too, so it is silenced around the calls that print it.
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bbox-do.h"

#include "test.h"

int bbox_mount_getopt(bbox_conf_t *conf, int argc, char * const argv[]);
int bbox_login_getopt(bbox_conf_t *conf, int argc, char * const argv[]);
int bbox_run_getopt(bbox_conf_t *conf, int argc, char * const argv[]);
int bbox_umount_getopt(bbox_conf_t *conf, int argc, char * const argv[]);
int bbox_init_getopt(bbox_conf_t *conf, int argc, char * const argv[]);

typedef int (*getopt_fn)(bbox_conf_t *, int, char * const[]);

#define ARGC(a) ((int) (sizeof(a) / sizeof((a)[0])) - 1)

static bbox_conf_t conf;

/* Run a handler on a fresh context with stdout pointed at /dev/null. */
static int run(getopt_fn fn, int argc, char * const argv[])
{
    int saved, devnull, rval;

    free(conf.target_dir);
    memset(&conf, 0, sizeof(conf));

    fflush(stdout);
    saved = dup(STDOUT_FILENO);
    devnull = open("/dev/null", O_WRONLY);
    dup2(devnull, STDOUT_FILENO);

    rval = fn(&conf, argc, argv);

    fflush(stdout);
    dup2(saved, STDOUT_FILENO);
    close(saved);
    close(devnull);

    return rval;
}

static unsigned int mounts(void)
{
    return bbox_config_get_mount_any(&conf);
}

int main(void)
{
    /* ── mount ────────────────────────────────────────────────────── */

    {
        char *argv[] = {"mount", "t", NULL};
        test_int_eq(run(bbox_mount_getopt, ARGC(argv), argv), 1,
                "mount: the target is the first positional");
        test_int_eq(mounts(), BBOX_DO_MOUNT_ALL, "mount: no -m means all");
    }
    {
        char *argv[] = {"mount", "-m", "dev", "-m", "home", "t", NULL};
        test_int_eq(run(bbox_mount_getopt, ARGC(argv), argv), 5,
                "mount: -m can be repeated");
        test_int_eq(mounts(), BBOX_DO_MOUNT_DEV | BBOX_DO_MOUNT_HOME,
                "mount: and means exactly what was named");
    }
    {
        char *argv[] = {"mount", "--mount=proc", "t", NULL};
        run(bbox_mount_getopt, ARGC(argv), argv);
        test_int_eq(mounts(), BBOX_DO_MOUNT_PROC, "mount: --mount=proc is proc alone");
    }
    {
        char *argv[] = {"mount", "-m", "sys", "t", NULL};
        run(bbox_mount_getopt, ARGC(argv), argv);
        test_int_eq(mounts(), BBOX_DO_MOUNT_SYS, "mount: -m sys is sys alone");
    }
    {
        char *argv[] = {"mount", "-m", "bogus", "t", NULL};
        test_int_eq(run(bbox_mount_getopt, ARGC(argv), argv), -2,
                "mount: an unknown file system is an invocation error");
    }
    {
        char *argv[] = {"mount", "-m", NULL};
        test_int_eq(run(bbox_mount_getopt, ARGC(argv), argv), -2,
                "mount: -m without an argument is an invocation error");
    }
    {
        char *argv[] = {"mount", "--bogus", "t", NULL};
        test_int_eq(run(bbox_mount_getopt, ARGC(argv), argv), -2,
                "mount: an unknown option is an invocation error");
    }
    {
        char *argv[] = {"mount", "-h", NULL};
        test_int_eq(run(bbox_mount_getopt, ARGC(argv), argv), -1,
                "mount: -h asks for a clean exit");
    }
    {
        char *argv[] = {"mount", "--help", NULL};
        test_int_eq(run(bbox_mount_getopt, ARGC(argv), argv), -1,
                "mount: so does --help");
    }
    {
        char *argv[] = {"mount", "-t", "/x", "t", NULL};
        test_int_eq(run(bbox_mount_getopt, ARGC(argv), argv), 3,
                "mount: -t takes a directory");
        test_str_eq(bbox_config_get_target_dir(&conf), "/x",
                "mount: and stores it as the target dir");
    }
    {
        char *argv[] = {"mount", "t", "extra", NULL};
        test_int_eq(run(bbox_mount_getopt, ARGC(argv), argv), -1,
                "mount: a second positional is usage");
    }

    /* ── login ────────────────────────────────────────────────────── */

    {
        char *argv[] = {"login", "t", NULL};
        test_int_eq(run(bbox_login_getopt, ARGC(argv), argv), 1,
                "login: the target is the first positional");
        test_int_eq(mounts(), BBOX_DO_MOUNT_ALL, "login: mounts everything by default");
        test_ok(bbox_config_do_file_updates(&conf) != 0,
                "login: and copies the host files by default");
    }
    {
        char *argv[] = {"login", "--no-mount", "t", NULL};
        run(bbox_login_getopt, ARGC(argv), argv);
        test_int_eq(mounts(), 0, "login: --no-mount mounts nothing");
    }
    {
        char *argv[] = {"login", "--no-mount", "-m", "home", "t", NULL};
        run(bbox_login_getopt, ARGC(argv), argv);
        test_int_eq(mounts(), BBOX_DO_MOUNT_HOME,
                "login: --no-mount with -m still mounts what was named");
    }
    {
        char *argv[] = {"login", "--no-file-copy", "t", NULL};
        run(bbox_login_getopt, ARGC(argv), argv);
        test_int_eq(bbox_config_do_file_updates(&conf), 0,
                "login: --no-file-copy disables the copy");
        test_int_eq(mounts(), BBOX_DO_MOUNT_ALL, "login: without touching the mounts");
    }
    {
        char *argv[] = {"login", "-t", "/x", "t", "extra", NULL};
        test_int_eq(run(bbox_login_getopt, ARGC(argv), argv), -1,
                "login: a second positional is usage");
    }
    {
        char *argv[] = {"login", "--isolate", "t", NULL};
        test_int_eq(run(bbox_login_getopt, ARGC(argv), argv), -2,
                "login: --isolate is not a login option");
    }

    /* ── leaves permute: an option after the target is still an option ── */

    {
        char *argv[] = {"mount", "t", "-m", "dev", NULL};
        int i = run(bbox_mount_getopt, ARGC(argv), argv);
        test_ok(i > 0 && i < ARGC(argv) && strcmp(argv[i], "t") == 0,
                "mount: -m after the target is read, the target is found");
        test_int_eq(mounts(), BBOX_DO_MOUNT_DEV, "mount: and takes effect");
    }
    {
        char *argv[] = {"login", "t", "--no-mount", NULL};
        run(bbox_login_getopt, ARGC(argv), argv);
        test_int_eq(mounts(), 0, "login: --no-mount after the target takes effect");
    }
    {
        char *argv[] = {"umount", "t", "--bogus", NULL};
        test_int_eq(run(bbox_umount_getopt, ARGC(argv), argv), -2,
                "umount: an unknown option after the target is still an error");
    }
    {
        setenv("POSIXLY_CORRECT", "1", 1);
        char *argv[] = {"mount", "t", "-m", "sys", NULL};
        run(bbox_mount_getopt, ARGC(argv), argv);
        test_int_eq(mounts(), BBOX_DO_MOUNT_SYS,
                "a leaf permutes whatever POSIXLY_CORRECT says");
        test_ok(getenv("POSIXLY_CORRECT") == NULL,
                "and the variable is gone from the environment");
    }

    /* ── run ──────────────────────────────────────────────────────── */

    {
        char *argv[] = {"run", "t", "make", "all", NULL};
        test_int_eq(run(bbox_run_getopt, ARGC(argv), argv), 1,
                "run: the command follows the target");
        test_int_eq(bbox_config_get_isolation(&conf), 0, "run: not isolated by default");
        test_int_eq(mounts(), BBOX_DO_MOUNT_ALL, "run: mounts everything by default");
    }
    {
        /* The documented form: options, target, "--", command. */
        char *argv[] = {"run", "--isolate", "t", "--", "make", NULL};
        int i = run(bbox_run_getopt, ARGC(argv), argv);
        test_int_eq(i, 2, "run: the returned index is the target's");
        test_str_eq(argv[i + 1], "--", "run: the -- is not consumed by getopt");
        test_str_eq(argv[bbox_run_command_index(ARGC(argv), argv, i + 1)], "make",
                "run: and the command starts after it");
        test_ok(bbox_config_get_isolation(&conf) != 0, "run: --isolate is noted");
    }
    {
        /* The command's own options are not build-box's to read. */
        char *argv[] = {"run", "t", "ls", "-l", NULL};
        int i = run(bbox_run_getopt, ARGC(argv), argv);
        test_int_eq(i, 1, "run: the parse stops at the target");
        test_ok(strcmp(argv[2], "ls") == 0 && strcmp(argv[3], "-l") == 0,
                "run: and the command's -l is left where it was");
    }
    {
        char *argv[] = {"run", "-m", "proc", "t", "make", "-j4", "--isolate", NULL};
        int i = run(bbox_run_getopt, ARGC(argv), argv);
        test_int_eq(i, 3, "run: options before the target are read");
        test_int_eq(bbox_config_get_isolation(&conf), 0,
                "run: an --isolate after the target is the command's, not ours");
        test_int_eq(mounts(), BBOX_DO_MOUNT_PROC, "run: the -m before it was");
    }
    {
        setenv("POSIXLY_CORRECT", "1", 1);
        char *argv[] = {"run", "--isolate", "t", "sh", NULL};
        test_int_eq(run(bbox_run_getopt, ARGC(argv), argv), 2,
                "run: stops at the target whatever POSIXLY_CORRECT says");
        test_ok(bbox_config_get_isolation(&conf) != 0, "run: with --isolate read");
    }
    {
        char *argv[] = {"run", "--no-mount", "--no-file-copy", "t", "sh", NULL};
        run(bbox_run_getopt, ARGC(argv), argv);
        test_int_eq(mounts(), 0, "run: --no-mount mounts nothing");
        test_int_eq(bbox_config_do_file_updates(&conf), 0,
                "run: --no-file-copy disables the copy");
    }
    {
        char *argv[] = {"run", "-m", "proc", "t", "sh", NULL};
        run(bbox_run_getopt, ARGC(argv), argv);
        test_int_eq(mounts(), BBOX_DO_MOUNT_PROC, "run: -m proc is proc alone");
    }
    {
        char *argv[] = {"run", "-m", "bogus", "t", "sh", NULL};
        test_int_eq(run(bbox_run_getopt, ARGC(argv), argv), -2,
                "run: an unknown file system is an invocation error");
    }
    {
        char *argv[] = {"run", "--help", NULL};
        test_int_eq(run(bbox_run_getopt, ARGC(argv), argv), -1,
                "run: --help asks for a clean exit");
    }

    /* ── run: one "--" after the target is build-box's ────────────── */

    {
        char *argv[] = {"run", "t", "--", "ls", "-l", NULL};
        test_int_eq(bbox_run_command_index(ARGC(argv), argv, 2), 3,
                "run: a -- right after the target is skipped");
    }
    {
        char *argv[] = {"run", "t", "ls", "-l", NULL};
        test_int_eq(bbox_run_command_index(ARGC(argv), argv, 2), 2,
                "run: without one the command starts at the target's successor");
    }
    {
        char *argv[] = {"run", "t", "--", "--", "x", NULL};
        test_int_eq(bbox_run_command_index(ARGC(argv), argv, 2), 3,
                "run: a second -- belongs to the command");
    }
    {
        char *argv[] = {"run", "t", NULL};
        test_int_eq(bbox_run_command_index(ARGC(argv), argv, 2), 2,
                "run: nothing after the target is left alone");
    }

    /* ── umount: a set bit is something to keep ───────────────────── */

    {
        char *argv[] = {"umount", "t", NULL};
        test_int_eq(run(bbox_umount_getopt, ARGC(argv), argv), 1,
                "umount: the target is the first positional");
        test_int_eq(mounts(), 0, "umount: no -m keeps nothing, everything comes down");
    }
    {
        char *argv[] = {"umount", "-m", "dev", "t", NULL};
        run(bbox_umount_getopt, ARGC(argv), argv);
        test_int_eq(mounts(), BBOX_DO_MOUNT_PROC | BBOX_DO_MOUNT_SYS | BBOX_DO_MOUNT_HOME,
                "umount: -m dev takes down dev and keeps the rest");
    }
    {
        char *argv[] = {"umount", "--umount=home", "-m", "sys", "t", NULL};
        run(bbox_umount_getopt, ARGC(argv), argv);
        test_int_eq(mounts(), BBOX_DO_MOUNT_DEV | BBOX_DO_MOUNT_PROC,
                "umount: naming home and sys keeps dev and proc");
    }
    {
        char *argv[] = {"umount", "-m", "proc", "t", NULL};
        run(bbox_umount_getopt, ARGC(argv), argv);
        test_int_eq(mounts() & BBOX_DO_MOUNT_PROC, 0, "umount: -m proc takes down proc");
    }
    {
        char *argv[] = {"umount", "-m", "bogus", "t", NULL};
        test_int_eq(run(bbox_umount_getopt, ARGC(argv), argv), -2,
                "umount: an unknown file system is an invocation error");
    }
    {
        char *argv[] = {"umount", "-t", "/x", "t", NULL};
        run(bbox_umount_getopt, ARGC(argv), argv);
        test_str_eq(bbox_config_get_target_dir(&conf), "/x", "umount: -t is stored");
    }
    {
        char *argv[] = {"umount", "t", "extra", NULL};
        test_int_eq(run(bbox_umount_getopt, ARGC(argv), argv), -1,
                "umount: a second positional is usage");
    }
    {
        char *argv[] = {"umount", "-h", NULL};
        test_int_eq(run(bbox_umount_getopt, ARGC(argv), argv), -1,
                "umount: -h asks for a clean exit");
    }

    /* ── init ─────────────────────────────────────────────────────── */

    {
        char *argv[] = {"init", NULL};
        test_int_eq(run(bbox_init_getopt, ARGC(argv), argv), 1,
                "init: takes no arguments");
    }
    {
        char *argv[] = {"init", "-h", NULL};
        test_int_eq(run(bbox_init_getopt, ARGC(argv), argv), -1,
                "init: -h asks for a clean exit");
    }
    {
        char *argv[] = {"init", "-x", NULL};
        test_int_eq(run(bbox_init_getopt, ARGC(argv), argv), -2,
                "init: an unknown option is an invocation error");
    }

    free(conf.target_dir);
    return test_summary();
}
