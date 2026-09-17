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
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
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

/*
 * Open the mount point <sys_root>/<parent_relpath>/<name> for mounting.
 *
 * The parent directory is verified through a file descriptor and the mount
 * point is looked up relative to it, so that a symlink swapped in by the
 * user between check and mount cannot redirect us elsewhere. Both the
 * parent and the mount point must be owned by the invoking user.
 *
 * Returns 1 if something is already mounted there, 0 if the descriptors
 * were filled in and the mount can proceed, -1 on error.
 */
static int bbox_mount_open_target(const char *sys_root,
        const char *parent_relpath, const char *name, int *parent_fd_ptr,
        int *dir_fd_ptr, char **target_ptr)
{
    char *parent = NULL;
    size_t parent_len = 0;
    size_t target_len = 0;
    struct stat st;
    int parent_fd = -1;
    int dir_fd = -1;
    int is_mounted = 0;
    int rval = -1;

    uid_t uid = getuid();

    if(bbox_validate_entry_name("mount", name) == -1)
        return -1;

    bbox_path_join(&parent, sys_root, parent_relpath, &parent_len);
    bbox_path_join(target_ptr, parent, name, &target_len);

    if((parent_fd = bbox_open_dir_owned_by("mount", parent, uid)) == -1)
        goto cleanup_and_exit;

    if((is_mounted = bbox_is_mount_point_at("mount", parent_fd, name)) == -1)
        goto cleanup_and_exit;

    if(is_mounted) {
        rval = 1;
        goto cleanup_and_exit;
    }

    dir_fd = openat(parent_fd, name, O_PATH | O_DIRECTORY | O_NOFOLLOW);

    if(dir_fd == -1) {
        bbox_perror("mount", "could not open '%s': %s.\n", *target_ptr,
                strerror(errno));
        goto cleanup_and_exit;
    }

    if(fstat(dir_fd, &st) == -1) {
        bbox_perror("mount", "could not stat '%s': %s.\n", *target_ptr,
                strerror(errno));
        goto cleanup_and_exit;
    }

    if(st.st_uid != uid) {
        bbox_perror("mount", "directory '%s' is not owned by user id '%ld'.\n",
                *target_ptr, (long) uid);
        goto cleanup_and_exit;
    }

    *parent_fd_ptr = parent_fd;
    *dir_fd_ptr = dir_fd;
    parent_fd = -1;
    dir_fd = -1;
    rval = 0;

cleanup_and_exit:

    if(dir_fd != -1)
        close(dir_fd);
    if(parent_fd != -1)
        close(parent_fd);
    free(parent);
    return rval;
}

/*
 * The per-mount flags currently in effect on the mount an open descriptor
 * refers to, in the form mount(2) takes them.
 *
 * A bind remount replaces the whole flag set with what it is given. To add
 * a restriction without dropping the ones the source already had -- "ro" or
 * "nodev" on a home partition, say -- the current flags have to be carried
 * over. Inside a user namespace the kernel insists on it.
 */
static int bbox_mount_current_flags(const char *target, int fd,
        unsigned long *flags_ptr)
{
    struct statvfs vfs;
    unsigned long flags = 0;

    if(fstatvfs(fd, &vfs) == -1) {
        bbox_perror("mount", "could not read mount flags of '%s': %s.\n",
                target, strerror(errno));
        return -1;
    }

    if(vfs.f_flag & ST_RDONLY)
        flags |= MS_RDONLY;
    if(vfs.f_flag & ST_NOSUID)
        flags |= MS_NOSUID;
    if(vfs.f_flag & ST_NODEV)
        flags |= MS_NODEV;
    if(vfs.f_flag & ST_NOEXEC)
        flags |= MS_NOEXEC;
    if(vfs.f_flag & ST_SYNCHRONOUS)
        flags |= MS_SYNCHRONOUS;
    if(vfs.f_flag & ST_MANDLOCK)
        flags |= MS_MANDLOCK;
    if(vfs.f_flag & ST_NODIRATIME)
        flags |= MS_NODIRATIME;
#ifdef ST_NOSYMFOLLOW
    if(vfs.f_flag & ST_NOSYMFOLLOW)
        flags |= MS_NOSYMFOLLOW;
#endif

    /*
     * Without an atime flag mount(2) defaults to relatime, so strict atime
     * has to be asked for explicitly to be preserved.
     */
    if(vfs.f_flag & ST_NOATIME)
        flags |= MS_NOATIME;
    else if(vfs.f_flag & ST_RELATIME)
        flags |= MS_RELATIME;
    else
        flags |= MS_STRICTATIME;

    *flags_ptr = flags;
    return 0;
}

/*
 * Apply propagation and mount flags to the mount that was just created on
 * <name> below the parent. Must be called with privileges raised.
 *
 * The new mount is re-opened relative to the verified parent and addressed
 * through that descriptor, so the follow-up mount calls act on exactly the
 * mount we made and nothing else. Bind mounts inherit the source mount's
 * flags, so a remount is the only way to add restrictions like MS_NOSUID,
 * and the inherited flags are kept. If that remount fails, the mount is
 * taken down again rather than left in place without the restrictions that
 * were asked for.
 */
static int bbox_mount_finish(const char *target, int parent_fd,
        const char *name, int dir_fd, unsigned long remount_flags)
{
    char mnt_path[64];
    char undo_path[64 + NAME_MAX];
    long dir_mount_id = -1;
    long mnt_mount_id = -1;
    int mnt_fd = -1;
    int rval = -1;

    mnt_fd = openat(parent_fd, name, O_PATH | O_DIRECTORY | O_NOFOLLOW);

    if(mnt_fd == -1) {
        bbox_perror("mount", "could not re-open '%s' after mounting: %s.\n",
                target, strerror(errno));
        return -1;
    }

    if((dir_mount_id = bbox_fd_mount_id("mount", dir_fd)) == -1)
        goto cleanup_and_exit;
    if((mnt_mount_id = bbox_fd_mount_id("mount", mnt_fd)) == -1)
        goto cleanup_and_exit;

    if(dir_mount_id == mnt_mount_id) {
        bbox_perror("mount", "no mount appeared on '%s'.\n", target);
        goto cleanup_and_exit;
    }

    snprintf(mnt_path, sizeof(mnt_path), "/proc/self/fd/%d", mnt_fd);

    if(mount(NULL, mnt_path, NULL, MS_PRIVATE, NULL) != 0) {
        bbox_perror("mount", "failed to make mountpoint %s private: %s.\n",
                target, strerror(errno));
        /* Continue anyway. */
    }

    if(remount_flags) {
        unsigned long current_flags = 0;
        int failed = 0;

        if(bbox_mount_current_flags(target, mnt_fd, &current_flags) == -1) {
            failed = 1;
        }
        else if(mount(NULL, mnt_path, NULL,
                    MS_BIND | MS_REMOUNT | current_flags | remount_flags,
                    NULL) != 0)
        {
            bbox_perror("mount",
                    "failed to remount %s with restricted flags: %s.\n",
                    target, strerror(errno));
            failed = 1;
        }

        if(failed) {
            /*
             * Undo the mount. The descriptor to it must be closed first, or
             * the unmount fails with EBUSY. The parent descriptor remains
             * open, so the single name below it is still unambiguous.
             */
            close(mnt_fd);
            mnt_fd = -1;

            if(snprintf(undo_path, sizeof(undo_path), "/proc/self/fd/%d/%s",
                    parent_fd, name) < (int) sizeof(undo_path))
            {
                (void) umount2(undo_path, UMOUNT_NOFOLLOW);
            }

            goto cleanup_and_exit;
        }
    }

    rval = 0;

cleanup_and_exit:

    if(mnt_fd != -1)
        close(mnt_fd);
    return rval;
}

int bbox_mount_special(const char *sys_root, const char *parent_relpath,
        const char *name, const char *filesystemtype)
{
    char *target = NULL;
    char fd_path[64];
    int lock_fd = -1;
    int parent_fd = -1;
    int dir_fd = -1;
    int rval = -1;

    if(strcmp(filesystemtype, "proc") != 0 &&
            strcmp(filesystemtype, "sysfs") != 0)
    {
        bbox_perror("mount", "unsupported special filesystem: %s\n",
            filesystemtype);
        return -1;
    }

    /* Serialize with other invocations on this sysroot, see bbox_lock_dir. */
    if((lock_fd = bbox_lock_dir("mount", sys_root)) == -1)
        return -1;

    switch(bbox_mount_open_target(sys_root, parent_relpath, name, &parent_fd,
                &dir_fd, &target))
    {
        case 1:
            rval = 0;
            goto cleanup_and_exit;
        case -1:
            goto cleanup_and_exit;
        default:
            break;
    }

    snprintf(fd_path, sizeof(fd_path), "/proc/self/fd/%d", dir_fd);

    /*
     * We need to be running mount as root, so we briefly raise privileges to
     * drop them again immediately after.
     */
    if(bbox_raise_privileges() == -1)
        goto cleanup_and_exit;

    if(mount(NULL, fd_path, filesystemtype,
                MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) != 0)
    {
        bbox_perror("mount", "failed to mount %s on %s: %s.\n",
                filesystemtype, target, strerror(errno));
    }
    else if(bbox_mount_finish(target, parent_fd, name, dir_fd, 0) == 0)
    {
        rval = 0;
    }

    /*
     * We're done with mounting, lower privileges right away.
     */
    if(bbox_lower_privileges() == -1)
        rval = -1;

cleanup_and_exit:

    if(dir_fd != -1)
        close(dir_fd);
    if(parent_fd != -1)
        close(parent_fd);
    close(lock_fd);
    free(target);
    return rval;
}

int bbox_mount_bind(const char *sys_root, const char *source,
        const char *parent_relpath, const char *name, int recursive,
        unsigned long remount_flags)
{
    char *target = NULL;
    char fd_path[64];
    char source_path[64];
    int lock_fd = -1;
    int parent_fd = -1;
    int dir_fd = -1;
    int source_fd = -1;
    int rval = -1;

    /* Serialize with other invocations on this sysroot, see bbox_lock_dir. */
    if((lock_fd = bbox_lock_dir("mount", sys_root)) == -1)
        return -1;

    switch(bbox_mount_open_target(sys_root, parent_relpath, name, &parent_fd,
                &dir_fd, &target))
    {
        case 1:
            rval = 0;
            goto cleanup_and_exit;
        case -1:
            goto cleanup_and_exit;
        default:
            break;
    }

    /*
     * Resolve the source while privileges are still lowered and hand root
     * the descriptor, so that root never walks a path on the user's behalf.
     */
    if((source_fd = open(source, O_PATH | O_DIRECTORY)) == -1) {
        bbox_perror("mount", "could not open '%s': %s.\n", source,
                strerror(errno));
        goto cleanup_and_exit;
    }

    snprintf(fd_path, sizeof(fd_path), "/proc/self/fd/%d", dir_fd);
    snprintf(source_path, sizeof(source_path), "/proc/self/fd/%d", source_fd);

    /*
     * We need to be running mount as root, so we briefly raise privileges to
     * drop them again immediately after.
     */
    if(bbox_raise_privileges() == -1)
        goto cleanup_and_exit;

    unsigned long mountflags = MS_BIND | (recursive ? MS_REC : 0);

    if(mount(source_path, fd_path, NULL, mountflags, NULL) != 0)
    {
        bbox_perror("mount", "failed to mount %s on %s: %s.\n",
                source, target, strerror(errno));
    }
    else if(bbox_mount_finish(target, parent_fd, name, dir_fd,
                remount_flags) == 0)
    {
        rval = 0;
    }

    /*
     * We're done with mounting, lower privileges right away.
     */
    if(bbox_lower_privileges() == -1)
        rval = -1;

cleanup_and_exit:

    if(source_fd != -1)
        close(source_fd);
    if(dir_fd != -1)
        close(dir_fd);
    if(parent_fd != -1)
        close(parent_fd);
    close(lock_fd);
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
        if(bbox_mount_bind(sys_root, "/dev", "", "dev", 0,
                    MS_NOSUID | MS_NOEXEC) < 0)
            return -1;
    }

    if(bbox_config_get_mount_proc(conf)) {
        if(bbox_mount_special(sys_root, "", "proc", "proc") < 0)
            return -1;
    }

    if(bbox_config_get_mount_sys(conf)) {
        if(bbox_mount_special(sys_root, "", "sys", "sysfs") < 0)
            return -1;
    }

    /*
     * Each target gets its own, isolated home directory at
     * {sysroot}/home/{username}. The user's real home is bind-mounted onto
     * {sysroot}/home/{username}/RealHome so that the user's source files
     * remain accessible while dotfiles are kept per-target.
     *
     * The source path has been normalized and checked for ownership when the
     * configuration was created. `bbox_mount_bind` checks the parent and the
     * mount point before executing the mount.
     */
    if(bbox_config_get_mount_home(conf)) {
        const char *homedir = bbox_config_get_home_dir(conf);
        const char *chroot_home = bbox_config_get_chroot_home_dir(conf);

        char *realhome_relpath = NULL;
        char *home_path = NULL;
        size_t rh_buf_len = 0;
        size_t hp_buf_len = 0;
        struct stat st;
        int home_existed = 0;

        bbox_path_join(&realhome_relpath, chroot_home, "RealHome",
                &rh_buf_len);
        bbox_path_join(&home_path, sys_root, chroot_home, &hp_buf_len);

        /*
         * Create the per-target home and the RealHome mount point inside the
         * sysroot. We're not worried about this, because we are currently
         * running with lowered privileges.
         *
         * A per-target home that does not exist yet -- the target predates
         * them -- is created with the mode a real home gets, so that the
         * dotfiles and history kept there are the user's alone. An existing
         * one keeps whatever mode it has.
         */
        home_existed = lstat(home_path, &st) == 0;

        if(bbox_sysroot_mkdir_p("mount", sys_root, chroot_home) == -1)
            goto home_failure;

        if(!home_existed && chmod(home_path, 0700) == -1) {
            bbox_perror("mount", "could not set the mode of '%s': %s.\n",
                    home_path, strerror(errno));
            goto home_failure;
        }

        if(bbox_sysroot_mkdir_p("mount", sys_root, realhome_relpath) == -1)
            goto home_failure;

        free(realhome_relpath);
        free(home_path);

        if(bbox_mount_bind(sys_root, homedir, chroot_home, "RealHome", 0,
                    MS_NOSUID | MS_NODEV) < 0)
            return -1;

        return 0;

    home_failure:
        free(realhome_relpath);
        free(home_path);
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
