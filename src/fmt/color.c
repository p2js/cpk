#include "color.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

bool use_colors() {
    static int use = 2;  // 0 = false, 1 = true, 2 = unknown
    if (use == 2) {
        const char* no_color = getenv("NO_COLOR");
        use = no_color ? false : isatty(1);
    }
    return (bool)use;
}

void print_err(bool use_perror, const char* fmt, ...) {
    va_list ap;
    fprintf(stderr, use_colors() ? COLOR_BOLD(COLOR_REDBRIGHT("error")) " " : "ERROR: ");
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
    fprintf(stderr, use_colors() ? COLOR_BOLD(COLOR_YELLOW("warning")) " " : "WARNING: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void print_info(const char* fmt, ...) {
    va_list ap;
    fprintf(stderr, use_colors() ? COLOR_BOLD(COLOR_BLUEBRIGHT("info")) " " : "INFO: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}