/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2020-2022 Tobias Koch <tobias.koch@gmail.com>
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
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bbox-do.h"

void bbox_init_usage()
{
    printf(
        "                                                                        \n"
        "USAGE:                                                                  \n"
        "                                                                        \n"
        "  build-box init [OPTIONS]                                              \n"
        "                                                                        \n"
        "OPTIONS:                                                                \n"
        "                                                                        \n"
        " -h,--help             Print this help message and exit immediately.    \n"
        "                                                                        \n"
    );
}

int bbox_init_getopt(bbox_conf_t *conf, int argc, char * const argv[])
{
    int c;
    int option_index = 0;

    static struct option long_options[] = {
        {"help",     no_argument,       0, 'h'},
        { 0,         0,                 0,  0 }
    };

    optind = 1;

    while(1) {
        c = getopt_long(argc, argv, ":h", long_options, &option_index);

        if(c == -1)
            break;

        switch(c) {
            case 'h':
                bbox_init_usage();
                return -1;
            case '?':
            case ':':
                bbox_init_usage();
                return -2;
            default:
                /* impossible, ignore */
                break;
        }
    }

    return optind;
}

int bbox_init_user_directory()
{
    unsigned long uid = (unsigned long) getuid();
    unsigned long gid = (unsigned long) getgid();

    char *user_dir = bbox_get_user_dir(uid, NULL);

    if(!user_dir) {
        bbox_perror("bbox_init_user_directory", "failed to get user directory.\n");
        return -1;
    }

    struct stat st;

    if(lstat(user_dir, &st) == 0)
        goto success;

    if(bbox_raise_privileges() == -1)
        goto failure;

    if(mkdir(user_dir, 0755) == -1)
        goto failure;
    if(chown(user_dir, uid, gid) == -1)
        goto failure;

    if(bbox_lower_privileges() == -1)
        goto failure;

success:
    free(user_dir);
    return 0;

failure:
    bbox_lower_privileges();
    free(user_dir);
    return -1;
}

int bbox_init(int argc, char * const argv[])
{
    int non_optind;

    bbox_conf_t *conf = bbox_config_new();
    if(!conf) {
        bbox_perror("init", "creating configuration context failed.\n");
        return BBOX_ERR_RUNTIME;
    }

    if((non_optind = bbox_init_getopt(conf, argc, argv)) < 0) {
        bbox_config_free(conf);

        if(non_optind < -1) {
            return BBOX_ERR_INVOCATION;
        } else {
            /* user asked for --help */
            return 0;
        }
    }

    int rval = bbox_init_user_directory();

    bbox_config_free(conf);
    return rval;
}
