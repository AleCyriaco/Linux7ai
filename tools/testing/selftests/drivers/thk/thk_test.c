// SPDX-License-Identifier: GPL-2.0
/*
 * THK - LLM Command Assistant selftests
 *
 * Tests the /dev/thk ioctl interface: version, validate, stats, config.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

#include "../../../../include/uapi/misc/thk.h"

#include "../../kselftest_harness.h"

#define THK_DEV "/dev/thk"

FIXTURE(thk)
{
	int fd;
};

FIXTURE_SETUP(thk)
{
	self->fd = open(THK_DEV, O_RDWR);
	ASSERT_GE(self->fd, 0) {
		TH_LOG("Cannot open %s: %s", THK_DEV, strerror(errno));
		TH_LOG("Is the thk module loaded?");
	}
}

FIXTURE_TEARDOWN(thk)
{
	if (self->fd >= 0)
		close(self->fd);
}

TEST_F(thk, version)
{
	__u32 version = 0;
	int ret;

	ret = ioctl(self->fd, THK_IOC_VERSION, &version);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(version, THK_VERSION);

	TH_LOG("THK version: %d.%d.%d",
		(version >> 16) & 0xff,
		(version >> 8) & 0xff,
		version & 0xff);
}

TEST_F(thk, get_stats)
{
	struct thk_stats stats = {};
	int ret;

	ret = ioctl(self->fd, THK_IOC_GET_STATS, &stats);
	ASSERT_EQ(ret, 0);
	ASSERT_GE(stats.uptime_secs, 0);

	TH_LOG("Stats: requests=%llu allowed=%llu blocked=%llu",
		(unsigned long long)stats.total_requests,
		(unsigned long long)stats.total_allowed,
		(unsigned long long)stats.total_blocked);
}

TEST_F(thk, get_config)
{
	struct thk_config cfg = {};
	int ret;

	ret = ioctl(self->fd, THK_IOC_GET_CONFIG, &cfg);
	ASSERT_EQ(ret, 0);

	TH_LOG("Config: audit=%u rate_limit=%u blocklist=%u",
		cfg.audit_enabled, cfg.rate_limit, cfg.blocklist_count);

	/* Default blocklist should have entries */
	ASSERT_GT(cfg.blocklist_count, 0);
}

TEST_F(thk, validate_safe_command)
{
	struct thk_exec_request req = {};
	struct thk_exec_result result = {};
	int ret;

	snprintf(req.command, sizeof(req.command), "ls -la /tmp");
	req.flags = THK_EXEC_F_DRYRUN;
	req.uid = getuid();

	ret = ioctl(self->fd, THK_IOC_EXEC_VALIDATE, &req);
	ASSERT_EQ(ret, 0);

	ret = ioctl(self->fd, THK_IOC_EXEC_STATUS, &result);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(result.result, THK_RESULT_OK);
}

TEST_F(thk, validate_blocked_command)
{
	struct thk_exec_request req = {};
	struct thk_exec_result result = {};
	int ret;

	snprintf(req.command, sizeof(req.command), "rm -rf /");
	req.flags = THK_EXEC_F_DRYRUN;
	req.uid = getuid();

	ret = ioctl(self->fd, THK_IOC_EXEC_VALIDATE, &req);
	ASSERT_EQ(ret, 0);

	ret = ioctl(self->fd, THK_IOC_EXEC_STATUS, &result);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(result.result, THK_RESULT_BLOCKED);

	TH_LOG("Blocked reason: %s", result.reason);
}

TEST_F(thk, validate_fork_bomb)
{
	struct thk_exec_request req = {};
	struct thk_exec_result result = {};
	int ret;

	snprintf(req.command, sizeof(req.command), ":(){ :|:& };:");
	req.flags = THK_EXEC_F_DRYRUN;
	req.uid = getuid();

	ret = ioctl(self->fd, THK_IOC_EXEC_VALIDATE, &req);
	ASSERT_EQ(ret, 0);

	ret = ioctl(self->fd, THK_IOC_EXEC_STATUS, &result);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(result.result, THK_RESULT_BLOCKED);
}

TEST_F(thk, validate_empty_command)
{
	struct thk_exec_request req = {};
	struct thk_exec_result result = {};
	int ret;

	req.command[0] = '\0';
	req.flags = THK_EXEC_F_DRYRUN;
	req.uid = getuid();

	ret = ioctl(self->fd, THK_IOC_EXEC_VALIDATE, &req);
	ASSERT_EQ(ret, 0);

	ret = ioctl(self->fd, THK_IOC_EXEC_STATUS, &result);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(result.result, THK_RESULT_INVALID);
}

TEST_F(thk, stats_increment)
{
	struct thk_stats before = {}, after = {};
	struct thk_exec_request req = {};
	int ret;

	/* Get stats before */
	ret = ioctl(self->fd, THK_IOC_GET_STATS, &before);
	ASSERT_EQ(ret, 0);

	/* Validate a command */
	snprintf(req.command, sizeof(req.command), "echo hello");
	req.flags = THK_EXEC_F_DRYRUN;
	req.uid = getuid();

	ret = ioctl(self->fd, THK_IOC_EXEC_VALIDATE, &req);
	ASSERT_EQ(ret, 0);

	/* Get stats after */
	ret = ioctl(self->fd, THK_IOC_GET_STATS, &after);
	ASSERT_EQ(ret, 0);

	ASSERT_EQ(after.total_requests, before.total_requests + 1);
	ASSERT_EQ(after.total_allowed, before.total_allowed + 1);
}

TEST_F(thk, invalid_ioctl)
{
	int ret;

	ret = ioctl(self->fd, _IO(THK_IOC_MAGIC, 0xFF), NULL);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, ENOTTY);
}

TEST_HARNESS_MAIN
