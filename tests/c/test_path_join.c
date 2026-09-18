/*
 * test_path_join.c - bbox_path_join() on its edges
 *
 * Copyright (c) 2026 Tobias Koch <tobias.koch@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <string.h>

#include "bbox-do.h"

#include "test.h"

static void check(const char *base, const char *sub, const char *want)
{
    char *buf = NULL;
    size_t buf_len = 0;
    char label[256];

    bbox_path_join(&buf, base, sub, &buf_len);

    snprintf(label, sizeof(label), "join(\"%s\", \"%s\")", base, sub);
    test_str_eq(buf, want, label);

    free(buf);
}

int main(void)
{
    /* ── the ordinary case ─────────────────────────────────────────── */

    check("/root", "dev", "/root/dev");
    check("/home", "tobias", "/home/tobias");

    /* ── separators are normalized, never doubled ──────────────────── */

    check("/root", "/dev", "/root/dev");
    check("/root/", "/dev", "/root/dev");
    check("/root/", "dev", "/root/dev");
    check("/root", "//etc/x", "/root/etc/x");

    /* ── an empty base still yields an absolute path ───────────────── */

    check("", "dev", "/dev");
    check("/", "dev", "/dev");

    /*
     * An empty or slash-only sub must not leave a trailing separator:
     * umount.c joins the sysroot with an empty relative path to address
     * the sysroot itself.
     */

    check("/root", "", "/root");
    check("/root", "/", "/root");
    check("/root/", "", "/root/");

    /* ── growing the output buffer in place, as config.c does ──────── */

    {
        char *buf = NULL;
        size_t buf_len = 0;

        bbox_path_join(&buf, "/var/lib/build-box/users/1000", "", &buf_len);
        bbox_path_join(&buf, buf, "targets", &buf_len);
        test_str_eq(buf, "/var/lib/build-box/users/1000/targets",
                "join into the buffer that holds the base");

        bbox_path_join(&buf, buf, "a-much-longer-component-forcing-realloc",
                &buf_len);
        test_str_eq(buf,
                "/var/lib/build-box/users/1000/targets/"
                "a-much-longer-component-forcing-realloc",
                "join into the buffer across a reallocation");

        free(buf);
    }

    return test_summary();
}
