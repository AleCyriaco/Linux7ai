// SPDX-License-Identifier: GPL-2.0
/*
 * THK - LLM Command Assistant for Linux
 *
 * LLM backend abstraction: Ollama, OpenAI, Anthropic, llama.cpp
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <ctype.h>

#include "thk_llm.h"

#define HTTP_BUF_SIZE	65536
#define URL_MAX		512

struct http_url {
	char host[256];
	char path[256];
	int port;
	int use_ssl;
};

static int parse_url(const char *url, struct http_url *out)
{
	const char *p = url;

	memset(out, 0, sizeof(*out));

	if (strncmp(p, "https://", 8) == 0) {
		out->use_ssl = 1;
		out->port = 443;
		p += 8;
	} else if (strncmp(p, "http://", 7) == 0) {
		out->use_ssl = 0;
		out->port = 80;
		p += 7;
	} else {
		return -EINVAL;
	}

	const char *slash = strchr(p, '/');
	const char *colon = strchr(p, ':');

	if (colon && (!slash || colon < slash)) {
		size_t hlen = colon - p;

		if (hlen >= sizeof(out->host))
			return -ENAMETOOLONG;
		memcpy(out->host, p, hlen);
		out->host[hlen] = '\0';
		out->port = atoi(colon + 1);
		p = slash ? slash : p + strlen(p);
	} else if (slash) {
		size_t hlen = slash - p;

		if (hlen >= sizeof(out->host))
			return -ENAMETOOLONG;
		memcpy(out->host, p, hlen);
		out->host[hlen] = '\0';
		p = slash;
	} else {
		snprintf(out->host, sizeof(out->host), "%s", p);
		p = "";
	}

	snprintf(out->path, sizeof(out->path), "%s", *p ? p : "/");
	return 0;
}

static int http_post(const char *url, const char *body,
		     const char *auth_header,
		     char *response, size_t response_len)
{
	struct http_url u;
	struct sockaddr_in addr;
	struct hostent *he;
	char *req = NULL;
	char *buf = NULL;
	int fd = -1, ret = 0;
	ssize_t n;
	size_t total = 0;
	size_t body_len = strlen(body);
	size_t req_len;

	ret = parse_url(url, &u);
	if (ret)
		return ret;

	if (u.use_ssl) {
		fprintf(stderr, "thk: SSL not supported, use http endpoint\n");
		return -ENOTSUP;
	}

	he = gethostbyname(u.host);
	if (!he)
		return -EHOSTUNREACH;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -errno;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(u.port);
	memcpy(&addr.sin_addr, he->h_addr, he->h_length);

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		ret = -errno;
		goto out;
	}

	/* Build HTTP request */
	req_len = body_len + 1024;
	req = malloc(req_len);
	if (!req) {
		ret = -ENOMEM;
		goto out;
	}

	if (auth_header && auth_header[0]) {
		snprintf(req, req_len,
			 "POST %s HTTP/1.1\r\n"
			 "Host: %s:%d\r\n"
			 "Content-Type: application/json\r\n"
			 "Connection: close\r\n"
			 "Content-Length: %zu\r\n"
			 "%s\r\n"
			 "\r\n"
			 "%s",
			 u.path, u.host, u.port, body_len,
			 auth_header, body);
	} else {
		snprintf(req, req_len,
			 "POST %s HTTP/1.1\r\n"
			 "Host: %s:%d\r\n"
			 "Content-Type: application/json\r\n"
			 "Connection: close\r\n"
			 "Content-Length: %zu\r\n"
			 "\r\n"
			 "%s",
			 u.path, u.host, u.port, body_len, body);
	}

	n = write(fd, req, strlen(req));
	if (n < 0) {
		ret = -errno;
		goto out;
	}

	/* Read response */
	buf = malloc(HTTP_BUF_SIZE);
	if (!buf) {
		ret = -ENOMEM;
		goto out;
	}

	while ((n = read(fd, buf + total,
			 HTTP_BUF_SIZE - total - 1)) > 0) {
		total += n;
		if (total >= HTTP_BUF_SIZE - 1)
			break;
	}
	buf[total] = '\0';

	/* Skip HTTP headers - find \r\n\r\n */
	char *body_start = strstr(buf, "\r\n\r\n");
	if (body_start) {
		body_start += 4;
		snprintf(response, response_len, "%s", body_start);
	} else {
		snprintf(response, response_len, "%s", buf);
	}

out:
	free(req);
	free(buf);
	if (fd >= 0)
		close(fd);
	return ret;
}

/*
 * Extract a JSON string value by key from raw JSON.
 * Handles escaped quotes. Writes unescaped result to out.
 */
static int json_extract_string(const char *json, const char *key,
			       char *out, size_t out_len)
{
	char pattern[128];
	const char *p, *start;
	size_t off = 0;

	snprintf(pattern, sizeof(pattern), "\"%s\"", key);
	p = strstr(json, pattern);
	if (!p)
		return -1;

	p += strlen(pattern);
	while (*p == ' ' || *p == ':' || *p == '\t')
		p++;
	if (*p != '"')
		return -1;

	start = ++p;
	while (*p && off < out_len - 1) {
		if (*p == '\\' && p[1]) {
			p++;
			switch (*p) {
			case 'n':  out[off++] = '\n'; break;
			case 't':  out[off++] = '\t'; break;
			case '"':  out[off++] = '"';  break;
			case '\\': out[off++] = '\\'; break;
			default:   out[off++] = *p;   break;
			}
			p++;
		} else if (*p == '"') {
			break;
		} else {
			out[off++] = *p++;
		}
	}
	out[off] = '\0';
	return off > 0 ? 0 : -1;
}

/*
 * Build the full system prompt with context information.
 */
int thk_llm_build_prompt(const char *user_prompt,
			  const struct thk_query_context *ctx,
			  char *out, size_t out_len)
{
	snprintf(out, out_len,
		 "You are THK, a Linux command assistant integrated into the kernel. "
		 "Given a user question, provide clear steps with exact commands.\n\n"
		 "Context:\n"
		 "- Working directory: %s\n"
		 "- User: %s\n"
		 "- Distribution: %s\n"
		 "- Kernel: %s\n\n"
		 "Rules:\n"
		 "1. Each step must have a short description and a single shell command\n"
		 "2. Use standard Linux utilities available on most distros\n"
		 "3. Mark dangerous commands with [DANGEROUS] prefix in description\n"
		 "4. Commands requiring root should have [ROOT] prefix\n"
		 "5. Format output as numbered steps:\n"
		 "   STEP N: description\n"
		 "   CMD: command\n\n"
		 "User question: %s",
		 ctx->cwd, ctx->user, ctx->distro, ctx->kernel,
		 user_prompt);
	return 0;
}

static int query_ollama(const struct thk_cfg *cfg, const char *prompt,
			char *response, size_t response_len)
{
	char url[URL_MAX];
	char *body = NULL;
	size_t body_len;
	int ret;

	snprintf(url, sizeof(url), "%s/api/generate", cfg->endpoint);

	body_len = strlen(prompt) + 1024;
	body = malloc(body_len);
	if (!body)
		return -ENOMEM;

	/* Escape prompt for JSON */
	char *escaped = malloc(strlen(prompt) * 2 + 1);
	if (!escaped) {
		free(body);
		return -ENOMEM;
	}

	const char *s = prompt;
	char *d = escaped;

	while (*s) {
		if (*s == '"' || *s == '\\') {
			*d++ = '\\';
		} else if (*s == '\n') {
			*d++ = '\\';
			*d++ = 'n';
			s++;
			continue;
		} else if (*s == '\t') {
			*d++ = '\\';
			*d++ = 't';
			s++;
			continue;
		}
		*d++ = *s++;
	}
	*d = '\0';

	snprintf(body, body_len,
		 "{\"model\":\"%s\",\"prompt\":\"%s\","
		 "\"stream\":false,"
		 "\"options\":{\"temperature\":%.1f,\"num_predict\":%d}}",
		 cfg->model, escaped, cfg->temperature, cfg->max_tokens);

	free(escaped);

	char *raw = malloc(response_len);
	if (!raw) {
		free(body);
		return -ENOMEM;
	}

	ret = http_post(url, body, NULL, raw, response_len);
	free(body);

	if (ret == 0) {
		/* Ollama returns {"response":"...","done":true,...} */
		if (json_extract_string(raw, "response", response,
					response_len) < 0)
			snprintf(response, response_len, "%s", raw);
	}
	free(raw);

	return ret;
}

static int query_openai(const struct thk_cfg *cfg, const char *prompt,
			char *response, size_t response_len)
{
	char url[URL_MAX];
	char *body = NULL;
	char auth[512];
	size_t body_len;
	int ret;

	snprintf(url, sizeof(url), "%s/v1/chat/completions", cfg->endpoint);
	snprintf(auth, sizeof(auth), "Authorization: Bearer %s", cfg->api_key);

	body_len = strlen(prompt) + 1024;
	body = malloc(body_len);
	if (!body)
		return -ENOMEM;

	char *escaped = malloc(strlen(prompt) * 2 + 1);
	if (!escaped) {
		free(body);
		return -ENOMEM;
	}

	const char *s = prompt;
	char *d = escaped;

	while (*s) {
		if (*s == '"' || *s == '\\') {
			*d++ = '\\';
		} else if (*s == '\n') {
			*d++ = '\\';
			*d++ = 'n';
			s++;
			continue;
		} else if (*s == '\t') {
			*d++ = '\\';
			*d++ = 't';
			s++;
			continue;
		}
		*d++ = *s++;
	}
	*d = '\0';

	snprintf(body, body_len,
		 "{\"model\":\"%s\","
		 "\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}],"
		 "\"max_tokens\":%d,\"temperature\":%.1f}",
		 cfg->model, escaped, cfg->max_tokens, cfg->temperature);

	free(escaped);

	char *raw = malloc(response_len);
	if (!raw) {
		free(body);
		return -ENOMEM;
	}

	ret = http_post(url, body, auth, raw, response_len);
	free(body);

	if (ret == 0) {
		/* OpenAI returns {"choices":[{"message":{"content":"..."}}]} */
		if (json_extract_string(raw, "content", response,
					response_len) < 0)
			snprintf(response, response_len, "%s", raw);
	}
	free(raw);

	return ret;
}

static int query_anthropic(const struct thk_cfg *cfg, const char *prompt,
			   char *response, size_t response_len)
{
	char url[URL_MAX];
	char *body = NULL;
	char auth[1024];
	size_t body_len;
	int ret;

	snprintf(url, sizeof(url), "%s/v1/messages", cfg->endpoint);
	snprintf(auth, sizeof(auth),
		 "x-api-key: %s\r\nanthropic-version: 2023-06-01",
		 cfg->api_key);

	body_len = strlen(prompt) + 1024;
	body = malloc(body_len);
	if (!body)
		return -ENOMEM;

	char *escaped = malloc(strlen(prompt) * 2 + 1);
	if (!escaped) {
		free(body);
		return -ENOMEM;
	}

	const char *s = prompt;
	char *d = escaped;

	while (*s) {
		if (*s == '"' || *s == '\\') {
			*d++ = '\\';
		} else if (*s == '\n') {
			*d++ = '\\';
			*d++ = 'n';
			s++;
			continue;
		} else if (*s == '\t') {
			*d++ = '\\';
			*d++ = 't';
			s++;
			continue;
		}
		*d++ = *s++;
	}
	*d = '\0';

	snprintf(body, body_len,
		 "{\"model\":\"%s\","
		 "\"max_tokens\":%d,"
		 "\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}]}",
		 cfg->model, cfg->max_tokens, escaped);

	free(escaped);

	char *raw = malloc(response_len);
	if (!raw) {
		free(body);
		return -ENOMEM;
	}

	ret = http_post(url, body, auth, raw, response_len);
	free(body);

	if (ret == 0) {
		/* Anthropic returns {"content":[{"text":"..."}]} */
		if (json_extract_string(raw, "text", response,
					response_len) < 0)
			snprintf(response, response_len, "%s", raw);
	}
	free(raw);

	return ret;
}

static int query_llamacpp(const struct thk_cfg *cfg, const char *prompt,
			  char *response, size_t response_len)
{
	char url[URL_MAX];
	char *body = NULL;
	size_t body_len;
	int ret;

	snprintf(url, sizeof(url), "%s/completion", cfg->endpoint);

	body_len = strlen(prompt) + 1024;
	body = malloc(body_len);
	if (!body)
		return -ENOMEM;

	char *escaped = malloc(strlen(prompt) * 2 + 1);
	if (!escaped) {
		free(body);
		return -ENOMEM;
	}

	const char *s = prompt;
	char *d = escaped;

	while (*s) {
		if (*s == '"' || *s == '\\') {
			*d++ = '\\';
		} else if (*s == '\n') {
			*d++ = '\\';
			*d++ = 'n';
			s++;
			continue;
		} else if (*s == '\t') {
			*d++ = '\\';
			*d++ = 't';
			s++;
			continue;
		}
		*d++ = *s++;
	}
	*d = '\0';

	snprintf(body, body_len,
		 "{\"prompt\":\"%s\","
		 "\"n_predict\":%d,\"temperature\":%.1f,"
		 "\"stream\":false}",
		 escaped, cfg->max_tokens, cfg->temperature);

	free(escaped);

	char *raw = malloc(response_len);
	if (!raw) {
		free(body);
		return -ENOMEM;
	}

	ret = http_post(url, body, NULL, raw, response_len);
	free(body);

	if (ret == 0) {
		/* llama.cpp returns {"content":"..."} */
		if (json_extract_string(raw, "content", response,
					response_len) < 0)
			snprintf(response, response_len, "%s", raw);
	}
	free(raw);

	return ret;
}

int thk_llm_query(const struct thk_cfg *cfg, const char *prompt,
		   const struct thk_query_context *ctx,
		   char *response, size_t response_len)
{
	char *full_prompt;
	size_t plen;
	int ret;

	plen = strlen(prompt) + 2048;
	full_prompt = malloc(plen);
	if (!full_prompt)
		return -ENOMEM;

	thk_llm_build_prompt(prompt, ctx, full_prompt, plen);

	switch (cfg->backend) {
	case THK_BACKEND_OLLAMA:
		ret = query_ollama(cfg, full_prompt, response, response_len);
		break;
	case THK_BACKEND_OPENAI:
		ret = query_openai(cfg, full_prompt, response, response_len);
		break;
	case THK_BACKEND_ANTHROPIC:
		ret = query_anthropic(cfg, full_prompt, response, response_len);
		break;
	case THK_BACKEND_LLAMACPP:
		ret = query_llamacpp(cfg, full_prompt, response, response_len);
		break;
	default:
		fprintf(stderr, "thk: unsupported backend: %s\n",
			thk_backend_name(cfg->backend));
		ret = -EINVAL;
		break;
	}

	free(full_prompt);
	return ret;
}

/*
 * Parse a raw LLM response into structured steps.
 * Looks for patterns like "STEP N: description\nCMD: command"
 * Also handles numbered lists like "1. description\n   $ command"
 */
int thk_llm_parse_response(const char *raw, struct thk_response *resp)
{
	const char *p = raw;
	int step_idx = 0;

	memset(resp, 0, sizeof(*resp));

	/* Try to extract summary - first line or paragraph */
	const char *nl = strchr(raw, '\n');
	if (nl) {
		size_t slen = nl - raw;

		if (slen >= sizeof(resp->summary))
			slen = sizeof(resp->summary) - 1;
		memcpy(resp->summary, raw, slen);
		resp->summary[slen] = '\0';
	}

	while (*p && step_idx < THK_MAX_STEPS) {
		struct thk_step *step = &resp->steps[step_idx];

		/* Pattern 1: "STEP N: description" */
		if (strncasecmp(p, "STEP ", 5) == 0) {
			p += 5;
			/* Skip number and colon */
			while (*p && *p != ':')
				p++;
			if (*p == ':')
				p++;
			while (*p == ' ')
				p++;

			/* Read description until newline */
			const char *eol = strchr(p, '\n');
			if (eol) {
				size_t dlen = eol - p;

				if (dlen >= sizeof(step->description))
					dlen = sizeof(step->description) - 1;
				memcpy(step->description, p, dlen);
				step->description[dlen] = '\0';
				p = eol + 1;
			}

			/* Look for CMD: line */
			while (*p == ' ' || *p == '\t')
				p++;
			if (strncasecmp(p, "CMD:", 4) == 0) {
				p += 4;
				while (*p == ' ')
					p++;
				eol = strchr(p, '\n');
				if (eol) {
					size_t clen = eol - p;

					if (clen >= sizeof(step->command))
						clen = sizeof(step->command) - 1;
					memcpy(step->command, p, clen);
					step->command[clen] = '\0';
					p = eol + 1;
				}
			}

			step->index = step_idx + 1;

			/* Check for danger markers */
			if (strstr(step->description, "[DANGEROUS]"))
				step->flags |= THK_STEP_F_DANGEROUS;
			if (strstr(step->description, "[ROOT]"))
				step->flags |= THK_STEP_F_NEEDS_ROOT;

			step_idx++;
			continue;
		}

		/* Pattern 2: "N. description\n   $ command" */
		if (isdigit((unsigned char)*p)) {
			const char *dot = p;

			while (isdigit((unsigned char)*dot))
				dot++;
			if (*dot == '.' || *dot == ')') {
				dot++;
				while (*dot == ' ')
					dot++;

				const char *eol = strchr(dot, '\n');
				if (eol) {
					size_t dlen = eol - dot;

					if (dlen >= sizeof(step->description))
						dlen = sizeof(step->description) - 1;
					memcpy(step->description, dot, dlen);
					step->description[dlen] = '\0';
					p = eol + 1;

					/* Look for "$ command" or "  command" */
					while (*p == ' ' || *p == '\t')
						p++;
					if (*p == '$') {
						p++;
						while (*p == ' ')
							p++;
					}
					eol = strchr(p, '\n');
					if (eol && eol > p) {
						size_t clen = eol - p;

						if (clen >= sizeof(step->command))
							clen = sizeof(step->command) - 1;
						memcpy(step->command, p, clen);
						step->command[clen] = '\0';
						p = eol + 1;
					}

					step->index = step_idx + 1;
					if (strstr(step->description,
						   "[DANGEROUS]"))
						step->flags |=
							THK_STEP_F_DANGEROUS;
					if (strstr(step->description, "[ROOT]"))
						step->flags |=
							THK_STEP_F_NEEDS_ROOT;
					step_idx++;
					continue;
				}
			}
		}

		/* Skip to next line */
		const char *next = strchr(p, '\n');

		if (next)
			p = next + 1;
		else
			break;
	}

	resp->step_count = step_idx;
	return 0;
}
