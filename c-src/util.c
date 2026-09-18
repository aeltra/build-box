/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2017-2022 Tobias Koch <tobias.koch@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <libgen.h>
#include <limits.h>
#include <pwd.h>
#include <stdarg.h>
#include <string.h>
#include <sys/file.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "bbox-do.h"

/* Linux 3.5. The value is kernel ABI; older libc headers simply lack it. */
#ifndef PR_SET_NO_NEW_PRIVS
#define PR_SET_NO_NEW_PRIVS 38
#endif

#ifndef _GNU_SOURCE
extern char **environ;
#endif

#define BBOX_COPY_BUF_SIZE 4096

/* How long a sysroot lock is waited for, and how often it is tried. */
#define BBOX_LOCK_WAIT_SECS  5
#define BBOX_LOCK_POLL_MSECS 200

void bbox_getopt_begin()
{
    /*
     * glibc and musl both permute by default and both stop at the first
     * non-option on a leading "+" in the optstring. They differ in two
     * places. glibc reads the optstring's mode only on a full reset,
     * which optind = 0 asks for; at 1 it keeps the mode of the previous
     * parse, so a "+" goes unseen once another parser has run first.
     * musl re-reads every time. And glibc, not musl, honours
     * POSIXLY_CORRECT, which turns permutation off and would let the
     * caller change what a command line means. A setuid binary parses
     * under conditions it chose, not the caller's, so the variable goes.
     */
    unsetenv("POSIXLY_CORRECT");
    optind = 0;
}

void bbox_sanitize_environment()
{
    char *start, *end, *name;
    size_t i = 0;

    while((start = environ[i]) != NULL)
    {
        if(!strncmp(start, "AELTRA_", 7))
            goto next;
        if(!strncmp(start, "DISPLAY=", 8))
            goto next;
        if(!strncmp(start, "SSH_CONNECTION=", 15))
            goto next;
        if(!strncmp(start, "SSH_CLIENT=", 11))
            goto next;
        if(!strncmp(start, "SSH_TTY=", 8))
            goto next;
        if(!strncmp(start, "USER=", 5))
            goto next;
        if(!strncmp(start, "TERM=", 5))
            goto next;
        if(!strncmp(start, "HOME=", 5))
            goto next;
        if(!strncmp(start, "ASFLAGS=", 8))
            goto next;
        if(!strncmp(start, "ASFLAGS_FOR_BUILD=", 18))
            goto next;
        if(!strncmp(start, "CFLAGS=", 7))
            goto next;
        if(!strncmp(start, "CFLAGS_FOR_BUILD=", 17))
            goto next;
        if(!strncmp(start, "CPPFLAGS=", 9))
            goto next;
        if(!strncmp(start, "CPPFLAGS_FOR_BUILD=", 19))
            goto next;
        if(!strncmp(start, "CXXFLAGS=", 9))
            goto next;
        if(!strncmp(start, "CXXFLAGS_FOR_BUILD=", 19))
            goto next;
        if(!strncmp(start, "DFLAGS=", 7))
            goto next;
        if(!strncmp(start, "DFLAGS_FOR_BUILD=", 17))
            goto next;
        if(!strncmp(start, "FCFLAGS=", 8))
            goto next;
        if(!strncmp(start, "FCFLAGS_FOR_BUILD=", 18))
            goto next;
        if(!strncmp(start, "FFLAGS=", 7))
            goto next;
        if(!strncmp(start, "FFLAGS_FOR_BUILD=", 17))
            goto next;
        if(!strncmp(start, "LDFLAGS=", 8))
            goto next;
        if(!strncmp(start, "LDFLAGS_FOR_BUILD=", 18))
            goto next;
        if(!strncmp(start, "OBJCFLAGS=", 10))
            goto next;
        if(!strncmp(start, "OBJCFLAGS_FOR_BUILD=", 20))
            goto next;
        if(!strncmp(start, "OBJCXXFLAGS=", 12))
            goto next;
        if(!strncmp(start, "OBJCXXFLAGS_FOR_BUILD=", 22))
            goto next;

        /*
         * An entry with no "=" or nothing in front of it is not a variable
         * and cannot be named to unsetenv(), which refuses an empty name.
         * execve(2) passes such strings through unchecked, so they do turn
         * up. Splice the entry out the way unsetenv() would.
         */
        if((end = strchr(start, '=')) == NULL || end == start) {
            for(size_t j = i; environ[j] != NULL; j++)
                environ[j] = environ[j + 1];
            continue;
        }

        name = strndup(start, end - start);

        if(!name) {
            bbox_perror("bbox_sanitize_environment", "out of memory?\n");
            abort();
        }

        unsetenv(name);
        free(name);

        /* After unsetenv, environ[i] is now the next entry. */
        continue;

    next:
        i++;
    }
}

int bbox_copy_file(const char *src, const char *dst)
{
    struct stat src_st, dst_st;
    char buf[BBOX_COPY_BUF_SIZE];
    char *tmp_dst = NULL;
    size_t dst_len = strlen(dst);
    char *ptr = NULL;
    int in_fd = -1, out_fd = -1, rval = -1;
    ssize_t num_bytes_read, num_bytes_written;

    /*
     * stat(), not lstat(): the content is read through a symlink, so the
     * mode has to come from the same place. resolv.conf is a symlink on
     * hosts running resolvconf or systemd-resolved, and a symlink's own
     * mode is 0777.
     */
    if(stat(src, &src_st) == -1) {
        bbox_perror("bbox_copy_file", "could not stat '%s'.\n", src);
        goto cleanup_and_exit;
    }

    if(lstat(dst, &dst_st) ==  0) {
        if(S_ISLNK(dst_st.st_mode) || !S_ISREG(dst_st.st_mode)) {
            bbox_perror("bbox_copy_file",
                    "destination is not a regular file.\n");
            goto cleanup_and_exit;
        }
    }

    if((tmp_dst = malloc(strlen(dst) + strlen("-XXXXXX") + 1)) == NULL) {
        bbox_perror("bbox_copy_file", "out of memory?\n");
        abort();
    }

    strncpy(tmp_dst, dst, dst_len);
    strncpy(tmp_dst + dst_len, "-XXXXXX", 8);

    if((out_fd = mkstemp(tmp_dst)) == -1) {
        bbox_perror(
            "bbox_copy_file",
            "failed to open temporary file '%s' for writing: %s\n",
            tmp_dst, strerror(errno)
        );
        goto cleanup_and_exit;
    }
    /* Permission bits only. A copy the user makes must never be setuid. */
    fchmod(out_fd, src_st.st_mode & 0777);

    if((in_fd = open(src, O_RDONLY)) == -1) {
        bbox_perror("bbox_copy_file", "failed to open '%s' for reading: %s\n",
                src, strerror(errno));
        goto cleanup_and_exit;
    }

    while(1)
    {
        num_bytes_read = read(in_fd, (void*) buf, BBOX_COPY_BUF_SIZE);

        if(!num_bytes_read)
            break;

        if(num_bytes_read == -1) {
            if(errno != EINTR) {
                goto cleanup_and_exit;
            } else {
                continue;
            }
        }

        ptr = buf;

        while(num_bytes_read) {
            num_bytes_written = write(out_fd, ptr, num_bytes_read);

            if(num_bytes_written == -1) {
                if(errno != EINTR) {
                    goto cleanup_and_exit;
                } else {
                    continue;
                }
            }

            num_bytes_read -= num_bytes_written;
            ptr += num_bytes_written;
        }
    }

    rval = 0;

cleanup_and_exit:

    if(in_fd != -1)
        close(in_fd);
    if(out_fd != -1)
        close(out_fd);

    if(tmp_dst && lstat(tmp_dst, &dst_st) == 0) {
        if(rval == 0) {
            rename(tmp_dst, dst);
        } else {
            unlink(tmp_dst);
        }
    }

    free(tmp_dst);
    return rval;
}

void bbox_path_join(char **buf_ptr, const char *base, const char *sub,
        size_t *n_ptr)
{
    size_t base_len = strlen(base);
    size_t sub_len  = strlen(sub);
    size_t req_buf_size;
    int base_is_buffer = 0;
    int need_sep = 0;

    if(base == *buf_ptr)
        base_is_buffer = 1;

    while(sub[0] == '/') {
        sub++;
        sub_len--;
    }

    /* Only insert a separator if there is something to separate. */
    if(sub_len > 0 && (base_len == 0 || base[base_len-1] != '/'))
        need_sep = 1;

    req_buf_size = base_len + need_sep + sub_len + 1;

    if(req_buf_size > *n_ptr)
    {
        *buf_ptr = realloc(*buf_ptr, req_buf_size);

        if(!*buf_ptr) {
            bbox_perror("bbox_path_join", "out of memory?\n");
            abort();
        }

        if(base_is_buffer)
            base = *buf_ptr;

        *n_ptr = req_buf_size;
    }

    memmove((void*) *buf_ptr, base, base_len + 1);

    if(need_sep)
        (*buf_ptr)[base_len++] = '/';

    memmove((void*) *buf_ptr + base_len, sub, sub_len + 1);
}

static void bbox_pmessage(const char *lead, const char *level,
        const char *color, const char *format, va_list ap)
{
    char *bold = "\033[1m";
    char *rst  = "\033[0m";

    if(isatty(STDOUT_FILENO)) {
        fprintf(stderr, "%sbuild-box %s%s: %s%s%s%s: ",
            bold, lead, rst, bold, color, level, rst);
    } else {
        fprintf(stderr, "build-box-do %s: %s: ", lead, level);
    }
    vfprintf(stderr, format, ap);
}

void bbox_perror(const char *lead, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    bbox_pmessage(lead, "error", "\033[31m", format, ap);
    va_end(ap);
}

void bbox_pwarning(const char *lead, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    bbox_pmessage(lead, "warning", "\033[33m", format, ap);
    va_end(ap);
}

int bbox_login_sh_chrooted(char *sys_root, char *home_dir)
{
    static char *shells[] = {"/tools/bin/sh", "/usr/bin/sh", NULL};

    char *sh = NULL;
    struct stat st;
    uid_t uid = getuid();

    /* change into system folder. */
    if(chdir(sys_root) == -1) {
        bbox_perror("bbox_login_sh_chrooted",
                "could not chdir to '%s': %s.\n",
                sys_root, strerror(errno));
        return -1;
    }

    /* do a few sanity checks before chrooting. */
    if(lstat(".", &st) == -1) {
        bbox_perror("bbox_login_sh_chrooted",
                "failed to stat '%s': %s.\n", sys_root,
                strerror(errno));
        return -1;
    }
    if(st.st_uid != uid) {
        bbox_perror("bbox_login_sh_chrooted",
                "system root is not owned by user.\n");
        return -1;
    }

    if(bbox_raise_privileges() == -1)
        return -1;

    if(bbox_reset_supplementary_groups() == -1) {
        bbox_lower_privileges();
        return -1;
    }

    if(chroot(".") == -1) {
        bbox_perror("bbox_login_sh_chrooted",
                "chroot to system root failed: %s.\n",
                strerror(errno));
        bbox_lower_privileges();
        return -1;
    }

    if(bbox_drop_privileges() == -1)
        _exit(BBOX_ERR_RUNTIME);

    if(bbox_no_new_privs() == -1)
        _exit(BBOX_ERR_RUNTIME);

    /* Do this while we're at the fs root. */
    bbox_try_fix_pkg_cache_symlink("login", home_dir);

    if(home_dir)
        (void)chdir(home_dir);

    /* search for a shell. */
    for(size_t i = 0; (sh = shells[i]) != NULL; i++) {
        if(lstat(sh, &st) == 0)
            break;
        else
            sh = NULL;
    }

    if(!sh) {
        bbox_perror("bbox_login_sh_chrooted",
                "could not find a shell.\n");
        return -1;
    }

    execlp(sh, "sh", "-l", (char*) NULL);

    bbox_perror("bbox_login_sh_chrooted", "failed to invoke shell: %s.\n",
            strerror(errno));
    return -1;
}

void bbox_update_chroot_dynamic_config(const char *sys_root,
        const bbox_conf_t *conf)
{
    struct stat st;
    char *buf1 = NULL;
    char *buf2 = NULL;
    size_t buf1_len = 0;
    size_t buf2_len = 0;
    int out_fd = -1;

    uid_t invoking_uid = getuid();
    const char *chroot_home = bbox_config_get_chroot_home_dir(conf);

    /* Copy the password database. */

    if(lstat("/etc/passwd", &st) == -1) {
        bbox_perror(
            "bbox_update_chroot_dynamic_config",
            "failed to stat '/etc/passwd': %s\n",
            strerror(errno)
        );
        goto cleanup_and_exit;
    }

    bbox_path_join(&buf1, sys_root, "/etc/passwd-XXXXXX", &buf1_len);
    bbox_path_join(&buf2, sys_root, "/etc/passwd", &buf2_len);

    if((out_fd = mkstemp(buf1)) == -1) {
        bbox_perror(
            "bbox_update_chroot_dynamic_config",
            "failed to open temporary file '%s' for writing: %s\n",
            buf1, strerror(errno)
        );
        goto cleanup_and_exit;
    }
    fchmod(out_fd, st.st_mode);

    struct passwd *pwd = NULL;
    while((pwd = getpwent()) != NULL) {
        /*
         * For the invoking user, substitute the home directory with
         * the in-chroot home path so that tools consulting passwd
         * inside the chroot see the correct home directory.
         */
        const char *pw_dir = pwd->pw_dir;
        if(pwd->pw_uid == invoking_uid && chroot_home)
            pw_dir = chroot_home;

        dprintf(
            out_fd,
            "%s:%s:%ld:%ld:%s:%s:%s\n",
            pwd->pw_name,
            "x",
            (long) pwd->pw_uid,
            (long) pwd->pw_gid,
            pwd->pw_gecos,
            pw_dir,
            pwd->pw_shell
        );
    }
    endpwent();

    close(out_fd);
    out_fd = -1;
    rename(buf1, buf2);

    /* Copy the group database. */

    if(lstat("/etc/group", &st) == -1) {
        bbox_perror(
            "bbox_update_chroot_dynamic_config",
            "failed to stat '/etc/group': %s\n",
            strerror(errno)
        );
        goto cleanup_and_exit;
    }

    bbox_path_join(&buf1, sys_root, "/etc/group-XXXXXX", &buf1_len);
    bbox_path_join(&buf2, sys_root, "/etc/group", &buf2_len);

    if((out_fd = mkstemp(buf1)) == -1) {
        bbox_perror(
            "bbox_update_chroot_dynamic_config",
            "failed to open temporary file '%s' for writing: %s\n",
            buf1, strerror(errno)
        );
        goto cleanup_and_exit;
    }
    fchmod(out_fd, st.st_mode);

    struct group *grp = NULL;
    while((grp = getgrent()) != NULL) {
        dprintf(
            out_fd,
            "%s:%s:%ld:",
            grp->gr_name,
            "x",
            (long) grp->gr_gid
        );

        for(size_t i = 0; grp->gr_mem[i] != NULL; i++) {
            if(grp->gr_mem[i+1] != NULL) {
                dprintf(out_fd, "%s,", grp->gr_mem[i]);
            } else {
                dprintf(out_fd, "%s", grp->gr_mem[i]);
            }
        }

        dprintf(out_fd, "\n");
    }
    endgrent();

    close(out_fd);
    out_fd = -1;
    rename(buf1, buf2);

    /* Copy other files. */

    char *file_list[] = {
        "/etc/resolv.conf",
        "/etc/hosts",
        NULL
    };

    for(size_t i = 0; file_list[i] != NULL; i++) {
        char *file = file_list[i];

        if(lstat(file, &st) == -1)
            continue;

        bbox_path_join(&buf1, sys_root, file, &buf1_len);
        bbox_copy_file(file, buf1);
    }

cleanup_and_exit:
    if(out_fd != -1)
        close(out_fd);

    free(buf1);
    free(buf2);
}

int bbox_lower_privileges()
{
    if(seteuid(getuid()) == -1) {
        bbox_perror("bbox_lower_privileges",
                "failed to lower privileges: %s.\n",
                    strerror(errno));
        return -1;
    }

    return 0;
}

int bbox_raise_privileges()
{
    if(seteuid(0) == -1) {
        bbox_perror("bbox_raise_privileges",
                "failed to restore root privileges: %s.\n",
                    strerror(errno));
        return -1;
    }

    return 0;
}

int bbox_drop_privileges()
{
    uid_t uid = getuid();

    /*
     * Raise privileges first so that the subsequent setuid() call operates
     * in privileged mode. When euid == 0, setuid() sets all three UIDs
     * (real, effective, saved) to the target value, permanently dropping
     * privileges. Without this, calling setuid() from an unprivileged
     * state would only change the effective UID, leaving the saved
     * set-user-ID at 0 and privileges recoverable.
     */
    if(seteuid(0) == -1) {
        bbox_perror("bbox_drop_privileges",
                "could not raise privileges for permanent drop: %s.\n",
                    strerror(errno));
        return -1;
    }

    if(setgid(getgid()) == -1) {
        bbox_perror("bbox_drop_privileges",
                "could not drop group privileges: %s.\n",
                    strerror(errno));
        return -1;
    }

    if(setuid(uid) == -1) {
        bbox_perror("bbox_drop_privileges",
                "could not drop privileges: %s.\n",
                    strerror(errno));
        return -1;
    }

    return 0;
}

int bbox_no_new_privs()
{
    /*
     * From here on no execve() in this process or any descendant grants
     * privileges: setuid and setgid bits and file capabilities are ignored.
     * The flag cannot be cleared and is inherited across fork and exec.
     *
     * The chroot is a tree the user wrote, and everything a setuid binary
     * reads after chroot() -- passwd, shadow, PAM, libc -- comes from it. A
     * root-owned setuid binary hard-linked into the tree would hand the
     * user root. With this flag it runs as the user, whatever it is and
     * wherever it sits.
     */
    if(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == -1) {
        bbox_perror("bbox_no_new_privs",
                "failed to set no_new_privs: %s.\n", strerror(errno));
        return -1;
    }

    return 0;
}

int bbox_reset_supplementary_groups()
{
    gid_t gid = getgid();

    if(setgroups(1, &gid) == -1) {
        bbox_perror("bbox_reset_supplementary_groups",
                "failed to reset supplementary groups: %s.\n",
                strerror(errno));
        return -1;
    }

    return 0;
}

int bbox_check_user_in_group_build_box()
{
    struct group grp, *result = NULL;

    int    rval   = -1;
    char  *buf    = NULL;
    size_t buflen = 1024;
    gid_t *groups = NULL;

    /* Can't believe all this is needed just to get the group id by name!!! */
    while(1) {
        buf = realloc(buf, buflen);

        if(buf == NULL) {
            bbox_perror("bbox_check_user_in_group_build_box",
                    "out of memory?\n");
            goto cleanup_and_exit;
        }

        /* According to man page errno has to be initialized. Why?! */
        errno = 0;

        int rval = getgrnam_r(BBOX_GROUP_NAME, &grp, buf, buflen, &result);

        if(result)
            break;

        if(rval == 0) {
            bbox_perror("bbox_check_user_in_group_build_box",
                    "group '" BBOX_GROUP_NAME "' not found.\n");
            goto cleanup_and_exit;
        }

        if(rval == ERANGE) {
            buflen *= 2;
            continue;
        }

        if(rval == EINTR)
            continue;

        bbox_perror("bbox_check_user_in_group_build_box",
                "error retrieving group info: %s.\n", strerror(errno));
        goto cleanup_and_exit;
    }

    gid_t gid = grp.gr_gid;

    /* Someone might think using the primary groups is a good idea. */
    if(getegid() == gid) {
        rval = 0;
        goto cleanup_and_exit;
    }

    /* Get the number of supplementary groups. */
    int ngroups = getgroups(0, NULL);

    if(ngroups == -1) {
        bbox_perror("bbox_check_user_in_group_build_box",
                "error getting number of suplementary groups.\n");
        goto cleanup_and_exit;
    }

    /* Get the group list. */
    groups = realloc(groups, sizeof(gid_t) * ngroups);

    if(groups == NULL) {
        bbox_perror("bbox_check_user_in_group_build_box",
                "out of memory?\n");
        goto cleanup_and_exit;
    }

    if(getgroups(ngroups, groups) != ngroups) {
        bbox_perror("bbox_check_user_in_group_build_box",
                "error fetching group list: %s", strerror(errno));
        goto cleanup_and_exit;
    }

    /* Now, finally (!), check if group is in group list. Phew... */
    for(size_t i = 0; i < ngroups; i++) {
        if(gid == groups[i]) {
            rval = 0;
            break;
        }
    }

    if(rval != 0) {
        bbox_perror("bbox_check_user_in_group_build_box",
                "user is not in group '" BBOX_GROUP_NAME "'.\n");
    }

cleanup_and_exit:

    free(buf);
    free(groups);
    return rval;
}

int bbox_isdir_and_owned_by(const char *module, const char *dir, uid_t uid)
{
    struct stat st;

    char *normalized = realpath(dir, NULL);

    if(!normalized) {
        bbox_perror(
            module, "unable to normalize path '%s': %s.\n",
            dir, strerror(errno)
        );
        return -1;
    }

    if(lstat(normalized, &st) == -1) {
        bbox_perror(
            module, "could not stat '%s': %s.\n", dir, strerror(errno)
        );
        free(normalized);
        return -1;
    }
    if(S_ISLNK(st.st_mode) || !S_ISDIR(st.st_mode)) {
        bbox_perror(module, "%s is not a directory.\n", dir);
        free(normalized);
        return -1;
    }
    if(st.st_uid != uid) {
        bbox_perror(module, "directory '%s' is not owned by user id '%ld'.\n",
                dir, (long) uid);
        free(normalized);
        return -1;
    }

    free(normalized);
    return 0;
}

int bbox_open_dir_owned_by(const char *module, const char *dir, uid_t uid)
{
    struct stat st;

    char *normalized = realpath(dir, NULL);

    if(!normalized) {
        bbox_perror(
            module, "unable to normalize path '%s': %s.\n",
            dir, strerror(errno)
        );
        return -1;
    }

    int fd = open(normalized, O_PATH | O_DIRECTORY | O_NOFOLLOW);

    if(fd == -1) {
        bbox_perror(
            module, "could not open '%s': %s.\n", dir, strerror(errno)
        );
        free(normalized);
        return -1;
    }

    free(normalized);

    if(fstat(fd, &st) == -1) {
        bbox_perror(
            module, "could not stat '%s': %s.\n", dir, strerror(errno)
        );
        close(fd);
        return -1;
    }

    if(!S_ISDIR(st.st_mode)) {
        bbox_perror(module, "%s is not a directory.\n", dir);
        close(fd);
        return -1;
    }

    if(st.st_uid != uid) {
        bbox_perror(module, "directory '%s' is not owned by user id '%ld'.\n",
                dir, (long) uid);
        close(fd);
        return -1;
    }

    return fd;
}

int bbox_lock_dir(const char *module, const char *dir)
{
    struct timespec poll = {0, BBOX_LOCK_POLL_MSECS * 1000000L};
    int tries = BBOX_LOCK_WAIT_SECS * 1000 / BBOX_LOCK_POLL_MSECS;
    int fd = -1;

    /*
     * An exclusive flock(2) on the directory, held until the returned
     * descriptor is closed. Every invocation that mounts or unmounts below
     * the same sysroot takes it around the check whether something is
     * mounted and the mount or unmount that follows. Without it, two
     * invocations that both pass the check both mount, and the kernel
     * stacks the second mount on top of the first, where only the top one
     * gets its flags and propagation set and a single unmount only exposes
     * the one below.
     *
     * The lock is taken with lowered privileges, so it lands only on
     * directories the user can open. It is not a security check, only a
     * serialization point; the lock identity is the directory's inode, so
     * every path to the same sysroot means the same lock. flock() refuses
     * O_PATH descriptors, hence the plain O_RDONLY.
     *
     * The wait is bounded. Anyone who can open the directory for reading
     * can hold the same lock, and it is taken before anything is printed,
     * so an unbounded wait would be a silent hang at somebody else's
     * pleasure. A holder of our own is done in milliseconds.
     */
    fd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);

    if(fd == -1) {
        bbox_perror(
            module, "could not open '%s': %s.\n", dir, strerror(errno)
        );
        return -1;
    }

    while(flock(fd, LOCK_EX | LOCK_NB) == -1) {
        if(errno == EINTR)
            continue;

        if(errno != EWOULDBLOCK) {
            bbox_perror(
                module, "could not lock '%s': %s.\n", dir, strerror(errno)
            );
            close(fd);
            return -1;
        }

        if(tries-- == 0) {
            bbox_perror(
                module, "could not lock '%s': another process has been "
                "holding it for %d seconds.\n", dir, BBOX_LOCK_WAIT_SECS
            );
            close(fd);
            return -1;
        }

        nanosleep(&poll, NULL);
    }

    return fd;
}

long bbox_fd_mount_id(const char *module, int fd)
{
    char path[64];
    char line[128];
    long mount_id = -1;
    FILE *fp = NULL;

    /*
     * The kernel reports the id of the mount an open descriptor refers to in
     * /proc/self/fdinfo. This works with any libc, unlike statx(2), which
     * older versions of musl do not provide.
     */
    snprintf(path, sizeof(path), "/proc/self/fdinfo/%d", fd);

    if(!(fp = fopen(path, "re"))) {
        bbox_perror(
            module, "could not open '%s': %s.\n", path, strerror(errno)
        );
        return -1;
    }

    while(fgets(line, sizeof(line), fp)) {
        if(sscanf(line, "mnt_id: %ld", &mount_id) == 1)
            break;
    }

    fclose(fp);

    if(mount_id == -1)
        bbox_perror(module, "kernel does not report mount ids.\n");

    return mount_id;
}

int bbox_is_mount_point_at(const char *module, int dir_fd, const char *name)
{
    long parent_mount_id = -1;
    long child_mount_id = -1;
    int fd = -1;
    int rval = -1;

    /*
     * Open the entry relative to the parent directory, without following
     * symlinks, and compare the mount ids of both. They only differ if there
     * is a mount on top of the entry.
     */
    if((fd = openat(dir_fd, name, O_PATH | O_DIRECTORY | O_NOFOLLOW)) == -1) {
        bbox_perror(
            module, "could not open '%s': %s.\n", name, strerror(errno)
        );
        return -1;
    }

    if((parent_mount_id = bbox_fd_mount_id(module, dir_fd)) == -1)
        goto cleanup_and_exit;
    if((child_mount_id = bbox_fd_mount_id(module, fd)) == -1)
        goto cleanup_and_exit;

    rval = parent_mount_id != child_mount_id;

cleanup_and_exit:

    /*
     * The descriptor must not outlive this function. An open descriptor pins
     * the mount and would make a subsequent unmount fail with EBUSY.
     */
    close(fd);
    return rval;
}

/*
 * mkdir -p, done here rather than by spawning mkdir(1): a setuid binary has
 * no business searching the caller's PATH for a helper, and a minimal
 * chroot has no mkdir to find. Returns -1 with errno set and no message,
 * so callers can decide how loudly to report.
 */
static int bbox_mkdir_p_quiet(const char *path)
{
    char *buf = NULL;
    char *p = NULL;
    struct stat st;
    int rval = -1;

    /* The loop below starts at the second byte, which "" does not have. */
    if(path[0] == '\0') {
        errno = ENOENT;
        return -1;
    }

    if((buf = strdup(path)) == NULL) {
        bbox_perror("bbox_mkdir_p", "out of memory?\n");
        abort();
    }

    /*
     * Create each leading component in turn, then the path itself. An
     * existing directory is fine; anything else in the way is an error.
     */
    for(p = buf + 1; ; p++) {
        if(*p != '/' && *p != '\0')
            continue;

        char saved = *p;
        *p = '\0';

        if(mkdir(buf, 0777) == -1) {
            if(errno != EEXIST || stat(buf, &st) == -1 || !S_ISDIR(st.st_mode))
            {
                if(errno == EEXIST)
                    errno = ENOTDIR;
                *p = saved;
                goto cleanup_and_exit;
            }
        }

        *p = saved;

        if(saved == '\0')
            break;
    }

    rval = 0;

cleanup_and_exit:

    free(buf);
    return rval;
}

int bbox_mkdir_p(const char *module, const char *path)
{
    if(bbox_mkdir_p_quiet(path) == -1) {
        bbox_perror(module, "failed to create directory '%s': %s.\n", path,
                strerror(errno));
        return -1;
    }

    return 0;
}

int bbox_sysroot_mkdir_p(const char *module, const char *sys_root,
        const char *path)
{
    char *buf = NULL;
    size_t buf_len = 0;

    bbox_path_join(&buf, sys_root, path, &buf_len);

    int rval = bbox_mkdir_p(module, buf);
    free(buf);
    return rval;
}

int bbox_try_fix_pkg_cache_symlink(const char *module,
        const char *chroot_home)
{
    int rval = 0;
    struct stat link_st;

    if(lstat("/.pkg-cache", &link_st) == -1) {
        if(chroot_home) {
            char *fallback = NULL;
            size_t fb_len = 0;
            bbox_path_join(&fallback, chroot_home,
                    "RealHome/.aeltra/cache/aeltra", &fb_len);
            int rv = symlink(fallback, "/.pkg-cache");
            free(fallback);
            return rv;
        }
        return symlink("/var/cache/aept", "/.pkg-cache");
    }

    size_t bufsize = link_st.st_size ? link_st.st_size + 1 : PATH_MAX;
    char *buf = malloc(bufsize);

    if(buf == NULL) {
        bbox_perror(module, "out of memory?\n");
        abort();
    }

    ssize_t nbytes = readlink("/.pkg-cache", buf, bufsize);

    if(nbytes < 0 || nbytes >= bufsize) {
        rval = -1;
        goto cleanup_and_exit;
    }

    buf[nbytes] = '\0';

    if(bbox_mkdir_p_quiet(buf) == -1) {
        bbox_pwarning(
            module, "failed to create package cache directory '%s': %s.\n",
            buf, strerror(errno)
        );
    }

cleanup_and_exit:

    free(buf);
    return rval;
}

char *bbox_get_user_dir(uid_t uid, size_t *n_ptr)
{
    int buf_size = snprintf(NULL, 0, BBOX_USER_DIR_TEMPLATE,
            (unsigned long) uid) + 1;

    char *user_dir = NULL;

    if((user_dir = malloc(buf_size)) == NULL) {
        bbox_perror("bbox_get_user_dir", "out of memory!\n");
        abort();
    }

    int needed_bytes = snprintf(user_dir, buf_size, BBOX_USER_DIR_TEMPLATE,
            (unsigned long) uid);

    if(needed_bytes >= buf_size)
        goto failure;

    if(n_ptr != NULL)
        *n_ptr = buf_size;
    return user_dir;

failure:
    free(user_dir);
    return NULL;
}

int bbox_validate_entry_name(const char *module, const char *name)
{
    /*
     * Mount points are addressed as a single path component relative to a
     * verified parent directory. A name containing a slash would introduce
     * additional lookups that cannot be pinned down.
     */
    if(name[0] == '\0' || strchr(name, '/') != NULL) {
        bbox_perror(module, "invalid mount point name '%s'.\n", name);
        return -1;
    }

    return 0;
}

int validate_target_name(const char *module, const char *target_name)
{
    size_t len = strlen(target_name);

    if(len == 0)
        return -1;

    if(strcmp(target_name, ".") == 0 || strcmp(target_name, "..") == 0) {
        bbox_perror(
            module,
            "a target name must not be '.' or '..'.\n"
        );
        return -1;
    }

    for(size_t i = 0; i < len; i++) {
        int ch = target_name[i];

        switch(ch) {
            case '-':
            case '_':
            case '.':
                continue;
            default:
                break;
        }

        if(ch >= 'a' && ch <= 'z')
            continue;
        if(ch >= 'A' && ch <= 'Z')
            continue;
        if(ch >= '0' && ch <= '9')
            continue;

        bbox_perror(
            module,
            "a target name must consist of characters matching "
            "[-_a-zA-Z0-9.].\n"
        );

        return -1;
    }

    return 0;
}
