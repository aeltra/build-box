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
#include <getopt.h>
#include <limits.h>
#include <mntent.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "bbox-do.h"

void bbox_umount_usage()
{
    printf(
        "                                                                          \n"
        "USAGE:                                                                    \n"
        "                                                                          \n"
        "  build-box umount [OPTIONS] <target-name>                                \n"
        "                                                                          \n"
        "OPTIONS:                                                                  \n"
        "                                                                          \n"
        "  -h, --help             Print this help message and exit immediately.    \n"
        "                                                                          \n"
        "  -m, --umount <fstype>  Unmount 'dev', 'proc', 'sys' or 'home'. If this  \n"
        "                         option is not specified, then the default is to  \n"
        "                         unmount all of them.                             \n"
        "                                                                          \n"
    );
}

int bbox_umount_getopt(bbox_conf_t *conf, int argc, char * const argv[])
{
    int c;
    int option_index = 0;
    int do_umount_all = 1;

    static struct option long_options[] = {
        {"help",     no_argument,       0, 'h'},
        {"targets",  required_argument, 0, 't'},
        {"umount",   required_argument, 0, 'm'},
        { 0,         0,                 0,  0 }
    };

    bbox_config_set_mount_all(conf);
    optind = 1;

    while(1) {
        c = getopt_long(argc, argv, ":ht:m:", long_options, &option_index);

        if(c == -1)
            break;

        switch(c) {
            case 'h':
                bbox_umount_usage();
                return -1;
            case 't':
                if(bbox_config_set_target_dir(conf, optarg) < 0)
                    return -2;
                break;
            case 'm':
                do_umount_all = 0;

                if(!strcmp(optarg, "dev")) {
                    bbox_config_unset_mount_dev(conf);
                } else if(!strcmp(optarg, "proc")) {
                    bbox_config_unset_mount_proc(conf);
                } else if(!strcmp(optarg, "sys")) {
                    bbox_config_unset_mount_sys(conf);
                } else if(!strcmp(optarg, "home")) {
                    bbox_config_unset_mount_home(conf);
                } else {
                    bbox_perror("umount", "unknown file system specifier "
                            "'%s'.\n", optarg);
                    return -2;
                }

                break;
            case '?':
            case ':':
                bbox_umount_usage();
                return -2;
            default:
                /* impossible, ignore */
                break;
        }
    }

    if(do_umount_all)
        bbox_config_clear_mount(conf);

    if(argc - 1 > optind) {
        bbox_umount_usage();
        return -1;
    }

    return optind;
}

int bbox_umount_unbind(const char *sys_root, const char *parent_relpath,
        const char *name)
{
    char *parent = NULL;
    size_t parent_len = 0;
    char fd_path[64 + NAME_MAX];
    struct stat st;
    int lock_fd = -1;
    int parent_fd = -1;
    int is_mounted = 0;
    int rval = -1;

    if(bbox_validate_entry_name("umount", name) == -1)
        return -1;

    /* Serialize with other invocations on this sysroot, see bbox_lock_dir. */
    if((lock_fd = bbox_lock_dir("umount", sys_root)) == -1)
        return -1;

    bbox_path_join(&parent, sys_root, parent_relpath, &parent_len);

    /* If the parent directory does not exist, there is nothing to unmount. */
    if(lstat(parent, &st) == -1 && errno == ENOENT) {
        rval = 0;
        goto cleanup_and_exit;
    }

    /*
     * The parent is verified through a file descriptor and the mount point
     * is looked up relative to it, so that a symlink swapped in by the user
     * between check and unmount cannot redirect us to a mount elsewhere on
     * the system.
     */
    if((parent_fd = bbox_open_dir_owned_by("umount", parent, getuid())) == -1)
        goto cleanup_and_exit;

    if(fstatat(parent_fd, name, &st, AT_SYMLINK_NOFOLLOW) == -1) {
        if(errno == ENOENT) {
            rval = 0;
            goto cleanup_and_exit;
        }
        bbox_perror("umount", "could not stat '%s/%s': %s.\n", parent, name,
                strerror(errno));
        goto cleanup_and_exit;
    }

    if(S_ISLNK(st.st_mode) || !S_ISDIR(st.st_mode)) {
        bbox_perror("umount", "%s/%s is not a directory.\n", parent, name);
        goto cleanup_and_exit;
    }

    if((is_mounted = bbox_is_mount_point_at("umount", parent_fd, name)) == -1)
        goto cleanup_and_exit;

    if(!is_mounted) {
        rval = 0;
        goto cleanup_and_exit;
    }

    /*
     * Go through the descriptor of the verified parent. A mount point cannot
     * be renamed and UMOUNT_NOFOLLOW refuses symlinks, so the single path
     * component behind it can only ever resolve to a mount directly below
     * the parent.
     */
    if(snprintf(fd_path, sizeof(fd_path), "/proc/self/fd/%d/%s", parent_fd,
            name) >= (int) sizeof(fd_path))
    {
        bbox_perror("umount", "mount point name '%s' is too long.\n", name);
        goto cleanup_and_exit;
    }

    if(bbox_raise_privileges() == -1)
        goto cleanup_and_exit;

    rval = 0;

    if(umount2(fd_path, UMOUNT_NOFOLLOW) != 0) {
        bbox_perror("umount", "failed to unmount %s/%s: %s\n", parent, name,
                strerror(errno));
        rval = -1;
    }

    if(bbox_lower_privileges() == -1)
        rval = -1;

cleanup_and_exit:

    if(parent_fd != -1)
        close(parent_fd);
    close(lock_fd);
    free(parent);
    return rval;
}

int bbox_umount_any(const bbox_conf_t *conf, const char *sys_root)
{
    uid_t uid = getuid();

    /*
     * As an additional precaution, we require the normalized sys-root
     * directory to be owned by the user who invoked `build-box`.
     */
    if(bbox_isdir_and_owned_by("umount", sys_root, uid) == -1)
        return -1;

    if(!bbox_config_get_mount_dev(conf)) {
        if(bbox_umount_unbind(sys_root, "", "dev") < 0)
            return -1;
    }
    if(!bbox_config_get_mount_proc(conf)) {
        if(bbox_umount_unbind(sys_root, "", "proc") < 0)
            return -1;
    }
    if(!bbox_config_get_mount_sys(conf)) {
        if(bbox_umount_unbind(sys_root, "", "sys") < 0)
            return -1;
    }

    /*
     * The real home is bind-mounted at {chroot_home}/RealHome inside the
     * sysroot. `bbox_umount_unbind` verifies that {chroot_home} belongs to
     * the user who executed build box before unmounting anything below it.
     */
    if(!bbox_config_get_mount_home(conf)) {
        const char *chroot_home = bbox_config_get_chroot_home_dir(conf);

        if(bbox_umount_unbind(sys_root, chroot_home, "RealHome") < 0)
            return -1;
    }

    return 0;
}

int bbox_umount(int argc, char * const argv[])
{
    char *buf = NULL;
    size_t buf_len = 0;

    int rval = BBOX_ERR_INVOCATION;

    bbox_conf_t *conf = bbox_config_new();
    if(!conf) {
        bbox_perror("umount", "creating configuration context failed.\n");
        return BBOX_ERR_RUNTIME;
    }

    int non_optind;

    if((non_optind = bbox_umount_getopt(conf, argc, argv)) < 0) {
        /* user asked for --help */
        if(non_optind == -1)
            rval = 0;
        goto cleanup_and_exit;
    }

    if(non_optind >= argc) {
        bbox_perror("umount", "no target specified.\n");
        goto cleanup_and_exit;
    }

    char *target = argv[non_optind];

    if(validate_target_name("umount", target) == -1)
        goto cleanup_and_exit;

    bbox_path_join(
        &buf, bbox_config_get_target_dir(conf), target, &buf_len
    );

    struct stat st;

    if(lstat(buf, &st) == -1) {
        bbox_perror("umount", "target '%s' not found.\n", target);
        goto cleanup_and_exit;
    }

    rval = BBOX_ERR_RUNTIME;

    if(bbox_umount_any(conf, buf) == 0)
        rval = 0;

cleanup_and_exit:

    bbox_config_free(conf);
    free(buf);
    return rval;
}
