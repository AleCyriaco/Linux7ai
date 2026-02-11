// SPDX-License-Identifier: GPL-2.0
/*
 * THK - LLM Command Assistant for Linux
 *
 * Terminal output formatting with colors
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "thk_format.h"

static int use_color = -1;

int thk_format_has_color(void)
{
	if (use_color < 0)
		use_color = isatty(STDOUT_FILENO);
	return use_color;
}

static const char *color(const char *code)
{
	return thk_format_has_color() ? code : "";
}

void thk_format_response(const struct thk_response *resp)
{
	int i;

	printf("\n%s%sTHK:%s %s\n\n",
	       color(THK_COLOR_BOLD), color(THK_COLOR_CYAN),
	       color(THK_COLOR_RESET),
	       resp->summary);

	for (i = 0; i < resp->step_count; i++)
		thk_format_step(&resp->steps[i]);

	if (resp->step_count > 0)
		thk_format_menu();
}

void thk_format_step(const struct thk_step *step)
{
	const char *flag_color = color(THK_COLOR_GREEN);

	if (step->flags & THK_STEP_F_DANGEROUS)
		flag_color = color(THK_COLOR_RED);
	else if (step->flags & THK_STEP_F_NEEDS_ROOT)
		flag_color = color(THK_COLOR_YELLOW);

	printf("  %s%s%d.%s %s%s%s\n",
	       color(THK_COLOR_BOLD), flag_color,
	       step->index, color(THK_COLOR_RESET),
	       color(THK_COLOR_WHITE),
	       step->description,
	       color(THK_COLOR_RESET));

	if (step->command[0]) {
		printf("     %s$ %s%s%s\n\n",
		       color(THK_COLOR_DIM),
		       color(THK_COLOR_GREEN),
		       step->command,
		       color(THK_COLOR_RESET));
	} else {
		printf("\n");
	}
}

void thk_format_menu(void)
{
	printf("  %s[%sE%s]xecutar todos  "
	       "[%sS%s]elecionar  "
	       "[%sC%s]ancelar%s\n",
	       color(THK_COLOR_DIM),
	       color(THK_COLOR_BOLD), color(THK_COLOR_DIM),
	       color(THK_COLOR_BOLD), color(THK_COLOR_DIM),
	       color(THK_COLOR_BOLD), color(THK_COLOR_DIM),
	       color(THK_COLOR_RESET));
}

void thk_format_error(const char *msg)
{
	fprintf(stderr, "%s%serror:%s %s\n",
		color(THK_COLOR_BOLD), color(THK_COLOR_RED),
		color(THK_COLOR_RESET), msg);
}

void thk_format_info(const char *msg)
{
	printf("%s%sinfo:%s %s\n",
	       color(THK_COLOR_BOLD), color(THK_COLOR_BLUE),
	       color(THK_COLOR_RESET), msg);
}
