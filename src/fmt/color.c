/*
 * color.c
 * Functions for terminal-aware color formatting.
 */
#include "color.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

bool use_colors;

void set_use_colors() {
    use_colors = getenv("NO_COLOR") ? false : isatty(1);
}

void print_err(bool use_perror, const char* fmt, ...) {
    va_list ap;
    fprintf(stderr, use_colors ? COLOR_BOLD(COLOR_REDBRIGHT("error")) " " : "ERROR: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    if (use_perror) {
        fputs(": ", stderr);
        perror("");
    } else {
        fputc('\n', stderr);
    }
}

void print_warn(const char* fmt, ...) {
    va_list ap;
    fprintf(stderr, use_colors ? COLOR_BOLD(COLOR_YELLOW("warning")) " " : "WARNING: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void print_info(const char* fmt, ...) {
    va_list ap;
    fprintf(stderr, use_colors ? COLOR_BOLD(COLOR_BLUEBRIGHT("info")) " " : "INFO: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}