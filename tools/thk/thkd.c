// SPDX-License-Identifier: GPL-2.0
/*
 * THK - LLM Command Assistant for Linux
 *
 * Daemon: Unix socket server, LLM dispatch, client handling
 *
 * Usage: thkd [-c config] [-f] [-v]
 *   -c config   Path to config file (default: /etc/thk/thk.conf)
 *   -f          Run in foreground (no daemonize)
 *   -v          Verbose output
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <fcntl.h>
#include <getopt.h>

#include "thk_common.h"
#include "thk_config.h"
#include "thk_llm.h"

static volatile sig_atomic_t running = 1;
static int verbose;

static void signal_handler(int sig)
{
	(void)sig;
	running = 0;
}

static void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [-c config] [-f] [-v]\n", prog);
	fprintf(stderr, "  -c config   Config file path (default: %s)\n",
		THK_CONFIG_PATH);
	fprintf(stderr, "  -f          Foreground mode\n");
	fprintf(stderr, "  -v          Verbose output\n");
}

/*
 * Minimal JSON helpers - we avoid external dependencies.
 * These handle the simple JSON protocol defined in the plan.
 */

/* Extract a string value from a JSON key */
static int json_get_string(const char *json, const char *key,
			   char *out, size_t out_len)
{
	char pattern[128];
	const char *p, *start, *end;

	snprintf(pattern, sizeof(pattern), "\"%s\"", key);
	p = strstr(json, pattern);
	if (!p)
		return -1;

	p += strlen(pattern);
	while (*p == ' ' || *p == ':' || *p == '\t')
		p++;
	if (*p != '"')
		return -1;

	start = p + 1;
	end = start;
	while (*end && *end != '"') {
		if (*end == '\\')
			end++;
		end++;
	}
	if (*end != '"')
		return -1;

	size_t len = end - start;

	if (len >= out_len)
		len = out_len - 1;
	memcpy(out, start, len);
	out[len] = '\0';
	return 0;
}

/* Build a JSON response from thk_response */
static int build_json_response(const struct thk_response *resp,
			       char *out, size_t out_len)
{
	int i, off = 0;

	off += snprintf(out + off, out_len - off,
			"{\"type\":\"response\",\"summary\":\"");

	/* Escape summary */
	for (const char *s = resp->summary; *s && off < (int)out_len - 10; s++) {
		if (*s == '"' || *s == '\\')
			out[off++] = '\\';
		out[off++] = *s;
	}

	off += snprintf(out + off, out_len - off, "\",\"steps\":[");

	for (i = 0; i < resp->step_count; i++) {
		const struct thk_step *step = &resp->steps[i];

		if (i > 0)
			out[off++] = ',';

		off += snprintf(out + off, out_len - off,
				"{\"index\":%d,\"description\":\"",
				step->index);

		for (const char *s = step->description;
		     *s && off < (int)out_len - 10; s++) {
			if (*s == '"' || *s == '\\')
				out[off++] = '\\';
			out[off++] = *s;
		}

		off += snprintf(out + off, out_len - off,
				"\",\"command\":\"");

		for (const char *s = step->command;
		     *s && off < (int)out_len - 10; s++) {
			if (*s == '"' || *s == '\\')
				out[off++] = '\\';
			out[off++] = *s;
		}

		off += snprintf(out + off, out_len - off,
				"\",\"flags\":%u}", step->flags);
	}

	off += snprintf(out + off, out_len - off, "]}");
	return off;
}

static int create_socket(const char *path)
{
	struct sockaddr_un addr;
	int fd;
	char *dir, *tmp;

	/* Ensure parent directory exists */
	tmp = strdup(path);
	if (!tmp)
		return -1;
	dir = tmp;
	char *last_slash = strrchr(dir, '/');

	if (last_slash) {
		*last_slash = '\0';
		mkdir(dir, 0770);
	}
	free(tmp);

	/* Remove stale socket */
	unlink(path);

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("socket");
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		close(fd);
		return -1;
	}

	/* Set socket permissions: root:thk 0770 */
	chmod(path, 0770);

	if (listen(fd, 8) < 0) {
		perror("listen");
		close(fd);
		return -1;
	}

	return fd;
}

static void handle_client(int client_fd, const struct thk_cfg *cfg)
{
	char buf[THK_MAX_RESPONSE_LEN];
	char prompt[THK_MAX_PROMPT_LEN];
	char raw_response[THK_MAX_RESPONSE_LEN];
	char json_response[THK_MAX_RESPONSE_LEN];
	struct thk_response resp;
	struct thk_query_context ctx = {};
	ssize_t n;
	int ret;

	n = read(client_fd, buf, sizeof(buf) - 1);
	if (n <= 0)
		return;
	buf[n] = '\0';

	if (verbose)
		fprintf(stderr, "thkd: received: %.256s...\n", buf);

	/* Parse JSON request */
	char type[32];

	if (json_get_string(buf, "type", type, sizeof(type)) < 0) {
		const char *err = "{\"type\":\"error\","
				  "\"message\":\"invalid request\"}";
		write(client_fd, err, strlen(err));
		return;
	}

	if (strcmp(type, THK_MSG_QUERY) != 0) {
		const char *err = "{\"type\":\"error\","
				  "\"message\":\"unknown type\"}";
		write(client_fd, err, strlen(err));
		return;
	}

	/* Extract prompt and context */
	if (json_get_string(buf, "prompt", prompt, sizeof(prompt)) < 0) {
		const char *err = "{\"type\":\"error\","
				  "\"message\":\"missing prompt\"}";
		write(client_fd, err, strlen(err));
		return;
	}

	/* Try to extract context fields (optional) */
	json_get_string(buf, "cwd", ctx.cwd, sizeof(ctx.cwd));
	json_get_string(buf, "user", ctx.user, sizeof(ctx.user));
	json_get_string(buf, "distro", ctx.distro, sizeof(ctx.distro));
	json_get_string(buf, "kernel", ctx.kernel, sizeof(ctx.kernel));

	/* Fill defaults */
	if (!ctx.cwd[0])
		getcwd(ctx.cwd, sizeof(ctx.cwd));
	if (!ctx.user[0]) {
		struct passwd *pw = getpwuid(getuid());

		if (pw)
			snprintf(ctx.user, sizeof(ctx.user), "%s", pw->pw_name);
	}

	if (verbose)
		fprintf(stderr, "thkd: querying %s backend (%s)\n",
			thk_backend_name(cfg->backend), cfg->model);

	/* Query LLM */
	ret = thk_llm_query(cfg, prompt, &ctx, raw_response,
			    sizeof(raw_response));
	if (ret < 0) {
		snprintf(json_response, sizeof(json_response),
			 "{\"type\":\"error\",\"message\":\"LLM query failed: %s\"}",
			 strerror(-ret));
		write(client_fd, json_response, strlen(json_response));
		return;
	}

	/* Parse response into steps */
	thk_llm_parse_response(raw_response, &resp);

	/* Build JSON response */
	build_json_response(&resp, json_response, sizeof(json_response));

	write(client_fd, json_response, strlen(json_response));
}

static void write_pid_file(void)
{
	FILE *f = fopen(THK_PID_FILE, "w");

	if (f) {
		fprintf(f, "%d\n", getpid());
		fclose(f);
	}
}

static void daemonize(void)
{
	pid_t pid = fork();

	if (pid < 0) {
		perror("fork");
		exit(1);
	}
	if (pid > 0)
		exit(0);	/* Parent exits */

	setsid();

	/* Redirect stdio to /dev/null */
	int fd = open("/dev/null", O_RDWR);

	if (fd >= 0) {
		dup2(fd, STDIN_FILENO);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		if (fd > STDERR_FILENO)
			close(fd);
	}
}

int main(int argc, char *argv[])
{
	struct thk_cfg cfg;
	const char *config_path = THK_CONFIG_PATH;
	int foreground = 0;
	int sock_fd, client_fd;
	int opt;

	while ((opt = getopt(argc, argv, "c:fvh")) != -1) {
		switch (opt) {
		case 'c':
			config_path = optarg;
			break;
		case 'f':
			foreground = 1;
			break;
		case 'v':
			verbose = 1;
			foreground = 1;	/* Verbose implies foreground */
			break;
		case 'h':
		default:
			usage(argv[0]);
			return opt == 'h' ? 0 : 1;
		}
	}

	/* Load config */
	if (thk_config_load(config_path, &cfg) < 0)
		fprintf(stderr, "thkd: warning: using default config "
			"(could not load %s)\n", config_path);

	if (verbose) {
		fprintf(stderr, "thkd: backend=%s endpoint=%s model=%s\n",
			thk_backend_name(cfg.backend), cfg.endpoint, cfg.model);
		fprintf(stderr, "thkd: socket=%s\n", cfg.socket_path);
	}

	/* Setup signals */
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGPIPE, SIG_IGN);

	/* Create listening socket */
	sock_fd = create_socket(cfg.socket_path);
	if (sock_fd < 0)
		return 1;

	if (!foreground)
		daemonize();

	write_pid_file();

	if (foreground)
		fprintf(stderr, "thkd: listening on %s\n", cfg.socket_path);

	/* Main accept loop */
	while (running) {
		struct sockaddr_un client_addr;
		socklen_t client_len = sizeof(client_addr);

		client_fd = accept(sock_fd, (struct sockaddr *)&client_addr,
				   &client_len);
		if (client_fd < 0) {
			if (errno == EINTR)
				continue;
			perror("accept");
			break;
		}

		handle_client(client_fd, &cfg);
		close(client_fd);
	}

	close(sock_fd);
	unlink(cfg.socket_path);
	unlink(THK_PID_FILE);

	if (foreground)
		fprintf(stderr, "thkd: shutdown\n");

	return 0;
}
