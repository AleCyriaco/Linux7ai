// SPDX-License-Identifier: GPL-2.0
/*
 * THK - LLM Command Assistant for Linux
 *
 * Configuration file parser (key=value format)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "thk_common.h"
#include "thk_config.h"

static const char *backend_names[] = {
	[THK_BACKEND_OLLAMA]	= "ollama",
	[THK_BACKEND_OPENAI]	= "openai",
	[THK_BACKEND_ANTHROPIC]	= "anthropic",
	[THK_BACKEND_LLAMACPP]	= "llamacpp",
	[THK_BACKEND_CUSTOM]	= "custom",
};

const char *thk_backend_name(enum thk_backend b)
{
	if (b >= 0 && b <= THK_BACKEND_CUSTOM)
		return backend_names[b];
	return "unknown";
}

static enum thk_backend parse_backend(const char *val)
{
	int i;

	for (i = 0; i <= THK_BACKEND_CUSTOM; i++) {
		if (strcmp(val, backend_names[i]) == 0)
			return (enum thk_backend)i;
	}
	return THK_BACKEND_OLLAMA;
}

static char *trim(char *s)
{
	char *end;

	while (isspace((unsigned char)*s))
		s++;
	if (*s == '\0')
		return s;

	end = s + strlen(s) - 1;
	while (end > s && isspace((unsigned char)*end))
		end--;
	end[1] = '\0';
	return s;
}

void thk_config_defaults(struct thk_cfg *cfg)
{
	memset(cfg, 0, sizeof(*cfg));
	cfg->backend = THK_BACKEND_OLLAMA;
	snprintf(cfg->endpoint, sizeof(cfg->endpoint),
		 "http://localhost:11434");
	snprintf(cfg->model, sizeof(cfg->model), "llama3.2");
	cfg->max_tokens = 2048;
	cfg->temperature = 0.3f;
	snprintf(cfg->socket_path, sizeof(cfg->socket_path),
		 "%s", THK_SOCKET_PATH);
	cfg->audit_enabled = 1;
	cfg->rate_limit = 10;
}

int thk_config_load(const char *path, struct thk_cfg *cfg)
{
	FILE *f;
	char line[1024];
	int lineno = 0;

	thk_config_defaults(cfg);

	f = fopen(path, "r");
	if (!f)
		return -errno;

	while (fgets(line, sizeof(line), f)) {
		char *key, *val, *eq;

		lineno++;

		/* Strip comments and newlines */
		char *comment = strchr(line, '#');
		if (comment)
			*comment = '\0';

		key = trim(line);
		if (*key == '\0')
			continue;

		eq = strchr(key, '=');
		if (!eq) {
			fprintf(stderr, "thk: config:%d: missing '='\n",
				lineno);
			continue;
		}

		*eq = '\0';
		key = trim(key);
		val = trim(eq + 1);

		if (strcmp(key, "backend") == 0)
			cfg->backend = parse_backend(val);
		else if (strcmp(key, "endpoint") == 0)
			snprintf(cfg->endpoint, sizeof(cfg->endpoint),
				 "%s", val);
		else if (strcmp(key, "model") == 0)
			snprintf(cfg->model, sizeof(cfg->model), "%s", val);
		else if (strcmp(key, "api_key") == 0)
			snprintf(cfg->api_key, sizeof(cfg->api_key),
				 "%s", val);
		else if (strcmp(key, "max_tokens") == 0)
			cfg->max_tokens = atoi(val);
		else if (strcmp(key, "temperature") == 0)
			cfg->temperature = strtof(val, NULL);
		else if (strcmp(key, "socket_path") == 0)
			snprintf(cfg->socket_path, sizeof(cfg->socket_path),
				 "%s", val);
		else if (strcmp(key, "audit_enabled") == 0)
			cfg->audit_enabled = atoi(val);
		else if (strcmp(key, "rate_limit") == 0)
			cfg->rate_limit = atoi(val);
		else if (strcmp(key, "custom_prompt") == 0)
			snprintf(cfg->custom_prompt,
				 sizeof(cfg->custom_prompt), "%s", val);
		else
			fprintf(stderr,
				"thk: config:%d: unknown key '%s'\n",
				lineno, key);
	}

	fclose(f);
	return 0;
}
