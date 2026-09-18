/*
 * test_copy_file.c - bbox_copy_file() copies content and permission bits,
 * and nothing else
 *
 * Copyright (c) 2026 Tobias Koch <tobias.koch@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * The copy took its mode from lstat() of the source while reading the
 * content through the symlink, so a copy of a symlinked resolv.conf
 * came out 0777 inside the target.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bbox-do.h"

#include "test.h"

static char dir_template[] = "/tmp/bbox-copy-XXXXXX";
static char *work;

static char *path(const char *rel)
{
    static char buf[512];
    snprintf(buf, sizeof(buf), "%s/%s", work, rel);
    return buf;
}

static void write_file(const char *rel, const char *content, mode_t mode)
{
    FILE *fp = fopen(path(rel), "w");
    if(!fp) {
        perror(path(rel));
        exit(2);
    }
    fputs(content, fp);
    fclose(fp);
    chmod(path(rel), mode);
}

static mode_t mode_of(const char *rel)
{
    struct stat st;
    if(lstat(path(rel), &st) == -1)
        return (mode_t) -1;
    return st.st_mode & 07777;
}

static char *content_of(const char *rel)
{
    static char buf[256];
    FILE *fp = fopen(path(rel), "r");
    if(!fp)
        return NULL;
    if(!fgets(buf, sizeof(buf), fp))
        buf[0] = '\0';
    fclose(fp);
    return buf;
}

int main(void)
{
    char src[512], dst[512];

    work = mkdtemp(dir_template);
    if(!work) {
        perror("mkdtemp");
        return 2;
    }

    /* ── a regular file: content and permission bits ──────────────── */

    write_file("plain", "nameserver 192.0.2.1\n", 0644);
    snprintf(src, sizeof(src), "%s/plain", work);
    snprintf(dst, sizeof(dst), "%s/plain.copy", work);

    test_int_eq(bbox_copy_file(src, dst), 0, "copying a regular file succeeds");
    test_str_eq(content_of("plain.copy"), "nameserver 192.0.2.1\n",
            "the content is copied");
    test_int_eq(mode_of("plain.copy"), 0644, "the mode is copied");

    /* ── a symlink: the target's mode, not the link's ─────────────── */

    if(symlink("plain", path("link")) == -1) {
        perror("symlink");
        return 2;
    }
    snprintf(src, sizeof(src), "%s/link", work);
    snprintf(dst, sizeof(dst), "%s/link.copy", work);

    test_int_eq(bbox_copy_file(src, dst), 0, "copying through a symlink succeeds");
    test_str_eq(content_of("link.copy"), "nameserver 192.0.2.1\n",
            "the content comes from the link target");
    test_int_eq(mode_of("link.copy"), 0644,
            "the mode comes from the link target, not the link");

    /* ── setuid and setgid bits are not carried over ──────────────── */

    write_file("suid", "#!/bin/sh\n", 04755);
    snprintf(src, sizeof(src), "%s/suid", work);
    snprintf(dst, sizeof(dst), "%s/suid.copy", work);

    /* Without this the assertion below could pass for the wrong reason. */
    test_int_eq(mode_of("suid"), 04755, "the source really is setuid");

    test_int_eq(bbox_copy_file(src, dst), 0, "copying a setuid file succeeds");
    test_int_eq(mode_of("suid.copy"), 0755, "the setuid bit is dropped");

    /* ── an existing copy is replaced, a symlink in its place is not ── */

    write_file("plain.copy", "stale\n", 0600);
    snprintf(src, sizeof(src), "%s/plain", work);
    snprintf(dst, sizeof(dst), "%s/plain.copy", work);

    test_int_eq(bbox_copy_file(src, dst), 0, "an existing copy is replaced");
    test_str_eq(content_of("plain.copy"), "nameserver 192.0.2.1\n",
            "with the new content");
    test_int_eq(mode_of("plain.copy"), 0644, "and the source's mode");

    if(symlink("plain", path("elsewhere")) == -1) {
        perror("symlink");
        return 2;
    }
    snprintf(dst, sizeof(dst), "%s/elsewhere", work);

    test_int_eq(bbox_copy_file(src, dst), -1,
            "a symlink in the destination's place is refused");
    test_int_eq(mode_of("plain"), 0644, "and the link target is untouched");

    /* ── cleanup ──────────────────────────────────────────────────── */

    unlink(path("plain"));
    unlink(path("plain.copy"));
    unlink(path("link"));
    unlink(path("link.copy"));
    unlink(path("suid"));
    unlink(path("suid.copy"));
    unlink(path("elsewhere"));
    rmdir(work);

    return test_summary();
}
