/*
 * test_mkdir_p.c - bbox_mkdir_p() creates a path without spawning mkdir(1)
 *
 * Copyright (c) 2026 Tobias Koch <tobias.koch@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * It used to run "mkdir -p" from the caller's PATH. With PATH pointing
 * nowhere useful, or inside a chroot with no mkdir in it, that version
 * fails every case below.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bbox-do.h"

#include "test.h"

static char dir_template[] = "/tmp/bbox-mkdir-XXXXXX";
static char *work;

static char *path(const char *rel)
{
    static char buf[512];
    snprintf(buf, sizeof(buf), "%s/%s", work, rel);
    return buf;
}

static int is_dir(const char *rel)
{
    struct stat st;
    return stat(path(rel), &st) == 0 && S_ISDIR(st.st_mode);
}

static mode_t mode_of(const char *rel)
{
    struct stat st;
    if(stat(path(rel), &st) == -1)
        return (mode_t) -1;
    return st.st_mode & 0777;
}

int main(void)
{
    work = mkdtemp(dir_template);
    if(!work) {
        perror("mkdtemp");
        return 2;
    }

    /*
     * Not unsetenv(): execvp() falls back to /bin:/usr/bin when PATH is
     * unset, which would let the old implementation pass.
     */
    setenv("PATH", "/nonexistent", 1);
    umask(022);

    /* ── nested components ────────────────────────────────────────── */

    test_int_eq(bbox_mkdir_p("test", path("a/b/c")), 0, "a nested path is created");
    test_ok(is_dir("a") && is_dir("a/b") && is_dir("a/b/c"), "with every component");
    test_int_eq(mode_of("a/b/c"), 0755, "honouring the umask");

    /* ── existing directories are fine, files are not ─────────────── */

    test_int_eq(bbox_mkdir_p("test", path("a/b/c")), 0, "an existing path is fine");
    test_int_eq(bbox_mkdir_p("test", path("a/b/c/")), 0, "with a trailing slash too");
    test_int_eq(bbox_mkdir_p("test", path("a//b///d")), 0, "and with doubled slashes");
    test_ok(is_dir("a/b/d"), "which still creates the missing tail");

    {
        FILE *fp = fopen(path("a/file"), "w");
        if(!fp) {
            perror("fopen");
            return 2;
        }
        fclose(fp);
    }

    test_int_eq(bbox_mkdir_p("test", path("a/file")), -1,
            "a file in the way is an error");
    test_int_eq(bbox_mkdir_p("test", path("a/file/x")), -1,
            "as is a file in the middle of the path");

    /* ── an empty path is an error, not a read past the buffer ────── */
    /*
     * The loop over the components started at the second byte, which for
     * "" is past the terminator. Reachable only through a symlink with an
     * empty target, which Linux refuses to create, but a sanitizer build
     * flags it.
     */

    test_int_eq(bbox_mkdir_p("test", ""), -1, "an empty path is an error");

    /* ── a symlink to a directory is followed, like mkdir -p does ──── */

    if(symlink("a", path("link")) == -1) {
        perror("symlink");
        return 2;
    }

    test_int_eq(bbox_mkdir_p("test", path("link/e")), 0,
            "a symlinked component is followed");
    test_ok(is_dir("a/e"), "and the directory lands behind the link");

    /* ── the sysroot-relative variant joins first ─────────────────── */

    test_int_eq(bbox_sysroot_mkdir_p("test", work, "/home/me/RealHome"), 0,
            "bbox_sysroot_mkdir_p joins the sysroot and the path");
    test_ok(is_dir("home/me/RealHome"), "and creates it below the sysroot");

    /* ── cleanup ──────────────────────────────────────────────────── */

    unlink(path("link"));
    unlink(path("a/file"));
    rmdir(path("a/e"));
    rmdir(path("a/b/d"));
    rmdir(path("a/b/c"));
    rmdir(path("a/b"));
    rmdir(path("a"));
    rmdir(path("home/me/RealHome"));
    rmdir(path("home/me"));
    rmdir(path("home"));
    rmdir(work);

    return test_summary();
}
