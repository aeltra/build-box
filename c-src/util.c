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
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "bbox-do.h"

#ifndef _GNU_SOURCE
extern char **environ;
#endif

#define BBOX_COPY_BUF_SIZE 4096

void bbox_sanitize_environment()
{
    char *start, *end, *name;

    for(size_t i = 0; (start = environ[i]) != NULL; i++)
    {
        if(!strncmp(start, "AELTRA_", 7))
            continue;
        if(!strncmp(start, "DISPLAY=", 8))
            continue;
        if(!strncmp(start, "SSH_CONNECTION=", 15))
            continue;
        if(!strncmp(start, "SSH_CLIENT=", 11))
            continue;
        if(!strncmp(start, "SSH_TTY=", 8))
            continue;
        if(!strncmp(start, "USER=", 5))
            continue;
        if(!strncmp(start, "TERM=", 5))
            continue;
        if(!strncmp(start, "HOME=", 5))
            continue;
        if(!strncmp(start, "CFLAGS=", 7))
            continue;
        if(!strncmp(start, "CXXFLAGS=", 9))
            continue;
        if(!strncmp(start, "CPPFLAGS=", 9))
            continue;
        if(!strncmp(start, "LDFLAGS=", 8))
            continue;

        if((end = strchr(start, '=')) == NULL)
            continue;

        name = strndup(start, end - start);

        if(!name) {
            bbox_perror("bbox_sanitize_environment", "out of memory?\n");
            abort();
        }

        unsetenv(name);
        free(name);
        i = 0;
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
    int num_bytes_read, num_bytes_written;

    if(lstat(src, &src_st) == -1) {
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
    fchmod(out_fd, src_st.st_mode);

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

    if(lstat(tmp_dst, &dst_st) == 0) {
        if(rval == 0) {
            rename(tmp_dst, dst);
        } else {
            unlink(tmp_dst);
        }
    }

    free(tmp_dst);
    return rval;
}

void bbox_sep_join(char **buf_ptr, const char *base, const char *sep,
        const char *sub, size_t *n_ptr)
{
    size_t base_len     = strlen(base);
    size_t sub_len      = strlen(sub);
    size_t sep_len      = strlen(sep);
    size_t req_buf_size = base_len + sep_len + sub_len + 1;
    int base_is_buffer  = 0;

    if(base == *buf_ptr)
        base_is_buffer = 1;

    if(req_buf_size > *n_ptr)
    {
        *buf_ptr = realloc(*buf_ptr, req_buf_size);

        if(!*buf_ptr) {
            bbox_perror("bbox_sep_join", "out of memory?\n");
            abort();
        }

        if(base_is_buffer)
            base = *buf_ptr;

        *n_ptr = req_buf_size;
    }

    memmove((void*) *buf_ptr, base, base_len + 1);
    memmove((void*) *buf_ptr + base_len, sep, sep_len + 1);
    memmove((void*) *buf_ptr + base_len + sep_len, sub, sub_len + 1);
}

void bbox_path_join(char **buf_ptr, const char *base, const char *sub,
        size_t *n_ptr)
{
    size_t base_len = strlen(base);
    size_t sub_len  = strlen(sub);
    size_t req_buf_size = base_len + sub_len + 1;
    int base_is_buffer = 0;

    if(base == *buf_ptr)
        base_is_buffer = 1;

    if(base[base_len-1] != '/')
        req_buf_size++;

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

    if(base[base_len-1] != '/')
        (*buf_ptr)[base_len++] = '/';

    while(sub[0] == '/') {
        sub++;
        sub_len--;
    }

    memmove((void*) *buf_ptr + base_len, sub, sub_len + 1);
}

void bbox_perror(const char *lead, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);

    char *bold = "\033[1m";
    char *red  = "\033[31m";
    char *rst  = "\033[0m";

    if(isatty(STDOUT_FILENO)) {
        fprintf(stderr, "%sbuild-box %s%s: %s%serror%s: ",
            bold, lead, rst, bold, red, rst);
    } else {
        fprintf(stderr, "build-box-do %s: error: ", lead);
    }
    vfprintf(stderr, format, ap);

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

    if(chroot(".") == -1) {
        bbox_perror("bbox_login_sh_chrooted",
                "chroot to system root failed: %s.\n",
                strerror(errno));
        return -1;
    }

    if(bbox_drop_privileges() == -1)
        _exit(BBOX_ERR_RUNTIME);

    /* Do this while we're at the fs root. */
    bbox_try_fix_pkg_cache_symlink("");

    if(home_dir)
        if(chdir(home_dir) == -1);

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

int bbox_run_command_capture(uid_t uid, const char *cmd, char * const argv[],
        char **out_buf, size_t *out_buf_size)
{
    int pid;
    int child_status;
    int pipefd[2];
    int bytes_read;
    int total_read = 0;
    int req_space;
    char buf[BBOX_COPY_BUF_SIZE];

    if(pipe(pipefd) == -1) {
        bbox_perror("bbox_run_command_capture",
                "failed to construct pipe: %s.\n",
                strerror(errno));
        return -1;
    }

    /*
     * It is important that we don't leave the buffer uninitialized, if the
     * command won't produce any output.
     */
    if(*out_buf_size == 0) {
        *out_buf_size = 256;
        *out_buf = malloc(*out_buf_size);

        if(!*out_buf) {
            bbox_perror("bbox_run_command_capture", "out of memory!\n");
            abort();
        }

        (*out_buf)[0] = '\0';
    }

    if((pid = fork()) == -1) {
        bbox_perror("bbox_run_command_capture",
                "failed to start subprocess: %s.\n",
                strerror(errno));
        return -1;
    }

    /* this is the child exec'ing for example 'mount'. */
    if(pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        setenv("LC_ALL", "C", 1);
        if(uid == 0) {
            if(bbox_raise_privileges() == -1)
                _exit(BBOX_ERR_RUNTIME);
        }
        execvp(cmd, argv);

        /* if we make it here exec failed. */
        _exit(BBOX_ERR_RUNTIME);
    }

    close(pipefd[1]);

    while((bytes_read = read(pipefd[0], buf, BBOX_COPY_BUF_SIZE)) > 0) {
        req_space = total_read + bytes_read + 1;

        if(req_space > *out_buf_size) {
            *out_buf_size = req_space * 2;
            *out_buf = realloc(*out_buf, *out_buf_size);

            if(!*out_buf) {
                bbox_perror("bbox_run_command_capture", "out of memory!\n");
                break;
            }
        }

        memcpy(*out_buf + total_read, buf, bytes_read);
        total_read += bytes_read;
        (*out_buf)[total_read] = '\0';

        /* 4MB should be plenty for our use cases. */
        if(total_read > 4 * 1024 * 1024)
            break;
    }

    int done = 0;

    // rtrim string.
    while(!done && total_read >= 0) {
        switch((*out_buf)[total_read]) {
            case '\r':
            case '\n':
            case ' ':
            case 127:
            case '\0':
                (*out_buf)[total_read--] = '\0';
                break;
            default:
                done = 1;
                break;
        }
    }

    if(waitpid(pid, &child_status, 0) == -1) {
        bbox_perror("bbox_run_command_capture",
                "unable to retrieve child exit status: %s.\n",
                strerror(errno));
        return -1;
    }

    return WEXITSTATUS(child_status);
}

void bbox_update_chroot_dynamic_config(const char *sys_root)
{
    struct stat st;
    char *buf1 = NULL;
    char *buf2 = NULL;
    size_t buf1_len = 0;
    size_t buf2_len = 0;
    int out_fd = -1;

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
        dprintf(
            out_fd,
            "%s:%s:%ld:%ld:%s:%s:%s\n",
            pwd->pw_name,
            pwd->pw_passwd,
            (long) pwd->pw_uid,
            (long) pwd->pw_gid,
            pwd->pw_gecos,
            pwd->pw_dir,
            pwd->pw_shell
        );
    }

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
            grp->gr_passwd,
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
    if(setuid(0) == -1) {
        bbox_perror("bbox_raise_privileges",
                "failed to restore root privileges: %s.\n",
                    strerror(errno));
        return -1;
    }

    return 0;
}

int bbox_drop_privileges()
{
    if(setuid(getuid()) == -1) {
        bbox_perror("bbox_drop_privileges",
                "could not drop privileges: %s.\n",
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

    /*
     * It seems important to stat the normalized path to avoid any symlink
     * trickery. Hopefully, that is enough.
     */
    char *normalized = realpath(dir, NULL);

    if(!normalized) {
        bbox_perror(
            module, "unable to normalize path '%s': %s.\n",
            dir,
            strerror(errno)
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

int bbox_mkdir_p(const char *module, const char *path)
{
    char *out_buf = NULL;
    size_t out_buf_len = 0;
    char * const argv[] = {"mkdir", "-p", (char*) path, NULL};

    int rval = bbox_run_command_capture(getuid(), "mkdir", argv, &out_buf,
            &out_buf_len);

    if(rval != 0) {
        if(out_buf) {
            bbox_perror(
                module, "failed to create directory %s: \"%s\".\n",
                path, out_buf
            );
        }

        rval = -1;
    }

    return rval;
}

int bbox_sysroot_mkdir_p(const char *module, const char *sys_root,
        const char *path)
{
    char *buf = NULL;
    size_t buf_len = 0;

    bbox_path_join(&buf, sys_root, path, &buf_len);

    return bbox_mkdir_p(module, buf);
}

int bbox_is_subdir_of(const char *path, const char *subdir) 
{
    char *real_path = NULL;
    char *real_subdir = NULL;
    char *buf = NULL;
    int rval = -1;

    real_path = realpath(path, NULL);
    if(!real_path) {
        bbox_perror(
            "bbox_is_subdir_of",
            "unable to normalize path %s: %s.\n",
            path, strerror(errno)
        );
        goto cleanup_and_exit;
    }

    real_subdir = realpath(subdir, NULL);
    if(!real_subdir) {
        bbox_perror(
            "bbox_is_subdir_of",
            "unable to normalize path %s: %s.\n",
            subdir, strerror(errno)
        );
        goto cleanup_and_exit;
    }

    size_t buf_len = strlen(real_path) + 2;

    if((buf = malloc(buf_len)) == NULL) {
        bbox_perror("bbox_is_subdir_of", "out of memory!\n");
        abort();
    }

    strncpy(buf, real_path, buf_len);

    buf[buf_len - 1] = '\0';
    buf[buf_len - 2] = '/';

    if(strncmp(buf, real_subdir, buf_len - 1) == 0)
        rval = 0;

cleanup_and_exit:

    free(real_path);
    free(real_subdir);
    free(buf);

    return rval;
}

int bbox_try_fix_pkg_cache_symlink(char *module) {
    int rval = 0;
    struct stat link_st;
    char *out_buf = NULL;
    size_t out_buf_len = 0;

    if(lstat("/.pkg-cache", &link_st) == -1) {
        return symlink("/var/cache/opkg", "/.pkg-cache");
    }

    size_t bufsize = link_st.st_size ? link_st.st_size + 1 : PATH_MAX;
    char *buf = malloc(bufsize);

    if(buf == NULL) {
        bbox_perror(module, "out of memory?\n");
        abort();
    }

    int nbytes = readlink("/.pkg-cache", buf, bufsize);

    if(nbytes < 0 || nbytes >= bufsize) {
        rval = -1;
        goto cleanup_and_exit;
    }

    buf[bufsize - 1] = '\0';

    char * const argv[] = {"mkdir", "-p", (char*const) buf, NULL};
    if(bbox_run_command_capture(getuid(), "mkdir", argv, &out_buf,
            &out_buf_len) != 0)
    {
        if(out_buf) {
            bbox_perror(
                module,
                "warning: failed to fix /.pkg-cache symlink: %s\n",
                out_buf
            );
        } else {
            bbox_perror(
                module, "warning: failed to fix /.pkg-cache symlink.\n"
            );
        }
    }

cleanup_and_exit:

    free(buf);
    free(out_buf);
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

success:
    if(n_ptr != NULL)
        *n_ptr = buf_size;
    return user_dir;

failure:
    free(user_dir);
    return NULL;
}

int validate_target_name(const char *module, const char *target_name)
{
    size_t len = strlen(target_name);

    if(len == 0)
        return -1;

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
