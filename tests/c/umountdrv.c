/*
 * umountdrv.c - harness for test_umount_unbind.sh
 *
 * Copyright (c) 2026 Tobias Koch <tobias.koch@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * usage: umountdrv <sysroot> <parent-relpath> <name>
 *
 * Calls bbox_umount_unbind() and exits 0 on success, 1 on failure.
 * The privilege dance inside is a no-op for uid 0, which is what the
 * caller is inside the user namespace the test sets up.
 */

#include <stdio.h>

#include "bbox-do.h"

int main(int argc, char *argv[])
{
    if(argc != 4) {
        fprintf(stderr, "usage: umountdrv <sysroot> <parent-relpath> <name>\n");
        return 2;
    }

    return bbox_umount_unbind(argv[1], argv[2], argv[3]) == 0 ? 0 : 1;
}
