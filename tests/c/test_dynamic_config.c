/*
 * test_dynamic_config.c - bbox_update_chroot_dynamic_config() copies the
 * host's passwd, group, resolv.conf and hosts into a sysroot
 *
 * Copyright (c) 2026 Tobias Koch <tobias.koch@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Runs with the caller's own privileges into a temporary sysroot. The
 * databases are read through getpwent() and getgrent(), so the test
 * asserts against those rather than against /etc/passwd verbatim. The
 * invoking user's home directory is rewritten to the in-chroot one;
 * everybody else's line is passed through.
 */

#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>

#include "bbox-do.h"

#include "test.h"

static char dir_template[] = "/tmp/bbox-dynconf-XXXXXX";
static char *work;

static char *path(const char *rel)
{
    static char buf[512];
    snprintf(buf, sizeof(buf), "%s/%s", work, rel);
    return buf;
}

static char *slurp(const char *file, size_t *len)
{
    FILE *fp = fopen(file, "r");
    char *buf = NULL;
    size_t n = 0, cap = 0;

    if(!fp)
        return NULL;

    while(1) {
        if(n == cap) {
            cap = cap ? cap * 2 : 4096;
            buf = realloc(buf, cap + 1);
        }
        size_t got = fread(buf + n, 1, cap - n, fp);
        n += got;
        if(got == 0)
            break;
    }

    fclose(fp);
    buf[n] = '\0';
    if(len)
        *len = n;
    return buf;
}

static int count_lines(const char *buf)
{
    int n = 0;
    for(; *buf; buf++)
        if(*buf == '\n')
            n++;
    return n;
}

/* The line for a user, or NULL. Fields are colon-separated. */
static char *line_for(const char *buf, const char *name)
{
    size_t len = strlen(name);
    const char *p = buf;

    while(p && *p) {
        if(strncmp(p, name, len) == 0 && p[len] == ':') {
            const char *end = strchr(p, '\n');
            return strndup(p, end ? (size_t) (end - p) : strlen(p));
        }
        p = strchr(p, '\n');
        if(p)
            p++;
    }
    return NULL;
}

/* Field i (0-based) of a colon-separated line, empty fields included. */
static char *field(const char *line, int i)
{
    const char *p = line;
    for(; i > 0; i--) {
        p = strchr(p, ':');
        if(!p)
            return NULL;
        p++;
    }
    const char *end = strchr(p, ':');
    return strndup(p, end ? (size_t) (end - p) : strlen(p));
}

static int count_pw(void)
{
    int n = 0;
    setpwent();
    while(getpwent())
        n++;
    endpwent();
    return n;
}

static int count_gr(void)
{
    int n = 0;
    setgrent();
    while(getgrent())
        n++;
    endgrent();
    return n;
}

static int leftovers(void)
{
    DIR *d = opendir(path("etc"));
    struct dirent *e;
    int n = 0;

    if(!d)
        return -1;
    while((e = readdir(d)) != NULL) {
        if(strncmp(e->d_name, "passwd-", 7) == 0 ||
                strncmp(e->d_name, "group-", 6) == 0 ||
                strstr(e->d_name, "-XXXXXX") || strstr(e->d_name, ".conf-"))
            n++;
    }
    closedir(d);
    return n;
}

int main(void)
{
    struct passwd *me = getpwuid(getuid());
    struct stat st;
    bbox_conf_t conf;

    if(!me) {
        printf("1..0 # SKIP no password entry for uid %ld\n", (long) getuid());
        return 77;
    }

    work = mkdtemp(dir_template);
    if(!work || mkdir(path("etc"), 0755) == -1) {
        perror("mkdtemp");
        return 2;
    }

    memset(&conf, 0, sizeof(conf));
    conf.chroot_home_dir = "/home/tester";

    /* ── the databases are written ────────────────────────────────── */

    bbox_update_chroot_dynamic_config(work, &conf);

    test_ok(lstat(path("etc/passwd"), &st) == 0 && S_ISREG(st.st_mode),
            "etc/passwd is written as a regular file");
    {
        struct stat host;
        stat("/etc/passwd", &host);
        test_int_eq(st.st_mode & 07777, host.st_mode & 07777,
                "with the host file's permission bits");
    }

    char *passwd = slurp(path("etc/passwd"), NULL);
    test_ok(passwd != NULL, "and can be read back");
    test_int_eq(count_lines(passwd), count_pw(),
            "one line per entry of the password database");

    {
        char *line = line_for(passwd, me->pw_name);
        char *dir = line ? field(line, 5) : NULL;
        char *pw = line ? field(line, 1) : NULL;
        test_str_eq(dir, "/home/tester",
                "the invoking user's home is the in-chroot one");
        test_str_eq(pw, "x", "and the password field is a placeholder");
        free(line); free(dir); free(pw);
    }
    {
        struct passwd *root = getpwnam("root");
        char *line = line_for(passwd, "root");
        char *dir = line ? field(line, 5) : NULL;
        char *shell = line ? field(line, 6) : NULL;
        test_str_eq(dir, root ? root->pw_dir : NULL,
                "everybody else's home is passed through");
        test_str_eq(shell, root ? root->pw_shell : NULL, "as is the shell");
        free(line); free(dir); free(shell);
    }

    test_ok(lstat(path("etc/group"), &st) == 0 && S_ISREG(st.st_mode),
            "etc/group is written as a regular file");
    char *group = slurp(path("etc/group"), NULL);
    test_int_eq(group ? count_lines(group) : -1, count_gr(),
            "one line per entry of the group database");
    {
        char *line = group ? line_for(group, "root") : NULL;
        test_ok(line && strncmp(line, "root:x:0:", 9) == 0,
                "the root group is written with a placeholder password");
        free(line);
    }
    {
        /* A group with members lists them comma-separated, no trailing comma. */
        struct group *g;
        const char *with_members = NULL;
        setgrent();
        while((g = getgrent()) != NULL) {
            if(g->gr_mem[0] && g->gr_mem[1]) {
                with_members = strdup(g->gr_name);
                break;
            }
        }
        endgrent();
        if(with_members) {
            char *line = line_for(group, with_members);
            char *mem = line ? field(line, 3) : NULL;
            test_ok(mem && strchr(mem, ',') && mem[strlen(mem) - 1] != ',',
                    "members are comma-separated without a trailing comma");
            free(line); free(mem);
        } else {
            test_ok(1, "# SKIP no group with two members to check");
        }
    }

    /* ── the copied files ─────────────────────────────────────────── */

    {
        static const char *files[] = {"/etc/hosts", "/etc/resolv.conf", NULL};
        for(size_t i = 0; files[i]; i++) {
            char label[128];
            char rel[64];
            snprintf(rel, sizeof(rel), "etc/%s", files[i] + 5);
            snprintf(label, sizeof(label), "%s is copied byte for byte", files[i]);
            if(lstat(files[i], &st) == -1) {
                snprintf(label, sizeof(label), "# SKIP %s is not on this host", files[i]);
                test_ok(1, label);
                continue;
            }
            size_t a, b;
            char *host = slurp(files[i], &a);
            char *copy = slurp(path(rel), &b);
            test_ok(host && copy && a == b && memcmp(host, copy, a) == 0, label);
            test_ok(lstat(path(rel), &st) == 0 && S_ISREG(st.st_mode),
                    "as a regular file, even where the host has a symlink");
            free(host); free(copy);
        }
    }

    test_int_eq(leftovers(), 0, "no temporary files are left behind");

    /* ── a second run replaces the files in place ─────────────────── */

    bbox_update_chroot_dynamic_config(work, &conf);
    free(passwd);
    passwd = slurp(path("etc/passwd"), NULL);
    test_int_eq(passwd ? count_lines(passwd) : -1, count_pw(),
            "a second run rewrites passwd rather than appending");
    test_int_eq(leftovers(), 0, "and still leaves no temporary files");

    /* ── a sysroot without etc/ gets nothing ──────────────────────── */

    {
        char bare_template[] = "/tmp/bbox-dynconf-bare-XXXXXX";
        char *bare = mkdtemp(bare_template);
        char p[512];

        bbox_update_chroot_dynamic_config(bare, &conf);
        snprintf(p, sizeof(p), "%s/etc", bare);
        test_ok(lstat(p, &st) == -1, "without etc/ nothing is created");
        rmdir(bare);
    }

    /* ── cleanup ──────────────────────────────────────────────────── */

    free(passwd);
    free(group);
    unlink(path("etc/passwd"));
    unlink(path("etc/group"));
    unlink(path("etc/hosts"));
    unlink(path("etc/resolv.conf"));
    rmdir(path("etc"));
    rmdir(work);

    return test_summary();
}
