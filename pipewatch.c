/*
 * Copyright (c) 2017, Michael Santos <michael.santos@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <getopt.h>

#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/errno.h>

#include <pipeline.h>

#define PIPEWATCH_VERSION "0.1.0"

typedef struct {
    int sig;
    int verbose;
    int monitor;
    pid_t pid;
    pid_t mpid;
} pipewatch_state_t;

static void sighandler(int sig, siginfo_t *info, void *context);
static pid_t pipewatch_monitor_init(pipewatch_state_t *s);
static int pipewatch_signal_init();
static int pipewatch_wait(pipewatch_state_t *s, pipeline *p);
static void pipewatch_signal_broadcast(pipewatch_state_t *s, pid_t pid,
        int sig);
static void pipewatch_print_process_tree(pipewatch_state_t *s, pipeline *p);
static void usage();

static int PIPEWATCH_SIGREAD_FILENO;
static int PIPEWATCH_SIGWRITE_FILENO;

extern char *__progname;

#define VERBOSE(__s, __n, ...) do { \
    if (__s->verbose >= __n) { \
        (void)fprintf(stderr, __VA_ARGS__); \
    } \
} while (0)

static const struct option long_options[] =
{
    {"signal",        required_argument,  NULL, 's'},
    {"monitor",       no_argument,        NULL, 'm'},
    {"verbose",       no_argument,        NULL, 'v'},
    {"help",          no_argument,        NULL, 'h'},
    {NULL,            0,                  NULL, 0}
};

    static void
sighandler(int sig, siginfo_t *info, void *context)
{
    if (write(PIPEWATCH_SIGWRITE_FILENO, info,
                sizeof(siginfo_t)) != sizeof(siginfo_t))
        (void)close(PIPEWATCH_SIGWRITE_FILENO);
}

    int
main(int argc, char *argv[])
{
    pipeline *p = NULL;
    int n = 0;
    int ch = 0;
    int status = 0;
    int sigpipe[2];
    pipewatch_state_t *s = NULL;

    s = calloc(1, sizeof(pipewatch_state_t));

    if (!s)
        err(EXIT_FAILURE, "calloc");

    s->sig = SIGTERM;
    s->pid = getpid();

    while ((ch = getopt_long(argc, argv, "hms:v", long_options, NULL)) != -1) {
        switch (ch) {
            case 'm':
                s->monitor = 1;
                break;
            case 's':
                s->sig = atoi(optarg);
                break;
            case 'v':
                s->verbose += 1;
                break;
            case 'h':
            default:
                usage();
        }
    }

    argc -= optind;
    argv += optind;

    if (argc < 1)
        usage();

    if (s->monitor) {
        s->mpid = pipewatch_monitor_init(s);

        if (s->mpid < 0)
            err(EXIT_FAILURE, "pipewatch_monitor_init");
    }

    if (setpgid(0, 0) < 0)
        err(EXIT_FAILURE, "setpgid");

    if (!s->verbose) {
        if (setenv("PIPELINE_QUIET", "1", 1) < 0)
            err(EXIT_FAILURE, "setenv");
    }

    if (pipe2(sigpipe, O_CLOEXEC) < 0)
        err(EXIT_FAILURE, "pipe");

    PIPEWATCH_SIGREAD_FILENO = sigpipe[0];
    PIPEWATCH_SIGWRITE_FILENO = sigpipe[1];

    p = pipeline_new();

    for (n = 0; n < argc; n++) {
        pipeline_command_args(p, "/bin/sh", "-c", argv[n], NULL);
    }

    pipeline_start(p);

    if (pipewatch_signal_init() < 0)
        err(EXIT_FAILURE, "pipewatch_signal_init");

    status = pipewatch_wait(s, p);

    if (status != 0)
        pipewatch_signal_broadcast(s, 0, s->sig);

    exit(status);
}

    static pid_t
pipewatch_monitor_init(pipewatch_state_t *s)
{
    int mfd[2] = {0};
    char buf;
    int n = 0;
    pid_t mpid = 0;

    if (pipe2(mfd, O_CLOEXEC) < 0)
        return -1;

    mpid = fork();

    switch (mpid) {
        case -1:
            (void)close(mfd[0]);
            (void)close(mfd[1]);
            return -1;
        case 0:
            if (close(mfd[1]) < 0)
                return -1;
            n = read(mfd[0], &buf, 1);
            (void)n;
			VERBOSE(s, 1, "monitor: broadcasting signal=%d pgid=%d\n",
                    s->sig, s->pid);
            pipewatch_signal_broadcast(s, s->pid * -1, s->sig);
            exit(0);
            break;
        default:
            (void)close(mfd[0]);
            return mpid;
    }

    return -1;
}

    static int
pipewatch_signal_init()
{
    struct sigaction act = {0};
    int sig = 0;

    act.sa_flags |= SA_SIGINFO;
    act.sa_sigaction = sighandler;
    (void)sigfillset(&act.sa_mask);

    for (sig = 1; sig < NSIG; sig++) {
        switch (sig) {
            case SIGTRAP:

            case SIGTTIN:
            case SIGTTOU:
            case SIGTSTP:
            case SIGCONT:
            case SIGSTOP:
                continue;

            default:
                if (sigaction(sig, &act, NULL) < 0) {
                    if (errno == EINVAL)
                        continue;

                    return -1;
                }
                break;
        }
    }

    return 0;
}

    static void
pipewatch_print_process_tree(pipewatch_state_t *s, pipeline *p)
{
    int i = 0;
    int ncmd = pipeline_get_ncommands(p);
    char *pipeline = pipeline_tostring(p);

    (void)fprintf(stderr, "%s(%d,%d) %s\n", __progname, s->pid,
            getpgrp(), pipeline);
    free(pipeline);

    if (s->monitor)
        (void)fprintf(stderr, "   |-monitor(%d,%d)\n", s->mpid,
                getpgid(s->mpid));

    for (i = 0; i < ncmd; i++) {
        (void)fprintf(stderr, "   |-%s(%d,%d)\n",
                pipecmd_tostring(pipeline_get_command(p, i)),
                pipeline_get_pid(p, i),
                getpgid(pipeline_get_pid(p, i)));
    }
}

    static int
pipewatch_wait(pipewatch_state_t *s, pipeline *p)
{
    siginfo_t info = {0};
    int status;
    ssize_t n = 0;
    int ncmd = 0;

    ncmd = pipeline_get_ncommands(p);

    if (s->verbose)
        pipewatch_print_process_tree(s, p);

    for ( ; ; ) {
        n = read(PIPEWATCH_SIGREAD_FILENO, &info, sizeof(info));

        if (n != sizeof(info)) {
            if (errno == EAGAIN || errno == EINTR)
                continue;

            return 127+errno;
        }

        if (info.si_signo != SIGCHLD) {
            VERBOSE(s, 1, "signal=%d:%d\n", info.si_pid, info.si_signo);

            pipewatch_signal_broadcast(s, 0, info.si_signo);
            continue;
        }

        VERBOSE(s, 1, "status=%d:%d\n", info.si_pid, info.si_status);

        if (info.si_code == CLD_EXITED && info.si_status != 0)
            return info.si_status;

        switch (info.si_status) {
            case SIGTRAP:
            case SIGTTIN:
            case SIGTTOU:
            case SIGTSTP:
            case SIGCONT:
            case SIGSTOP:
            case 0:
                break;

            default:
                return info.si_status;
        }

        for ( ; ; ) {
            pid_t pid = waitpid(-1, &status, WNOHANG);

            if (errno == ECHILD || pid == 0)
                break;

            if (pid < 0)
                return 127+errno;

            if (WEXITSTATUS(status) != 0)
                return WEXITSTATUS(status);

            ncmd--;
            if (ncmd == 0)
                goto PIPEWATCH_EXIT;
        }
    }

PIPEWATCH_EXIT:
    return 0;
}

    void
pipewatch_signal_broadcast(pipewatch_state_t *s, pid_t pid, int sig)
{
    struct sigaction act = {0};

    if (pid == 0) {
        act.sa_handler = SIG_IGN;
        (void)sigfillset(&act.sa_mask);
        (void)sigaction(sig, &act, NULL);
    }

    (void)kill(pid, sig);

    if (pid == 0) {
        act.sa_flags |= SA_SIGINFO;
        act.sa_sigaction = sighandler;
        (void)sigaction(sig, &act, NULL);
    }
}

    static void
usage()
{
    errx(EXIT_FAILURE, "[OPTION] <COMMAND> <...>\n"
            "version: %s\n\n"
            "-s, --signal <signum>\tsignal sent to process group on error\n"
            "-m, --monitor\t\tfork monitor process\n"
            "-v, --verbose\t\tverbose mode\n"
            "-h, --help\t\thelp",
            PIPEWATCH_VERSION);
}
