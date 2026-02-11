/* SPDX-License-Identifier: GPL-2.0 */
/*
 * THK - LLM Command Assistant for Linux
 *
 * Configuration parser header
 */
#ifndef _THK_CONFIG_H
#define _THK_CONFIG_H

#define THK_CFG_MAX_VALUE	512
#define THK_CFG_MAX_ENTRIES	64

enum thk_backend {
	THK_BACKEND_OLLAMA = 0,
	THK_BACKEND_OPENAI,
	THK_BACKEND_ANTHROPIC,
	THK_BACKEND_LLAMACPP,
	THK_BACKEND_CUSTOM,
};

struct thk_cfg {
	enum thk_backend backend;
	char endpoint[THK_CFG_MAX_VALUE];
	char model[THK_CFG_MAX_VALUE];
	char api_key[THK_CFG_MAX_VALUE];
	int max_tokens;
	float temperature;
	char socket_path[THK_CFG_MAX_VALUE];
	int audit_enabled;
	int rate_limit;
	char custom_prompt[THK_CFG_MAX_VALUE];
};

/* Parse config file, returns 0 on success */
int thk_config_load(const char *path, struct thk_cfg *cfg);

/* Initialize config with defaults */
void thk_config_defaults(struct thk_cfg *cfg);

/* Get backend name as string */
const char *thk_backend_name(enum thk_backend b);

#endif /* _THK_CONFIG_H */
