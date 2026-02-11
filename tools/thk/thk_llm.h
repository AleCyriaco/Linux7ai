/* SPDX-License-Identifier: GPL-2.0 */
/*
 * THK - LLM Command Assistant for Linux
 *
 * LLM backend abstraction header
 */
#ifndef _THK_LLM_H
#define _THK_LLM_H

#include "thk_config.h"
#include "thk_common.h"

/* Context sent with each query */
struct thk_query_context {
	char cwd[256];
	char user[64];
	char distro[128];
	char kernel[128];
};

/*
 * Send a prompt to the configured LLM backend and parse the response.
 * Returns 0 on success, negative errno on error.
 * The response buffer must be pre-allocated with THK_MAX_RESPONSE_LEN.
 */
int thk_llm_query(const struct thk_cfg *cfg, const char *prompt,
		   const struct thk_query_context *ctx,
		   char *response, size_t response_len);

/*
 * Parse raw LLM text response into structured steps.
 * Returns 0 on success.
 */
int thk_llm_parse_response(const char *raw, struct thk_response *resp);

/*
 * Build the system prompt for the LLM based on context.
 */
int thk_llm_build_prompt(const char *user_prompt,
			  const struct thk_query_context *ctx,
			  char *out, size_t out_len);

#endif /* _THK_LLM_H */
