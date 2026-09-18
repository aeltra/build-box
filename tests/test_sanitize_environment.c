/*
 * test_sanitize_environment.c - bbox_sanitize_environment() keeps the
 * allow-listed variables and returns whatever the environment contains
 *
 * Copyright (c) 2026 Tobias Koch <tobias.koch@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * The environment is an array of strings handed to execve(2), and the
 * kernel does not check that each one looks like NAME=value. An entry
 * with an empty name, "=x", has an "=" and so is handed to unsetenv(),
 * which refuses an empty name and removes nothing. The loop then did
 * not advance, on the assumption that the next entry had shifted into
 * place, and spun on that entry forever. An entry without any "=" was
 * simply kept.
 *
 * The malformed case runs in a forked child under a time limit, so
 * that the old behaviour is a failure and not a hang of the suite.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "bbox-do.h"

#include "test.h"

extern char **environ;

static int env_count(void)
{
    int n = 0;
    while(environ[n] != NULL)
        n++;
    return n;
}

static int env_has(const char *entry)
{
    for(int i = 0; environ[i] != NULL; i++) {
        if(strcmp(environ[i], entry) == 0)
            return 1;
    }
    return 0;
}

/*
 * Exit codes of the child: 0 if the malformed entries are gone, 3 if
 * the nameless one is still there, 4 if the one without "=" is.
 */
static void malformed_child(void)
{
    static char *env[] = {"=x", "NOEQUALS", "HOME=/home/me", "SECRET=1", NULL};

    environ = env;
    bbox_sanitize_environment();

    if(env_has("=x"))
        _exit(3);
    if(env_has("NOEQUALS"))
        _exit(4);
    _exit(0);
}

int main(void)
{
    /* ── the allow list ───────────────────────────────────────────── */

    {
        static char *env[] = {
            "HOME=/home/me",
            "PATH=/usr/bin",
            "LD_PRELOAD=/tmp/evil.so",
            "AELTRA_RELEASE=ollie",
            "CFLAGS=-O2",
            "SECRET=1",
            NULL
        };

        environ = env;
        bbox_sanitize_environment();

        test_str_eq(getenv("HOME"), "/home/me", "HOME is kept");
        test_str_eq(getenv("AELTRA_RELEASE"), "ollie", "an AELTRA_ variable is kept");
        test_str_eq(getenv("CFLAGS"), "-O2", "CFLAGS is kept");
        test_str_eq(getenv("PATH"), NULL, "PATH is removed");
        test_str_eq(getenv("LD_PRELOAD"), NULL, "LD_PRELOAD is removed");
        test_str_eq(getenv("SECRET"), NULL, "an unknown variable is removed");
        test_int_eq(env_count(), 3, "and nothing else is left");
    }

    /* ── malformed entries ────────────────────────────────────────── */

    {
        pid_t pid = fork();
        int wstatus = 0;
        int finished = 0;

        if(pid == -1) {
            perror("fork");
            return 2;
        }
        if(pid == 0)
            malformed_child();

        for(int tenths = 0; tenths < 50; tenths++) {
            struct timespec ts = {0, 100000000};

            if(waitpid(pid, &wstatus, WNOHANG) == pid) {
                finished = 1;
                break;
            }
            nanosleep(&ts, NULL);
        }

        if(!finished) {
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
        }

        test_ok(finished, "the sanitizer returns with a nameless entry");
        test_ok(finished && WIFEXITED(wstatus) && WEXITSTATUS(wstatus) != 3,
                "and the nameless entry is removed");
        test_ok(finished && WIFEXITED(wstatus) && WEXITSTATUS(wstatus) != 4,
                "as is an entry without an equals sign");
    }

    return test_summary();
}
