/*
 * color.h
 * Functions and macro definitions for terminal-aware color formatting.
 */
#ifndef _CPK_COLOR_H
#define _CPK_COLOR_H

#include <stdbool.h>

bool use_colors;
void set_use_colors();

void print_err(bool use_perror, const char* fmt, ...);
void print_warn(const char* fmt, ...);
void print_info(const char* fmt, ...);

#define __DEFINE_COLOR__(code, message, reset) "\x1b[" #code "m" message "\x1b[" #reset "m"

#define COLOR_RESET(message) __DEFINE_COLOR__(0, message, 0)
#define COLOR_BOLD(message) __DEFINE_COLOR__(1, message, 22)
#define COLOR_DIM(message) __DEFINE_COLOR__(2, message, 22)
#define COLOR_ITALIC(message) __DEFINE_COLOR__(3, message, 23)
#define COLOR_UNDERLINE(message) __DEFINE_COLOR__(4, message, 24)
#define COLOR_BLINK(message) __DEFINE_COLOR__(5, message, 25)
#define COLOR_INVERSE(message) __DEFINE_COLOR__(7, message, 27)
#define COLOR_HIDDEN(message) __DEFINE_COLOR__(8, message, 28)
#define COLOR_STRIKETHROUGH(message) __DEFINE_COLOR__(9, message, 29)
#define COLOR_DOUBLEUNDERLINE(message) __DEFINE_COLOR__(21, message, 24)

#define COLOR_BLACK(message) __DEFINE_COLOR__(30, message, 39)
#define COLOR_RED(message) __DEFINE_COLOR__(31, message, 39)
#define COLOR_GREEN(message) __DEFINE_COLOR__(32, message, 39)
#define COLOR_YELLOW(message) __DEFINE_COLOR__(33, message, 39)
#define COLOR_BLUE(message) __DEFINE_COLOR__(34, message, 39)
#define COLOR_MAGENTA(message) __DEFINE_COLOR__(35, message, 39)
#define COLOR_CYAN(message) __DEFINE_COLOR__(36, message, 39)
#define COLOR_WHITE(message) __DEFINE_COLOR__(37, message, 39)

#define COLOR_BGBLACK(message) __DEFINE_COLOR__(40, message, 49)
#define COLOR_BGRED(message) __DEFINE_COLOR__(41, message, 49)
#define COLOR_BGGREEN(message) __DEFINE_COLOR__(42, message, 49)
#define COLOR_BGYELLOW(message) __DEFINE_COLOR__(43, message, 49)
#define COLOR_BGBLUE(message) __DEFINE_COLOR__(44, message, 49)
#define COLOR_BGMAGENTA(message) __DEFINE_COLOR__(45, message, 49)
#define COLOR_BGCYAN(message) __DEFINE_COLOR__(46, message, 49)
#define COLOR_BGWHITE(message) __DEFINE_COLOR__(47, message, 49)

#define COLOR_FRAMED(message) __DEFINE_COLOR__(51, message, 54)
#define COLOR_OVERLINED(message) __DEFINE_COLOR__(53, message, 55)

#define COLOR_GRAY(message) __DEFINE_COLOR__(90, message, 39)
#define COLOR_REDBRIGHT(message) __DEFINE_COLOR__(91, message, 39)
#define COLOR_GREENBRIGHT(message) __DEFINE_COLOR__(92, message, 39)
#define COLOR_YELLOWBRIGHT(message) __DEFINE_COLOR__(93, message, 39)
#define COLOR_BLUEBRIGHT(message) __DEFINE_COLOR__(94, message, 39)
#define COLOR_MAGENTABRIGHT(message) __DEFINE_COLOR__(95, message, 39)
#define COLOR_CYANBRIGHT(message) __DEFINE_COLOR__(96, message, 39)
#define COLOR_WHITEBRIGHT(message) __DEFINE_COLOR__(97, message, 39)

#define COLOR_BGGRAY(message) __DEFINE_COLOR__(100, message, 49)
#define COLOR_BGREDBRIGHT(message) __DEFINE_COLOR__(101, message, 49)
#define COLOR_BGGREENBRIGHT(message) __DEFINE_COLOR__(102, message, 49)
#define COLOR_BGYELLOWBRIGHT(message) __DEFINE_COLOR__(103, message, 49)
#define COLOR_BGBLUEBRIGHT(message) __DEFINE_COLOR__(104, message, 49)
#define COLOR_BGMAGENTABRIGHT(message) __DEFINE_COLOR__(105, message, 49)
#define COLOR_BGCYANBRIGHT(message) __DEFINE_COLOR__(106, message, 49)
#define COLOR_BGWHITEBRIGHT(message) __DEFINE_COLOR__(107, message, 49)

#endif