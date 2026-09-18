/*
 * groupdrv.c - call bbox_check_user_in_group_build_box(), optionally
 * with a given set of supplementary groups
 *
 * Copyright (c) 2026 Tobias Koch <tobias.koch@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * "groupdrv" checks with the groups the process has; "groupdrv 4242 7"
 * first sets exactly those as the supplementary groups, which the kernel
 * permits only in a user namespace set up through subordinate ids.
 * Exits 0 when the check passes, 1 when it refuses, 2 on a usage or
 * setgroups error. Driven by test_group_check.sh.
 */

#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "bbox-do.h"

int main(int argc, char *argv[])
{
    if(argc > 1) {
        gid_t gids[64];
        int n = argc - 1;

        if(n > 64) {
            fprintf(stderr, "groupdrv: too many groups\n");
            return 2;
        }
        for(int i = 0; i < n; i++)
            gids[i] = (gid_t) strtoul(argv[i + 1], NULL, 10);

        if(setgroups(n, gids) == -1) {
            perror("setgroups");
            return 2;
        }
    }

    return bbox_check_user_in_group_build_box() == 0 ? 0 : 1;
}
