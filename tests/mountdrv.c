/*
 * mountdrv.c - harness for test_mount.sh
 *
 * Copyright (c) 2026 Tobias Koch <tobias.koch@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * usage: mountdrv bind <sysroot> <source> <parent-relpath> <name> [flag...]
 *        mountdrv special <sysroot> <fstype> <parent-relpath> <name>
 *        mountdrv home <sysroot> <homedir> <chroot-home>
 *        mountdrv race <count> <command...>
 *
 * Calls bbox_mount_bind() or bbox_mount_special() and exits 0 on
 * success, 1 on failure. Flags are "nosuid", "nodev" and "noexec" and
 * are applied with the remount that bbox_mount_bind() does. "home" goes
 * through bbox_mount_any() with only the home mount enabled, the way
 * `build-box mount -m home` does, with a configuration assembled by hand
 * (see rundrv.c for why). The privilege dance inside is a no-op for
 * uid 0, which is what the caller is inside the user namespace the test
 * sets up.
 *
 * "race" runs one of the commands above from <count> processes at once.
 * The children are forked first and released together, so that all of
 * them are past the "already mounted" check before the first mount is
 * made -- the situation two build-box invocations only reach by chance.
 * Exits 0 if every child reported success.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <unistd.h>

#include "bbox-do.h"

static void usage(void)
{
    fprintf(stderr,
        "usage: mountdrv bind <sysroot> <source> <parent-relpath> <name> "
        "[nosuid|nodev|noexec]...\n"
        "       mountdrv special <sysroot> <fstype> <parent-relpath> <name>\n"
        "       mountdrv home <sysroot> <homedir> <chroot-home>\n"
        "       mountdrv race <count> <command...>\n"
    );
}

static int run(int argc, char *argv[]);

static int race(int argc, char *argv[])
{
    int count = 0;
    int barrier[2];
    int failed = 0;
    char byte;

    if(argc < 3 || (count = atoi(argv[2])) < 1) {
        usage();
        return 2;
    }

    /*
     * Every child blocks reading the pipe. Nobody writes to it, so they
     * all wake up together when the parent closes the write end.
     */
    if(pipe(barrier) == -1) {
        perror("pipe");
        return 1;
    }

    for(int i = 0; i < count; i++) {
        pid_t pid = fork();

        if(pid == -1) {
            perror("fork");
            return 1;
        }

        if(pid == 0) {
            close(barrier[1]);
            (void) read(barrier[0], &byte, 1);
            close(barrier[0]);
            _exit(run(argc - 2, argv + 2));
        }
    }

    close(barrier[1]);
    close(barrier[0]);

    for(int i = 0; i < count; i++) {
        int wstatus = 0;

        if(wait(&wstatus) == -1) {
            perror("wait");
            return 1;
        }
        if(!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0)
            failed = 1;
    }

    return failed;
}

int main(int argc, char *argv[])
{
    if(argc >= 2 && strcmp(argv[1], "race") == 0)
        return race(argc, argv);

    return run(argc, argv);
}

static int run(int argc, char *argv[])
{
    if(argc == 5 && strcmp(argv[1], "home") == 0) {
        bbox_conf_t conf;

        memset(&conf, 0, sizeof(conf));
        conf.home_dir = argv[3];
        conf.chroot_home_dir = argv[4];
        bbox_config_set_mount_home(&conf);

        return bbox_mount_any(&conf, argv[2]) == 0 ? 0 : 1;
    }

    if(argc < 6) {
        usage();
        return 2;
    }

    if(strcmp(argv[1], "bind") == 0) {
        unsigned long flags = 0;

        for(int i = 6; i < argc; i++) {
            if(strcmp(argv[i], "nosuid") == 0)
                flags |= MS_NOSUID;
            else if(strcmp(argv[i], "nodev") == 0)
                flags |= MS_NODEV;
            else if(strcmp(argv[i], "noexec") == 0)
                flags |= MS_NOEXEC;
            else {
                usage();
                return 2;
            }
        }

        return bbox_mount_bind(argv[2], argv[3], argv[4], argv[5], 0,
                flags) == 0 ? 0 : 1;
    }

    if(strcmp(argv[1], "special") == 0 && argc == 6)
        return bbox_mount_special(argv[2], argv[4], argv[5], argv[3]) == 0
            ? 0 : 1;

    usage();
    return 2;
}
