/*
 * cmddrv.c - call a command entry point of build-box-do with the given
 * arguments
 *
 * Copyright (c) 2026 Tobias Koch <tobias.koch@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * "cmddrv mount -m dev t" calls bbox_mount() the way main() would after
 * dispatching, with argv[0] the command name. The exit status is what
 * the entry point returns, or, for login and run, the status of the
 * shell they exec. Driven by test_commands.sh inside a user namespace,
 * where the caller is uid 0 and may mount and chroot.
 */

#include <stdio.h>
#include <string.h>

#include "bbox-do.h"

int main(int argc, char *argv[])
{
    int (*entry)(int, char * const[]) = NULL;

    if(argc >= 2) {
        if(strcmp(argv[1], "init") == 0)
            entry = bbox_init;
        else if(strcmp(argv[1], "mount") == 0)
            entry = bbox_mount;
        else if(strcmp(argv[1], "umount") == 0)
            entry = bbox_umount;
        else if(strcmp(argv[1], "login") == 0)
            entry = bbox_login;
        else if(strcmp(argv[1], "run") == 0)
            entry = bbox_run;
    }

    if(!entry) {
        fprintf(stderr, "usage: cmddrv init|mount|umount|login|run [args...]\n");
        return 2;
    }

    return entry(argc - 1, &argv[1]);
}
