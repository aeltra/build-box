/*
 * test_config.c - the configuration context and its flag accessors
 *
 * Copyright (c) 2026 Tobias Koch <tobias.koch@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * bbox_config_new() consults the password database for the invoking
 * user, so the test asserts against what getpwuid() says rather than
 * against fixed strings. The flag accessors are plain bit operations,
 * checked for independence from one another: setting isolation must not
 * touch the mount bits, clearing the mount bits must not touch file
 * updates, and so on.
 */

#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "bbox-do.h"

#include "test.h"

int main(void)
{
    struct passwd *pwd = getpwuid(getuid());
    char want[512];

    if(!pwd || !pwd->pw_dir) {
        printf("1..0 # SKIP no password entry for uid %ld\n", (long) getuid());
        return 77;
    }

    /* ── the context is built from the password database ─────────── */

    bbox_conf_t *conf = bbox_config_new();

    if(!conf) {
        printf("1..0 # SKIP bbox_config_new() failed, "
            "is the home directory owned by the caller?\n");
        return 77;
    }

    test_str_eq(bbox_config_get_user_name(conf), pwd->pw_name,
            "the user name is the caller's");

    {
        char *home = realpath(pwd->pw_dir, NULL);
        test_str_eq(bbox_config_get_home_dir(conf), home,
                "the home directory is the normalized pw_dir");
        free(home);
    }

    snprintf(want, sizeof(want), "/home/%s", pwd->pw_name);
    test_str_eq(bbox_config_get_chroot_home_dir(conf), want,
            "the in-chroot home is /home/<user> wherever the real one is");

    snprintf(want, sizeof(want), BBOX_VAR_LIB "/users/%lu/targets",
            (unsigned long) getuid());
    test_str_eq(bbox_config_get_target_dir(conf), want,
            "the target directory is the per-user one");

    test_int_eq(bbox_config_get_mount_any(conf), 0, "nothing is mounted by default");
    test_int_eq(bbox_config_do_file_updates(conf), 0, "no file updates by default");
    test_int_eq(bbox_config_get_isolation(conf), 0, "no isolation by default");

    /* ── overriding the directories ───────────────────────────────── */

    test_int_eq(bbox_config_set_target_dir(conf, "/x"), 0, "the target dir can be set");
    test_str_eq(bbox_config_get_target_dir(conf), "/x", "and is returned as given");
    test_int_eq(bbox_config_set_home_dir(conf, "/y"), 0, "the home dir can be set");
    test_str_eq(bbox_config_get_home_dir(conf), "/y", "and is returned as given");

    /* ── mount bits ───────────────────────────────────────────────── */

    bbox_config_set_mount_all(conf);
    test_ok(bbox_config_get_mount_dev(conf) && bbox_config_get_mount_proc(conf)
            && bbox_config_get_mount_sys(conf) && bbox_config_get_mount_home(conf),
            "set_mount_all sets all four");
    test_ok(bbox_config_get_mount_any(conf) != 0, "and any reports it");

    bbox_config_unset_mount_dev(conf);
    test_int_eq(bbox_config_get_mount_dev(conf), 0, "unset_mount_dev clears dev");
    test_ok(bbox_config_get_mount_proc(conf) != 0, "and leaves proc alone");

    bbox_config_unset_mount_proc(conf);
    bbox_config_unset_mount_sys(conf);
    bbox_config_unset_mount_home(conf);
    test_int_eq(bbox_config_get_mount_any(conf), 0, "unsetting each clears any");

    bbox_config_set_mount_home(conf);
    test_ok(bbox_config_get_mount_home(conf) && !bbox_config_get_mount_dev(conf)
            && !bbox_config_get_mount_proc(conf) && !bbox_config_get_mount_sys(conf),
            "set_mount_home sets home alone");

    bbox_config_set_mount_dev(conf);
    bbox_config_set_mount_proc(conf);
    bbox_config_set_mount_sys(conf);
    test_ok(bbox_config_get_mount_any(conf) == BBOX_DO_MOUNT_ALL,
            "the individual setters add up to all");

    bbox_config_clear_mount(conf);
    test_int_eq(bbox_config_get_mount_any(conf), 0, "clear_mount clears them all");

    /* ── the other bits are independent of the mount bits ─────────── */

    bbox_config_enable_file_updates(conf);
    bbox_config_set_isolation(conf);
    test_ok(bbox_config_do_file_updates(conf) != 0, "file updates can be enabled");
    test_ok(bbox_config_get_isolation(conf) != 0, "isolation can be enabled");
    test_int_eq(bbox_config_get_mount_any(conf), 0, "without touching the mount bits");

    bbox_config_set_mount_all(conf);
    bbox_config_clear_mount(conf);
    test_ok(bbox_config_do_file_updates(conf) && bbox_config_get_isolation(conf),
            "and the mount bits do not touch them");

    bbox_config_disable_file_updates(conf);
    bbox_config_unset_isolation(conf);
    test_int_eq(bbox_config_do_file_updates(conf), 0, "file updates can be disabled");
    test_int_eq(bbox_config_get_isolation(conf), 0, "isolation can be disabled");

    /* ── freeing ──────────────────────────────────────────────────── */

    bbox_config_free(conf);
    bbox_config_free(NULL);
    test_ok(1, "freeing the context and NULL is fine");

    return test_summary();
}
