/*
 * rundrv.c - harness for test_no_new_privs.sh
 *
 * Copyright (c) 2026 Tobias Koch <tobias.koch@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * usage: rundrv [--isolate] <sysroot> <command>...
 *
 * Calls bbox_runas_user_chrooted() the way `build-box run` does, with a
 * configuration assembled by hand: bbox_config_new() consults the
 * password database for the caller's home, and inside the user
 * namespace the test runs in, uid 0's home belongs to somebody with no
 * mapping. Exits with the command's status.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bbox-do.h"

int main(int argc, char *argv[])
{
    bbox_conf_t conf;
    int argi = 1;

    memset(&conf, 0, sizeof(conf));

    if(argi < argc && strcmp(argv[argi], "--isolate") == 0) {
        bbox_config_set_isolation(&conf);
        bbox_config_set_mount_proc(&conf);
        argi++;
    }

    if(argc - argi < 2) {
        fprintf(stderr, "usage: rundrv [--isolate] <sysroot> <command>...\n");
        return 2;
    }

    conf.chroot_home_dir = "/";

    return bbox_runas_user_chrooted(argv[argi], argc - argi - 1,
            &argv[argi + 1], &conf);
}
