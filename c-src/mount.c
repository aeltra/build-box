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
#include <getopt.h>
#include <limits.h>
#include <mntent.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "bbox-do.h"

void bbox_mount_usage()
{
    printf(
        "                                                                         \n"
        "USAGE:                                                                   \n"
        "                                                                         \n"
        "  build-box mount [OPTIONS] <target-name>                                \n"
        "                                                                         \n"
        "OPTIONS:                                                                 \n"
        "                                                                         \n"
        "  -h, --help            Print this help message and exit immediately.    \n"
        "                                                                         \n"
        "  -m, --mount <fstype>  Mount 'dev', 'proc', 'sys' or 'home'. If this    \n"
        "                        option is not specified then the default is to   \n"
        "                        mount all of them.                               \n"
        "                                                                         \n"
    );
}

int bbox_mount_getopt(bbox_conf_t *conf, int argc, char * const argv[])
{
    int c;
    int option_index = 0;
    int do_mount_all = 1;

    static struct option long_options[] = {
        {"help",      no_argument,       0, 'h'},
        {"targets",   required_argument, 0, 't'},
        {"mount",     required_argument, 0, 'm'},
        { 0,          0,                 0,  0 }
    };

    bbox_config_clear_mount(conf);
    optind = 1;

    while(1) {
        c = getopt_long(argc, argv, ":ht:m:", long_options, &option_index);

        if(c == -1)
            break;

        switch(c) {
            case 'h':
                bbox_mount_usage();
                return -1;
            case 't':
                if(bbox_config_set_target_dir(conf, optarg) == -1)
                    return -2;
                break;
            case 'm':
                do_mount_all = 0;

                if(!strcmp(optarg, "dev")) {
                    bbox_config_set_mount_dev(conf);
                } else if(!strcmp(optarg, "proc")) {
                    bbox_config_set_mount_proc(conf);
                } else if(!strcmp(optarg, "sys")) {
                    bbox_config_set_mount_sys(conf);
                } else if(!strcmp(optarg, "home")) {
                    bbox_config_set_mount_home(conf);
                } else {
                    bbox_perror("mount", "unknown file system specifier "
                            "'%s'.\n", optarg);
                    return -2;
                }

                break;
            case '?':
            case ':':
                bbox_mount_usage();
                return -2;
            default:
                /* impossible, ignore */
                break;
        }
    }

    if(do_mount_all)
        bbox_config_set_mount_all(conf);

    if(argc - 1 > optind) {
        bbox_mount_usage();
        return -1;
    }

    return optind;
}

int bbox_mount_is_mounted(const char *path)
{
    struct mntent info;
    size_t buf_len = 4096;
    char *buf = NULL;
    char *mount_point = NULL;
    int rval = 0;
    FILE *fp = NULL;

    /*
     * Make sure we use the normalized path to compare against the entries in
     * /proc/mounts.
     */
    if(!(mount_point = realpath(path, NULL))) {
        bbox_perror("mount", "could not resolve '%s': '%s'.\n",
                path, strerror(errno));
        rval = -1;
        goto cleanup_and_exit;
    }

    if(!(fp = setmntent("/proc/mounts", "re"))) {
        bbox_perror("mount", "failed to open /proc/mounts.\n");
        rval = -1;
        goto cleanup_and_exit;
    }

    if(!(buf = malloc(buf_len))) {
        bbox_perror("mount", "out of memory?\n");
        rval = -1;
        goto cleanup_and_exit;
    }

    /*
     * Loop over the entries in /proc/mounts and compare against the given
     * directory.
     */
    while(1)
    {
        struct mntent *tmp_info = getmntent_r(fp, &info, buf, buf_len);

        if(!tmp_info) {
            if(errno == ERANGE) {
                buf_len *= 2;
                buf = realloc(buf, buf_len);
                if(!buf) {
                    bbox_perror("mount", "out of memory?\n");
                    rval = -1;
                    goto cleanup_and_exit;
                }
                continue;
            }

            break;
        }

        if(!strcmp(tmp_info->mnt_dir, mount_point)) {
            rval = 1;
            break;
        }
    }

cleanup_and_exit:

    if(fp)
        endmntent(fp);
    free(mount_point);
    free(buf);

    return rval;
}

int bbox_mount_special(const char *sys_root, const char *filesystemtype)
{
    char *mount_point = NULL;
    char *target = NULL;
    size_t buf_len = 0;
    char fd_path[64];
    int target_fd = -1;

    int is_mounted = 0;

    if(!strcmp(filesystemtype, "proc")) {
        mount_point = "proc";
    } else if (!strcmp(filesystemtype, "sysfs")) {
        mount_point = "sys";
    } else {
        bbox_perror("mount", "unsupported special filesystem: %s\n",
            filesystemtype);
        return -1;
    }

    bbox_path_join(&target, sys_root, mount_point, &buf_len);

    if((is_mounted = bbox_mount_is_mounted(target)) == -1) {
        free(target);
        return -1;
    }

    if(is_mounted) {
        free(target);
        return 0;
    }

    /*
     * Open the mountpoint directory and verify ownership via the fd. Using
     * the fd (through /proc/self/fd) for the mount operation eliminates the
     * TOCTOU race between the ownership check and the privileged mount call.
     */
    if((target_fd = bbox_open_dir_owned_by("mount", target, getuid())) == -1) {
        free(target);
        return -1;
    }

    snprintf(fd_path, sizeof(fd_path), "/proc/self/fd/%d", target_fd);

    /*
     * We need to be running mount as root, so we briefly raise privileges to
     * drop them again immediately after.
     */
    if(bbox_raise_privileges() == -1) {
        close(target_fd);
        free(target);
        return -1;
    }

    int rval = 0;

    if(mount(NULL, fd_path, filesystemtype, 0, NULL) != 0)
    {
        bbox_perror("mount", "failed to mount %s on %s: %s.\n",
                filesystemtype, target, strerror(errno));
        rval = -1;
    }
    else if(mount(NULL, target, NULL, MS_PRIVATE, NULL) != 0)
    {
        bbox_perror("mount", "failed to make mountpoint %s private: %s.\n",
                target, strerror(errno));
        /* Continue anyway. */
    }

    /*
     * We're done with mounting, lower privileges right away.
     */
    if(bbox_lower_privileges() == -1)
        rval = -1;

    close(target_fd);
    free(target);
    return rval;
}

int bbox_mount_bind(const char *sys_root, const char *source, int recursive,
        unsigned long remount_flags)
{
    char *target = NULL;
    size_t buf_len = 0;
    char fd_path[64];
    int target_fd = -1;
    int is_mounted = 0;

    bbox_path_join(&target, sys_root, source, &buf_len);

    if((is_mounted = bbox_mount_is_mounted(target)) == -1) {
        free(target);
        return -1;
    }

    if(is_mounted) {
        free(target);
        return 0;
    }

    /*
     * Open the mountpoint directory and verify ownership via the fd. Using
     * the fd (through /proc/self/fd) for the mount operation eliminates the
     * TOCTOU race between the ownership check and the privileged mount call.
     */
    if((target_fd = bbox_open_dir_owned_by("mount", target, getuid())) == -1) {
        free(target);
        return -1;
    }

    snprintf(fd_path, sizeof(fd_path), "/proc/self/fd/%d", target_fd);

    /*
     * We need to be running mount as root, so we briefly raise privileges to
     * drop them again immediately after.
     */
    if(bbox_raise_privileges() == -1) {
        close(target_fd);
        free(target);
        return -1;
    }

    int rval = 0;

    unsigned long mountflags = MS_BIND | (recursive ? MS_REC : 0);

    if(mount(source, fd_path, NULL, mountflags, NULL) != 0)
    {
        bbox_perror("mount", "failed to mount %s on %s: %s.\n",
                source, target, strerror(errno));
        rval = -1;
    }
    else if(mount(NULL, target, NULL, MS_PRIVATE, NULL) != 0)
    {
        bbox_perror("mount", "failed to make mountpoint %s private: %s.\n",
                target, strerror(errno));
        /* Continue anyway. */
    }

    /*
     * If additional mount flags were requested, apply them via a remount.
     * Bind mounts inherit the source mount's flags, so a remount is the
     * only way to add restrictions like MS_NOSUID or MS_NOEXEC.
     */
    if(rval == 0 && remount_flags) {
        if(mount(NULL, target, NULL,
                    MS_BIND | MS_REMOUNT | remount_flags, NULL) != 0)
        {
            bbox_perror("mount",
                    "failed to remount %s with restricted flags: %s.\n",
                    target, strerror(errno));
            /* Continue anyway. */
        }
    }

    /*
     * We're done with mounting, lower privileges right away.
     */
    if(bbox_lower_privileges() == -1)
        rval = -1;

    close(target_fd);
    free(target);
    return rval;
}

int bbox_mount_any(const bbox_conf_t *conf, const char *sys_root)
{
    /*
     * As an additional precaution, we require the normalized sys-root directory
     * to be owned by the user who invoked `build-box`.
     */
    if(bbox_isdir_and_owned_by("mount", sys_root, getuid()) == -1)
        return -1;

    if(bbox_config_get_mount_dev(conf)) {
        if(bbox_mount_bind(sys_root, "/dev", 0, MS_NOSUID | MS_NOEXEC) < 0)
            return -1;
    }

    if(bbox_config_get_mount_proc(conf)) {
        if(bbox_mount_special(sys_root, "proc") < 0)
            return -1;
    }

    if(bbox_config_get_mount_sys(conf)) {
        if(bbox_mount_special(sys_root, "sysfs") < 0)
            return -1;
    }

    /*
     * Mounting the home directory requires extra pre-caution. The source path
     * has already been normalized and checked for ownership, so we should be
     * fine calling `bbox_mount_bind`, which in turn checks the target directory
     * before executing the mount.
     */
    if(bbox_config_get_mount_home(conf)) {
        const char *homedir = bbox_config_get_home_dir(conf);

        /*
         * We're not worried about this, because we are currently running with
         * lowered privileges.
         */
        if(bbox_sysroot_mkdir_p("mount", sys_root, homedir) == -1)
            return -1;

        /*
         * This internally checks the ownership of <sys_root>/<homedir>.
         */
        if(bbox_mount_bind(sys_root, homedir, 0, MS_NOSUID | MS_NODEV) < 0)
            return -1;
    }

    return 0;
}

int bbox_mount(int argc, char * const argv[])
{
    char *buf = NULL;
    size_t buf_len = 0;

    int rval = BBOX_ERR_INVOCATION;

    bbox_conf_t *conf = bbox_config_new();
    if(!conf) {
        bbox_perror("mount", "creating configuration context failed.\n");
        return BBOX_ERR_RUNTIME;
    }

    int non_optind;

    if((non_optind = bbox_mount_getopt(conf, argc, argv)) < 0) {
        /* user asked for --help */
        if(non_optind == -1)
            rval = 0;
        goto cleanup_and_exit;
    }

    if(non_optind >= argc) {
        bbox_perror("mount", "no target specified.\n");
        goto cleanup_and_exit;
    }

    char *target = argv[non_optind];

    if(validate_target_name("mount", target) == -1)
        goto cleanup_and_exit;

    bbox_path_join(
        &buf, bbox_config_get_target_dir(conf), target, &buf_len
    );

    struct stat st;

    if(lstat(buf, &st) == -1) {
        bbox_perror("mount", "target '%s' not found.\n", target);
        goto cleanup_and_exit;
    }

    rval = BBOX_ERR_RUNTIME;

    if(bbox_mount_any(conf, buf) == 0)
        rval = 0;

cleanup_and_exit:

    bbox_config_free(conf);
    free(buf);
    return rval;
}
