/* SPDX-License-Identifier: GPL-2.0 */
/*
 * THK - LLM Command Assistant for Linux
 *
 * Shared definitions for userspace tools
 */
#ifndef _THK_COMMON_H
#define _THK_COMMON_H

#include <stdint.h>
#include <stdbool.h>

#define THK_DEVICE_PATH		"/dev/thk"
#define THK_SOCKET_PATH		"/run/thk/thk.sock"
#define THK_CONFIG_PATH		"/etc/thk/thk.conf"
#define THK_PID_FILE		"/run/thk/thkd.pid"

#define THK_MAX_PROMPT_LEN	4096
#define THK_MAX_RESPONSE_LEN	65536
#define THK_MAX_STEPS		32
#define THK_MAX_CMD_LEN		4096
#define THK_MAX_DESC_LEN	512
#define THK_MAX_SUMMARY_LEN	1024

/* JSON protocol types */
#define THK_MSG_QUERY		"query"
#define THK_MSG_RESPONSE	"response"
#define THK_MSG_ERROR		"error"
#define THK_MSG_STATUS		"status"

/* Step flags */
#define THK_STEP_F_DANGEROUS	(1U << 0)
#define THK_STEP_F_NEEDS_ROOT	(1U << 1)
#define THK_STEP_F_INTERACTIVE	(1U << 2)

struct thk_step {
	int index;
	char description[THK_MAX_DESC_LEN];
	char command[THK_MAX_CMD_LEN];
	uint32_t flags;
};

struct thk_response {
	char summary[THK_MAX_SUMMARY_LEN];
	struct thk_step steps[THK_MAX_STEPS];
	int step_count;
};

#endif /* _THK_COMMON_H */
