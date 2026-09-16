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

void print_err(bool use_perror, const char* format_string, ...) {
    va_list arg_list;
    fprintf(stderr, use_colors ? COLOR_BOLD(COLOR_REDBRIGHT("error")) " " : "ERROR: ");
    va_start(arg_list, format_string);
    vfprintf(stderr, format_string, arg_list);
    va_end(arg_list);

    if (use_perror) {
        fputs(": ", stderr);
        perror("");
    } else {
        fputc('\n', stderr);
    }
}

void print_warn(const char* format_string, ...) {
    va_list arg_list;
    fprintf(stderr, use_colors ? COLOR_BOLD(COLOR_YELLOW("warning")) " " : "WARNING: ");
    va_start(arg_list, format_string);
    vfprintf(stderr, format_string, arg_list);
    va_end(arg_list);
    fputc('\n', stderr);
}

void print_info(const char* format_string, ...) {
    va_list arg_list;
    fprintf(stderr, use_colors ? COLOR_BOLD(COLOR_BLUEBRIGHT("info")) " " : "INFO: ");
    va_start(arg_list, format_string);
    vfprintf(stderr, format_string, arg_list);
    va_end(arg_list);
    fputc('\n', stderr);
}