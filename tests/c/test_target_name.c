/*
 * test_target_name.c - validate_target_name() and bbox_get_user_dir()
 *
 * Copyright (c) 2026 Tobias Koch <tobias.koch@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * A target name becomes one path component below the per-user target
 * directory, and everything the setuid binary does starts from that
 * path. The alphabet is deliberately small: letters, digits, dash,
 * underscore and dot, and never "." or ".." on their own.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "bbox-do.h"

#include "test.h"

int main(void)
{
    /* ── names that are fine ──────────────────────────────────────── */

    static const char *good[] = {
        "s390x", "a.b-c_D9", "...", "-x", "_", "0", "x86_64-glibc", NULL
    };
    for(size_t i = 0; good[i] != NULL; i++) {
        char label[128];
        snprintf(label, sizeof(label), "'%s' is a valid target name", good[i]);
        test_int_eq(validate_target_name("test", good[i]), 0, label);
    }

    /* ── names that are not ───────────────────────────────────────── */

    static const char *bad[] = {
        "", ".", "..", "a/b", "/a", "a b", "a\tb", "a\nb", "\xc3\xa4",
        "a:b", "a,b", "a*", NULL
    };
    for(size_t i = 0; bad[i] != NULL; i++) {
        char label[128];
        snprintf(label, sizeof(label), "'%s' is refused", bad[i]);
        test_int_eq(validate_target_name("test", bad[i]), -1, label);
    }

    /* ── the per-user directory ───────────────────────────────────── */

    {
        char want[256];
        size_t n = 0;
        char *dir = bbox_get_user_dir(1234, &n);

        snprintf(want, sizeof(want), BBOX_VAR_LIB "/users/%lu", 1234UL);
        test_str_eq(dir, want, "the user directory is keyed by uid");
        test_ok(dir && n == strlen(dir) + 1, "and the buffer size is reported");
        free(dir);

        dir = bbox_get_user_dir(getuid(), NULL);
        snprintf(want, sizeof(want), BBOX_VAR_LIB "/users/%lu",
                (unsigned long) getuid());
        test_str_eq(dir, want, "the size pointer may be NULL");
        free(dir);
    }

    return test_summary();
}
