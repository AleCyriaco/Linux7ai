// SPDX-License-Identifier: GPL-2.0
/*
 * THK - LLM Command Assistant for Linux
 *
 * CLI: query mode, Unix socket connection, interactive execution
 *
 * Usage: thk "question"
 *        thk --status
 *        thk --version
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <fcntl.h>
#include <pwd.h>
#include <termios.h>
#include <getopt.h>

#include "thk_common.h"
#include "thk_format.h"

/* Include UAPI header for ioctl interface */
#include "../../include/uapi/misc/thk.h"

static const char *socket_path = THK_SOCKET_PATH;

static int connect_daemon(void)
{
	struct sockaddr_un addr;
	int fd;

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path);

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(fd);
		return -1;
	}

	return fd;
}

static int send_query(int sock_fd, const char *prompt, char *response,
		      size_t response_len)
{
	char request[THK_MAX_PROMPT_LEN + 512];
	char cwd[256] = "";
	char user[64] = "";
	char distro[128] = "Linux";
	char kernel[128] = "";
	struct utsname uts;
	struct passwd *pw;
	ssize_t n;
	size_t total = 0;

	getcwd(cwd, sizeof(cwd));

	pw = getpwuid(getuid());
	if (pw)
		snprintf(user, sizeof(user), "%s", pw->pw_name);

	if (uname(&uts) == 0)
		snprintf(kernel, sizeof(kernel), "%s %s", uts.sysname,
			 uts.release);

	/* Read /etc/os-release for distro info */
	FILE *f = fopen("/etc/os-release", "r");

	if (f) {
		char line[256];

		while (fgets(line, sizeof(line), f)) {
			if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
				char *val = line + 12;

				if (*val == '"')
					val++;
				char *end = strrchr(val, '"');

				if (end)
					*end = '\0';
				end = strchr(val, '\n');
				if (end)
					*end = '\0';
				snprintf(distro, sizeof(distro), "%s", val);
				break;
			}
		}
		fclose(f);
	}

	/* Build JSON request */
	snprintf(request, sizeof(request),
		 "{\"type\":\"query\",\"prompt\":\"%s\","
		 "\"context\":{\"cwd\":\"%s\",\"user\":\"%s\","
		 "\"distro\":\"%s\",\"kernel\":\"%s\"}}",
		 prompt, cwd, user, distro, kernel);

	n = write(sock_fd, request, strlen(request));
	if (n < 0)
		return -errno;

	/* Read response */
	while ((n = read(sock_fd, response + total,
			 response_len - total - 1)) > 0) {
		total += n;
		if (total >= response_len - 1)
			break;
	}
	response[total] = '\0';

	return total > 0 ? 0 : -EIO;
}

/* Minimal JSON parser for response */
static int parse_response_json(const char *json, struct thk_response *resp)
{
	const char *p;
	char type[32] = "";
	int step_idx = 0;

	memset(resp, 0, sizeof(*resp));

	/* Check type */
	p = strstr(json, "\"type\"");
	if (p) {
		p = strchr(p + 6, '"');
		if (p) {
			p++;
			const char *end = strchr(p, '"');

			if (end) {
				size_t len = end - p;

				if (len >= sizeof(type))
					len = sizeof(type) - 1;
				memcpy(type, p, len);
				type[len] = '\0';
			}
		}
	}

	if (strcmp(type, "error") == 0) {
		p = strstr(json, "\"message\"");
		if (p) {
			p = strchr(p + 9, '"');
			if (p) {
				p++;
				const char *end = strchr(p, '"');

				if (end) {
					size_t len = end - p;

					if (len >= sizeof(resp->summary))
						len = sizeof(resp->summary) - 1;
					memcpy(resp->summary, p, len);
				}
			}
		}
		return -1;
	}

	/* Extract summary */
	p = strstr(json, "\"summary\"");
	if (p) {
		p = strchr(p + 9, '"');
		if (p) {
			p++;
			const char *end = p;

			while (*end && !(*end == '"' && *(end - 1) != '\\'))
				end++;
			size_t len = end - p;

			if (len >= sizeof(resp->summary))
				len = sizeof(resp->summary) - 1;
			memcpy(resp->summary, p, len);
		}
	}

	/* Parse steps array */
	p = strstr(json, "\"steps\"");
	if (!p)
		return 0;
	p = strchr(p, '[');
	if (!p)
		return 0;
	p++;

	while (*p && step_idx < THK_MAX_STEPS) {
		struct thk_step *step = &resp->steps[step_idx];
		const char *obj = strchr(p, '{');

		if (!obj)
			break;
		const char *obj_end = strchr(obj, '}');

		if (!obj_end)
			break;

		/* Parse index */
		const char *idx = strstr(obj, "\"index\"");

		if (idx && idx < obj_end) {
			idx += 7;
			while (*idx == ':' || *idx == ' ')
				idx++;
			step->index = atoi(idx);
		}

		/* Parse description */
		const char *desc = strstr(obj, "\"description\"");

		if (desc && desc < obj_end) {
			desc = strchr(desc + 13, '"');
			if (desc) {
				desc++;
				const char *end = desc;

				while (*end && !(*end == '"' &&
						 *(end - 1) != '\\'))
					end++;
				size_t len = end - desc;

				if (len >= sizeof(step->description))
					len = sizeof(step->description) - 1;
				memcpy(step->description, desc, len);
			}
		}

		/* Parse command */
		const char *cmd = strstr(obj, "\"command\"");

		if (cmd && cmd < obj_end) {
			cmd = strchr(cmd + 9, '"');
			if (cmd) {
				cmd++;
				const char *end = cmd;

				while (*end && !(*end == '"' &&
						 *(end - 1) != '\\'))
					end++;
				size_t len = end - cmd;

				if (len >= sizeof(step->command))
					len = sizeof(step->command) - 1;
				memcpy(step->command, cmd, len);
			}
		}

		/* Parse flags */
		const char *fl = strstr(obj, "\"flags\"");

		if (fl && fl < obj_end) {
			fl += 7;
			while (*fl == ':' || *fl == ' ')
				fl++;
			step->flags = atoi(fl);
		}

		step_idx++;
		p = obj_end + 1;
	}

	resp->step_count = step_idx;
	return 0;
}

static int validate_via_kernel(const char *command)
{
	struct thk_exec_request req = {};
	struct thk_exec_result result = {};
	int fd, ret;

	fd = open(THK_DEVICE_PATH, O_RDWR);
	if (fd < 0) {
		/* /dev/thk not available, skip kernel validation */
		return 0;
	}

	snprintf(req.command, sizeof(req.command), "%s", command);
	req.flags = THK_EXEC_F_AUDIT;
	req.uid = getuid();

	ret = ioctl(fd, THK_IOC_EXEC_VALIDATE, &req);
	if (ret < 0) {
		close(fd);
		return -errno;
	}

	ret = ioctl(fd, THK_IOC_EXEC_STATUS, &result);
	close(fd);

	if (ret < 0)
		return -errno;

	if (result.result == THK_RESULT_BLOCKED) {
		fprintf(stderr, "thk: command blocked by kernel: %s\n",
			result.reason);
		return -EPERM;
	}
	if (result.result == THK_RESULT_RATE_LIMITED) {
		fprintf(stderr, "thk: rate limited\n");
		return -EAGAIN;
	}

	return 0;
}

static int execute_step(const struct thk_step *step)
{
	int ret;

	if (!step->command[0])
		return 0;

	/* Validate through kernel module first */
	ret = validate_via_kernel(step->command);
	if (ret < 0)
		return ret;

	printf("\n  > %s\n\n", step->command);
	ret = system(step->command);
	printf("\n");
	return ret;
}

static int interactive_mode(const struct thk_response *resp)
{
	char choice[16];
	int i;

	printf("\n  > ");
	fflush(stdout);

	if (!fgets(choice, sizeof(choice), stdin))
		return 0;

	switch (choice[0]) {
	case 'e':
	case 'E':
		/* Execute all */
		for (i = 0; i < resp->step_count; i++) {
			if (resp->steps[i].flags & THK_STEP_F_DANGEROUS) {
				printf("  Skip dangerous step %d? [y/N] ",
				       resp->steps[i].index);
				fflush(stdout);
				char confirm[8];

				if (fgets(confirm, sizeof(confirm), stdin) &&
				    (confirm[0] == 'y' || confirm[0] == 'Y'))
					continue;
			}
			execute_step(&resp->steps[i]);
		}
		break;

	case 's':
	case 'S':
		/* Select specific steps */
		printf("  Enter step numbers (e.g. 1,3,4): ");
		fflush(stdout);
		char sel[64];

		if (!fgets(sel, sizeof(sel), stdin))
			break;

		char *tok = strtok(sel, ",\n ");

		while (tok) {
			int num = atoi(tok);

			for (i = 0; i < resp->step_count; i++) {
				if (resp->steps[i].index == num) {
					execute_step(&resp->steps[i]);
					break;
				}
			}
			tok = strtok(NULL, ",\n ");
		}
		break;

	case 'c':
	case 'C':
	default:
		break;
	}

	return 0;
}

static int cmd_version(void)
{
	int fd;
	__u32 kversion = 0;

	printf("thk CLI version %d.%d.%d\n",
	       (THK_VERSION >> 16) & 0xff,
	       (THK_VERSION >> 8) & 0xff,
	       THK_VERSION & 0xff);

	fd = open(THK_DEVICE_PATH, O_RDONLY);
	if (fd >= 0) {
		if (ioctl(fd, THK_IOC_VERSION, &kversion) == 0) {
			printf("kernel module version %d.%d.%d\n",
			       (kversion >> 16) & 0xff,
			       (kversion >> 8) & 0xff,
			       kversion & 0xff);
		}
		close(fd);
	} else {
		printf("kernel module: not loaded\n");
	}

	return 0;
}

static int cmd_status(void)
{
	struct thk_stats stats;
	struct thk_config cfg;
	int fd;

	fd = open(THK_DEVICE_PATH, O_RDONLY);
	if (fd < 0) {
		thk_format_error("cannot open /dev/thk (module not loaded?)");
		return 1;
	}

	if (ioctl(fd, THK_IOC_GET_STATS, &stats) == 0) {
		printf("Statistics:\n");
		printf("  requests:     %llu\n",
		       (unsigned long long)stats.total_requests);
		printf("  allowed:      %llu\n",
		       (unsigned long long)stats.total_allowed);
		printf("  blocked:      %llu\n",
		       (unsigned long long)stats.total_blocked);
		printf("  rate_limited: %llu\n",
		       (unsigned long long)stats.total_rate_limited);
		printf("  uptime:       %llu seconds\n",
		       (unsigned long long)stats.uptime_secs);
	}

	if (ioctl(fd, THK_IOC_GET_CONFIG, &cfg) == 0) {
		printf("\nConfiguration:\n");
		printf("  audit:     %s\n",
		       cfg.audit_enabled ? "enabled" : "disabled");
		printf("  rate_limit: %u req/min\n", cfg.rate_limit);
		printf("  blocklist:  %u patterns\n", cfg.blocklist_count);
	}

	close(fd);
	return 0;
}

static void usage_cli(const char *prog)
{
	fprintf(stderr, "Usage: %s [options] \"question\"\n\n", prog);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  --version   Show version info\n");
	fprintf(stderr, "  --status    Show kernel module status\n");
	fprintf(stderr, "  --socket    Socket path (default: %s)\n",
		THK_SOCKET_PATH);
	fprintf(stderr, "  --help      Show this help\n");
}

int main(int argc, char *argv[])
{
	static struct option long_opts[] = {
		{ "version", no_argument, NULL, 'V' },
		{ "status",  no_argument, NULL, 'S' },
		{ "socket",  required_argument, NULL, 's' },
		{ "help",    no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};
	int opt;
	int sock_fd;
	char *response;
	struct thk_response resp;
	int ret;
	const char *prompt;

	while ((opt = getopt_long(argc, argv, "VSs:h", long_opts, NULL)) != -1) {
		switch (opt) {
		case 'V':
			return cmd_version();
		case 'S':
			return cmd_status();
		case 's':
			socket_path = optarg;
			break;
		case 'h':
			usage_cli(argv[0]);
			return 0;
		default:
			usage_cli(argv[0]);
			return 1;
		}
	}

	if (optind >= argc) {
		usage_cli(argv[0]);
		return 1;
	}

	prompt = argv[optind];

	/* Connect to daemon */
	sock_fd = connect_daemon();
	if (sock_fd < 0) {
		thk_format_error(
			"cannot connect to thkd (is the daemon running?)");
		fprintf(stderr,
			"  Start with: thkd -c /etc/thk/thk.conf\n");
		return 1;
	}

	/* Send query and receive response */
	response = malloc(THK_MAX_RESPONSE_LEN);
	if (!response) {
		close(sock_fd);
		return 1;
	}

	ret = send_query(sock_fd, prompt, response, THK_MAX_RESPONSE_LEN);
	close(sock_fd);

	if (ret < 0) {
		thk_format_error("failed to get response from daemon");
		free(response);
		return 1;
	}

	/* Parse JSON response */
	ret = parse_response_json(response, &resp);
	free(response);

	if (ret < 0) {
		thk_format_error(resp.summary[0] ? resp.summary :
				 "invalid response from daemon");
		return 1;
	}

	/* Display formatted response */
	thk_format_response(&resp);

	/* Interactive mode */
	if (resp.step_count > 0 && isatty(STDIN_FILENO))
		interactive_mode(&resp);

	return 0;
}
