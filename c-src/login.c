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
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "bbox-do.h"

void bbox_login_usage()
{
    printf(
        "                                                                        \n"
        "USAGE:                                                                  \n"
        "                                                                        \n"
        "  build-box login [OPTIONS] <target-name>                               \n"
        "                                                                        \n"
        "OPTIONS:                                                                \n"
        "                                                                        \n"
        "  -h, --help            Print this help message and exit immediately.   \n"
        "                                                                        \n"
        "  -m, --mount <fstype>  Mount 'dev', 'proc', 'sys' or 'home'. If this   \n"
        "                        option is not specified then the default is to  \n"
        "                        mount all of them.                              \n"
        "                                                                        \n"
        "  --no-mount            Don't mount any filesystems per default.        \n"
        "                                                                        \n"
        "  --no-file-copy        Don't copy passwd database, group database and  \n"
        "                        resolv.conf from host.                          \n"
        "                                                                        \n"
    );
}

int bbox_login_getopt(bbox_conf_t *conf, int argc, char * const argv[])
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
        { 0,             0,                 0,  0 }
    };

    /*
     * Per default we turn on mounting of /proc /sys /dev, home and enable
     * the copying of passwd, group and hosts files from the host.
     */
    bbox_config_clear_mount(conf);
    bbox_config_enable_file_updates(conf);
    optind = 1;

    while(1) {
        c = getopt_long(argc, argv, ":ht:m:", long_options, &option_index);

        if(c == -1)
            break;

        switch(c) {
            case 'h':
                bbox_login_usage();
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
            case '?':
            case ':':
                bbox_login_usage();
                return -2;
            default:
                /* impossible, ignore */
                break;
        }
    }

    if(do_mount_all)
        bbox_config_set_mount_all(conf);

    if(argc - 1 > optind) {
        bbox_login_usage();
        return -1;
    }

    return optind;
}

int bbox_login(int argc, char * const argv[])
{
    char *buf = NULL;
    size_t buf_len = 0;

    int rval = BBOX_ERR_INVOCATION;

    bbox_conf_t *conf = bbox_config_new();
    if(!conf) {
        bbox_perror("login", "creating configuration context failed.\n");
        return BBOX_ERR_RUNTIME;
    }

    int non_optind;

    if((non_optind = bbox_login_getopt(conf, argc, argv)) < 0) {
        /* user asked for --help */
        if(non_optind == -1)
            rval = 0;
        goto cleanup_and_exit;
    }

    if(non_optind >= argc) {
        bbox_perror("login", "no target specified.\n");
        goto cleanup_and_exit;
    }

    char *target = argv[non_optind];

    if(validate_target_name("login", target) == -1)
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
     * Copy passwd, group and hosts information from the host to the target.
     */
    if(bbox_config_do_file_updates(conf))
        bbox_update_chroot_dynamic_config(buf, conf);

    /*
     * We clean out most of the environment except for variables starting with
     * AELTRA_ and a few select, such as CFLAGS. Then we log into the target and
     * change into the home directory.
     */
    bbox_sanitize_environment();

    if(bbox_config_get_chroot_home_dir(conf))
        setenv("HOME", bbox_config_get_chroot_home_dir(conf), 1);

    /* If this succeeds, it doesn't return. */
    if(bbox_login_sh_chrooted(buf, bbox_config_get_chroot_home_dir(conf)) == 0)
        rval = 0;

cleanup_and_exit:

    free(buf);
    bbox_config_free(conf);
    return rval;
}
