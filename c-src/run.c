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
#include <sched.h>
#include <signal.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "bbox-do.h"

static volatile pid_t pid_one = 0;

void signal_handler(int sig)
{
    if(!pid_one)
        return;
    kill(pid_one, SIGKILL);
}

void bbox_run_usage()
{
    printf(
        "                                                                         \n"
        "USAGE:                                                                   \n"
        "                                                                         \n"
        "  build-box run [OPTIONS] <target-name> [--] <command>                   \n"
        "                                                                         \n"
        "Options go before the target name. Everything after it is the command   \n"
        "and is passed on as it is, so the command may have options of its own.  \n"
        "                                                                         \n"
        "OPTIONS:                                                                 \n"
        "                                                                         \n"
        "  -h, --help            Print this help message and exit immediately.    \n"
        "                                                                         \n"
        "  -m, --mount <fstype>  Mount 'dev', 'proc', 'sys' or 'home'. If this    \n"
        "                        option is not specified then the default is to   \n"
        "                        mount all of them.                               \n"
        "                                                                         \n"
        "  --no-file-copy        Don't copy passwd database, group database and   \n"
        "                        resolv.conf from host.                           \n"
        "                                                                         \n"
        "  --no-mount            Don't mount any filesystems per default.         \n"
        "                                                                         \n"
        "  --isolate             Run in a separate PID and mount namespace.       \n"
        "                                                                         \n"
    );
}

int bbox_run_getopt(bbox_conf_t *conf, int argc, char * const argv[])
{
    int c;
    int option_index = 0;
    int do_mount_all = 1;

    static struct option long_options[] = {
        {"help",         no_argument,       0, 'h'},
        {"targets",      required_argument, 0, 't'},
        {"mount",        required_argument, 0, 'm'},
        {"no-file-copy", no_argument,       0, '1'},
        {"no-mount",     no_argument,       0, '2'},
        {"isolate",      no_argument,       0, '3'},
        { 0,             0,                 0,  0 }
    };

    bbox_config_clear_mount(conf);
    bbox_config_enable_file_updates(conf);
    bbox_getopt_begin();

    /* A dispatcher: the parse stops at the target, the rest is the command. */
    while(1) {
        c = getopt_long(argc, argv, BBOX_OPTS_DISPATCH(":ht:m:"), long_options,
                &option_index);

        if(c == -1)
            break;

        switch(c) {
            case 'h':
                bbox_run_usage();
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
            case '1':
                bbox_config_disable_file_updates(conf);
                break;
            case '2':
                do_mount_all = 0;
                break;
            case '3':
                bbox_config_set_isolation(conf);
                break;
            case '?':
            case ':':
                bbox_run_usage();
                return -2;
            default:
                /* impossible, ignore */
                break;
        }
    }

    if(do_mount_all)
        bbox_config_set_mount_all(conf);

    return optind;
}

/*
 * Mount the session's own proc on the sysroot's proc directory. The
 * sysroot is the working directory and has been verified to be the
 * caller's. The entry is opened relative to it and must not be a symlink:
 * the tree is the user's, and root must not walk a path through it.
 *
 * Whatever is mounted on the entry already -- the proc of a plain session
 * -- is stacked on, and that is required anyway: a proc instance shows the
 * PID namespace of the process that mounted it, so the existing one shows
 * the host's and only a mount made by the child shows the session's. The
 * mount lives in the private namespace unshared above and vanishes with
 * the session.
 */
static int bbox_mount_session_proc()
{
    char fd_path[64];
    int fd = -1;
    int rval = -1;

    if((fd = open("proc", O_PATH | O_DIRECTORY | O_NOFOLLOW)) == -1) {
        bbox_perror("bbox_runas_user_chrooted",
                "could not open the sysroot's proc directory: %s\n",
                strerror(errno));
        return -1;
    }

    snprintf(fd_path, sizeof(fd_path), "/proc/self/fd/%d", fd);

    if(mount(NULL, fd_path, "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC,
                NULL) != 0)
    {
        bbox_perror("bbox_runas_user_chrooted",
                "failed to mount /proc inside namespace: %s\n",
                strerror(errno));
    } else {
        rval = 0;
    }

    close(fd);
    return rval;
}

int bbox_runas_user_chrooted(const char *sys_root, int argc,
        char * const argv[], const bbox_conf_t *conf)
{
    static char *shells[] = {"/tools/bin/sh", "/usr/bin/sh", NULL};

    char *sh = NULL;
    struct stat st;
    pid_t pid = 0;

    if(argc == 0) {
        bbox_perror("bbox_runas_user_chrooted",
                "missing arguments, nothing to run.\n");
        return BBOX_ERR_INVOCATION;
    }

    /* change into system folder. */
    if(chdir(sys_root) == -1) {
        bbox_perror("bbox_runas_user_chrooted",
                "could not chdir to '%s': %s.\n",
                sys_root, strerror(errno));
        return BBOX_ERR_RUNTIME;
    }

    /* do a few sanity checks before chrooting. */
    if(lstat(".", &st) == -1) {
        bbox_perror("bbox_runas_user_chrooted", "failed to stat '%s': %s.\n",
                sys_root, strerror(errno));
        return BBOX_ERR_RUNTIME;
    }
    if(st.st_uid != getuid()) {
        bbox_perror("bbox_runas_user_chrooted",
                "chroot is not owned by user.\n");
        return BBOX_ERR_RUNTIME;
    }

    if(bbox_raise_privileges() == -1)
        return BBOX_ERR_RUNTIME;

    if(bbox_reset_supplementary_groups() == -1) {
        bbox_lower_privileges();
        return BBOX_ERR_RUNTIME;
    }

    /*
     * If isolation is requested, set up the namespaces while we still have
     * root privileges and before the chroot, because making the mounts
     * private needs the root of a mount and after the chroot "/" is the
     * sysroot directory, which is not one. The child's proc mount happens
     * before the chroot as well: it goes through /proc/self/fd, which has
     * to be the host's proc.
     */
    if(bbox_config_get_isolation(conf)) {
        /*
         * Moving the process into its own PID namespace means, that this
         * process group cannot interfere with other processes running under
         * the same account.
         *
         * Putting it into its own mount namespace so that it gets a limited
         * view of the proc filesystem (mounted below).
         */
        if(unshare(CLONE_NEWPID | CLONE_NEWNS) == -1) {
            bbox_perror("bbox_runas_user_chrooted",
                    "failed to isolate process: %s\n", strerror(errno));
            bbox_lower_privileges();
            return BBOX_ERR_RUNTIME;
        }

        /*
         * unshare(2) leaves the copied mounts in the host's peer groups, so
         * a mount made in here would propagate back out. The unshare(1)
         * command makes everything private at this point; the syscall does
         * not.
         */
        if(mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == -1) {
            bbox_perror("bbox_runas_user_chrooted",
                    "failed to make mounts private: %s\n", strerror(errno));
            bbox_lower_privileges();
            return BBOX_ERR_RUNTIME;
        }

        /* Note that we only fork and wait when isolation is requested. */
        if((pid = fork()) == -1) {
            bbox_perror("bbox_runas_user_chrooted", "fork failed: %s\n",
                    strerror(errno));
            bbox_lower_privileges();
            return BBOX_ERR_RUNTIME;
        }

        /*
         * The child is the first process in the new PID namespace, so only
         * a proc it mounts itself shows that namespace.
         */
        if(pid == 0 && bbox_config_get_mount_proc(conf)) {
            if(bbox_mount_session_proc() == -1)
                _exit(BBOX_ERR_RUNTIME);
        }
    }

    /* now do actual chroot call. */
    if(chroot(".") == -1) {
        bbox_perror("bbox_runas_user_chrooted",
                "chroot to system root failed: %s.\n",
                strerror(errno));
        bbox_lower_privileges();
        if(pid > 0) {
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
            return BBOX_ERR_RUNTIME;
        }
        if(bbox_config_get_isolation(conf))
            _exit(BBOX_ERR_RUNTIME);
        return BBOX_ERR_RUNTIME;
    }

    /*
     * Permanently drop all privileges. After this point, root privileges
     * cannot be recovered by any code path, and with no_new_privs set they
     * cannot be regained through a setuid binary inside the chroot either.
     */
    if(bbox_drop_privileges() == -1 || bbox_no_new_privs() == -1) {
        bbox_perror("bbox_runas_user_chrooted",
                "failed to permanently drop privileges.\n");
        if(pid > 0) {
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
            return BBOX_ERR_RUNTIME;
        }
        _exit(BBOX_ERR_RUNTIME);
    }

    /*
     * Parent process in isolated mode: wait for the child and exit.
     */
    if(pid > 0) {
        pid_one = pid;

        int signals_to_handle[] = {SIGTERM, SIGINT, SIGHUP, 0};

        for(int i = 0; signals_to_handle[i] != 0; i++) {
            signal(signals_to_handle[i], signal_handler);
        }

        int wstatus = 0;

        /* If we were interrupted, we try again. */
        while(waitpid(pid, &wstatus, 0) == -1) {
            /*
             * This is really the only error that can occur. ECHILD cannot,
             * because we forked the child ourselves. And EINVAL because we
             * don't pass any flags to waitpid.
             */
            if(errno != EINTR)
                break;
        }

        /* Pass through the exit status, if that is possible. */
        if(WIFEXITED(wstatus))
            return WEXITSTATUS(wstatus);

        return BBOX_ERR_RUNTIME;
    }

    /*
     * Execution path: main process (non-isolated) or child (isolated).
     * All code below runs permanently unprivileged.
     */

    /* Do this while we're at the fs root. */
    bbox_try_fix_pkg_cache_symlink("run",
            bbox_config_get_chroot_home_dir(conf));

    /* this is non-critical. */
    char *home_dir = bbox_config_get_chroot_home_dir(conf);
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
        bbox_perror("bbox_runas_user_chrooted", "could not find a shell.\n");
        return BBOX_ERR_RUNTIME;
    }

    /*
     * Single argument: pass directly to sh -c, allowing shell syntax
     * (pipes, redirections, etc.) to be interpreted by the shell.
     *
     * Multiple arguments: use "$@" to preserve argument boundaries.
     * Without this, arguments containing spaces would be split incorrectly
     * when the shell re-parses the command string.
     */
    if(argc == 1) {
        char *command[] = {"sh", "-l", "-c", "--", argv[0], NULL};
        execvp(sh, command);
    } else {
        char **command = malloc(sizeof(char*) * (argc + 7));

        if(!command) {
            bbox_perror("bbox_runas_user_chrooted", "out of memory?\n");
            _exit(BBOX_ERR_RUNTIME);
        }

        command[0] = "sh";
        command[1] = "-l";
        command[2] = "-c";
        command[3] = "--";
        command[4] = "\"$@\"";
        command[5] = "sh";

        for(int i = 0; i < argc; i++)
            command[6 + i] = argv[i];
        command[6 + argc] = NULL;

        execvp(sh, command);
        free(command);
    }

    bbox_perror("bbox_runas_user_chrooted", "failed to invoke shell: %s\n",
            strerror(errno));
    _exit(BBOX_ERR_RUNTIME);
}

/*
 * Where the command starts, given the index just past the target. The
 * usage text has long put a "--" there, and since the parser stops at
 * the target that "--" now reaches us instead of getopt. It is ours, not
 * the shell's, so one is dropped; a second one is the command's.
 */
int bbox_run_command_index(int argc, char * const argv[], int index)
{
    if(index < argc && strcmp(argv[index], "--") == 0)
        index++;

    return index;
}

int bbox_run(int argc, char * const argv[])
{
    char *buf = NULL;
    size_t buf_len = 0;

    int rval = BBOX_ERR_INVOCATION;

    bbox_conf_t *conf = bbox_config_new();
    if(!conf) {
        bbox_perror("run", "creating configuration context failed.\n");
        return BBOX_ERR_RUNTIME;
    }

    int non_optind;

    if((non_optind = bbox_run_getopt(conf, argc, argv)) < 0) {
        /* user asked for --help */
        if(non_optind == -1)
            rval = 0;
        goto cleanup_and_exit;
    }

    if(non_optind >= argc) {
        bbox_perror("run", "no target specified.\n");
        goto cleanup_and_exit;
    }

    char *target = argv[non_optind++];

    if(validate_target_name("run", target) == -1)
        goto cleanup_and_exit;

    non_optind = bbox_run_command_index(argc, argv, non_optind);

    bbox_path_join(
        &buf, bbox_config_get_target_dir(conf), target, &buf_len
    );

    struct stat st;

    if(lstat(buf, &st) == -1) {
        bbox_perror("login", "target '%s' not found.\n", target);
        goto cleanup_and_exit;
    }

    rval = BBOX_ERR_RUNTIME;

    /*
     * Mount special directories and home if configured (default).
     */
    if(bbox_mount_any(conf, buf) == -1)
        goto cleanup_and_exit;

    /*
     * We're not worried about this block, because we're currently running with
     * lowered privileges.
     */
    if(bbox_config_do_file_updates(conf))
        bbox_update_chroot_dynamic_config(buf, conf);

    /*
     * We clean out most of the environment except for variables starting with
     * AELTRA_ and a few select, such as CFLAGS. Then we log into the target and
     * execute what's left on the command line.
     */
    bbox_sanitize_environment();

    if(bbox_config_get_chroot_home_dir(conf))
        setenv("HOME", bbox_config_get_chroot_home_dir(conf), 1);

    rval = bbox_runas_user_chrooted(buf, argc-non_optind, &argv[non_optind],
            conf);

cleanup_and_exit:

    bbox_config_free(conf);
    free(buf);
    return rval;
}
