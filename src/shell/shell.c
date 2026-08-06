/*
 * shell.c
 * Functions to start, execute commands in, and kill a shell.
 * Used to execute multiple build commands with the same shell,
 * allowing sharing context and avoiding the overhead of multiple
 * system(3) calls.
 */
#include "shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int shell_init(Shell* sh) {
    int in_pipe[2];
    int out_pipe[2];

    if (pipe(in_pipe) != 0) return 1;
    if (pipe(out_pipe) != 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(out_pipe[1], STDERR_FILENO);

        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);

        execl("/bin/sh", "sh", (char*)0);
        _exit(127);
    }

    close(in_pipe[0]);
    close(out_pipe[1]);

    sh->pid = pid;
    sh->in_fd = in_pipe[1];
    sh->out_fd = out_pipe[0];

    if (dprintf(sh->in_fd, "set -e\n") < 0) return 1;
    return 0;
}

static int read_line_fd(int fd, char* buf, size_t cap) {
    size_t i = 0;
    while (i + 1 < cap) {
        char c;
        ssize_t n = read(fd, &c, 1);
        if (n <= 0) break;
        buf[i++] = c;
        if (c == '\n') break;
    }
    buf[i] = '\0';
    return (i > 0) ? 0 : -1;
}

int shell_exec(Shell* sh, const char* command) {
    // Use a status marker to print the exit code
    char marker[64];
    snprintf(marker, sizeof(marker), "__CMD_STATUS_%ld__", (long)getpid());

    // run command and then print status marker
    if (dprintf(sh->in_fd, "%s\n", command) < 0) return -1;
    if (dprintf(sh->in_fd, "printf '%s%%d\\n' $?\n", marker) < 0) return -1;

    // read output until marker line appears
    char line[4096];
    int status = -1;

    while (read_line_fd(sh->out_fd, line, sizeof(line)) == 0) {
        if (strncmp(line, marker, strlen(marker)) == 0) {
            status = atoi(line + strlen(marker));
            break;
        }

        fputs(line, stdout);
        fflush(stdout);
    }

    return status;
}

int shell_kill(Shell* sh) {
    close(sh->in_fd);
    close(sh->out_fd);

    int status;
    if (waitpid(sh->pid, &status, 0) < 0) return -1;

    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}
