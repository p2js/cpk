/*
 * shell.h
 * Functions to start, execute commands in, and kill a shell.
 * Used to execute multiple build commands with the same shell,
 * allowing sharing context and avoiding the overhead of multiple
 * system(3) calls.
 */
#ifndef _CPK_SHELL_H
#define _CPK_SHELL_H

#include <sys/types.h>

typedef struct {
    pid_t pid;
    int in_fd;   // fd to write commands
    int out_fd;  // fd to read shell output
} Shell;

int shell_init(Shell* sh);
int shell_exec(Shell* sh, const char* command);
int shell_kill(Shell* sh);

#endif