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

static pid_t pid_one = 0;

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
        "  build-box run [OPTIONS] <target-name> -- <command>                     \n"
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
    optind = 1;

    while(1) {
        c = getopt_long(argc, argv, ":ht:m:", long_options, &option_index);

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

int bbox_runas_user_chrooted(const char *sys_root, int argc,
        char * const argv[], const bbox_conf_t *conf)
{
    static char *shells[] = {"/tools/bin/sh", "/usr/bin/sh", NULL};

    char *sh = NULL;
    char *buf = NULL;
    size_t buf_len = 0;
    struct stat st;

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

    if(bbox_reset_supplementary_groups() == -1)
        return BBOX_ERR_RUNTIME;

    /* now do actual chroot call. */
    if(chroot(".") == -1) {
        bbox_perror("bbox_runas_user_chrooted",
                "chroot to system root failed: %s.\n",
                strerror(errno));
        return BBOX_ERR_RUNTIME;
    }

    if(bbox_lower_privileges() == -1)
        return BBOX_ERR_RUNTIME;

    /* Do this while we're at the fs root. */
    bbox_try_fix_pkg_cache_symlink("");

    /* this is non-critical. */
    char *home_dir = bbox_config_get_home_dir(conf);
    if(home_dir)
        if(chdir(home_dir));

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

    /* prepare the command line. */
    if(argc > 1) {
        for(size_t i = 0; i < argc; i++) {
            if(i > 0)
                bbox_sep_join(&buf, buf, " ", argv[i], &buf_len);
            else
                bbox_sep_join(&buf, "", "", argv[i], &buf_len);
        }
    } else {
        buf = argv[0];
    }

    pid_t pid = 0;

    if(bbox_config_get_isolation(conf)) {
        if(bbox_raise_privileges() == -1)
            return BBOX_ERR_RUNTIME;

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
            return BBOX_ERR_RUNTIME;
        }

        /* Note that we only fork and wait when isolation is requested. */
        if((pid = fork()) == -1) {
            bbox_perror("bbox_runas_user_chrooted", "fork failed: %s\n",
                    strerror(errno));
            return BBOX_ERR_RUNTIME;
        }

        if(pid == 0 && bbox_config_get_mount_proc(conf)) {
            if(mount(NULL, "/proc", "proc", 0, NULL) != 0) {
                bbox_perror("bbox_runas_user_chrooted",
                        "failed to mount /proc inside namespace: %s\n",
                        strerror(errno));
                _exit(BBOX_ERR_RUNTIME);
            }
        }
    }

    /* Drop privileges in both parent and child (if there is a child). */
    int drop_priv_result = bbox_drop_privileges();

    /* Here pid is 0 if we didn't fork or if we forked and are the parent. */
    if(pid == 0) {
        if(drop_priv_result == -1) {
            bbox_perror("bbox_runas_user_chrooted",
                    "failed to drop privileges in confined child: %s\n",
                    strerror(errno));
            _exit(BBOX_ERR_RUNTIME);
        }

        char *command[6] = {"sh", "-l", "-c", "--", buf, NULL};
        execvp(sh, command);

        bbox_perror("bbox_runas_user_chrooted", "failed to invoke shell: %s\n",
                strerror(errno));
        _exit(BBOX_ERR_RUNTIME);
    }

    /*
     * We only get here if --isolate was set and we forked above. Otherwise,
     * the execvp above will already have replaced the process.
     */

    pid_one = pid;

    int signals_to_handle[] = {SIGTERM, SIGINT, SIGHUP, 0};

    for(int i = 0; signals_to_handle[i] != 0; i++) {
        signal(signals_to_handle[i], signal_handler);
    }

    int wstatus = 0;

    /* If we were interrupted, we try again. */
    while(waitpid(pid, &wstatus, 0) == -1) {
        /*
         * This is really the only error that can occur. ECHILD cannot, because
         * we forked the child ourselves. And EINVAL because we don't pass any
         * flags to waitpid.
         */
        if(errno != EINTR)
            break;
    }

    /* Pass through the exit status, if that is possible. */
    if(WIFEXITED(wstatus))
        return WEXITSTATUS(wstatus);

    return BBOX_ERR_RUNTIME;
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
        bbox_update_chroot_dynamic_config(buf);

    /*
     * We clean out most of the environment except for variables starting with
     * AELTRA_ and a few select, such as CFLAGS. Then we log into the target and
     * execute what's left on the command line.
     */
    bbox_sanitize_environment();

    rval = bbox_runas_user_chrooted(buf, argc-non_optind, &argv[non_optind],
            conf);

cleanup_and_exit:

    bbox_config_free(conf);
    free(buf);
    return rval;
}
