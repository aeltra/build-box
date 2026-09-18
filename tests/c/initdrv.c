/*
 * initdrv.c - harness for test_init_user_dir.sh
 *
 * Copyright (c) 2026 Tobias Koch <tobias.koch@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * usage: initdrv
 *
 * Calls bbox_init_user_directory(), which creates the caller's directory
 * under /var/lib/build-box/users. The path is compiled in, so the test
 * mounts a tmpfs over /var/lib inside its namespace first.
 */

#include "bbox-do.h"

int main(void)
{
    return bbox_init_user_directory() == 0 ? 0 : 1;
}
