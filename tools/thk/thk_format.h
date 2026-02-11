/* SPDX-License-Identifier: GPL-2.0 */
/*
 * THK - LLM Command Assistant for Linux
 *
 * Terminal output formatting header
 */
#ifndef _THK_FORMAT_H
#define _THK_FORMAT_H

#include "thk_common.h"

/* ANSI color codes */
#define THK_COLOR_RESET		"\033[0m"
#define THK_COLOR_BOLD		"\033[1m"
#define THK_COLOR_DIM		"\033[2m"
#define THK_COLOR_RED		"\033[31m"
#define THK_COLOR_GREEN		"\033[32m"
#define THK_COLOR_YELLOW	"\033[33m"
#define THK_COLOR_BLUE		"\033[34m"
#define THK_COLOR_MAGENTA	"\033[35m"
#define THK_COLOR_CYAN		"\033[36m"
#define THK_COLOR_WHITE		"\033[37m"
#define THK_COLOR_BG_GRAY	"\033[48;5;236m"

/* Print formatted THK response with colors */
void thk_format_response(const struct thk_response *resp);

/* Print a single step */
void thk_format_step(const struct thk_step *step);

/* Print the interactive menu */
void thk_format_menu(void);

/* Print error message */
void thk_format_error(const char *msg);

/* Print status/info message */
void thk_format_info(const char *msg);

/* Check if stdout is a terminal (for color support) */
int thk_format_has_color(void);

#endif /* _THK_FORMAT_H */
