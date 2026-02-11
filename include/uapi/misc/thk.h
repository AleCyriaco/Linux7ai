/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * THK - LLM Command Assistant for Linux
 *
 * UAPI header: ioctl interface, structs, constants
 */
#ifndef _UAPI_MISC_THK_H
#define _UAPI_MISC_THK_H

#ifdef __KERNEL__
#include <linux/ioctl.h>
#include <linux/types.h>
#else
#include <sys/ioctl.h>
#include <stdint.h>
typedef uint32_t __u32;
typedef uint64_t __u64;
#endif

#define THK_VERSION		0x00010000	/* 1.0.0 */
#define THK_NAME		"thk"

/* Maximum lengths */
#define THK_MAX_CMD_LEN		4096
#define THK_MAX_REASON_LEN	256
#define THK_MAX_BLOCKLIST_PAT	64
#define THK_MAX_BLOCKLIST_ENTRIES 128

/* Execution validation flags */
#define THK_EXEC_F_AUDIT	(1U << 0)	/* Log to audit subsystem */
#define THK_EXEC_F_DRYRUN	(1U << 1)	/* Validate only, don't mark exec */
#define THK_EXEC_F_FORCE	(1U << 2)	/* Skip blocklist (CAP_SYS_ADMIN) */

/* Validation result codes */
#define THK_RESULT_OK		0
#define THK_RESULT_BLOCKED	1
#define THK_RESULT_RATE_LIMITED	2
#define THK_RESULT_INVALID	3

/**
 * struct thk_exec_request - Command validation request
 * @command:	Command string to validate
 * @flags:	THK_EXEC_F_* flags
 * @uid:	UID of the requesting user
 */
struct thk_exec_request {
	char command[THK_MAX_CMD_LEN];
	__u32 flags;
	__u32 uid;
};

/**
 * struct thk_exec_result - Command validation result
 * @result:	THK_RESULT_* code
 * @flags:	Flags from the original request
 * @reason:	Human-readable reason if blocked
 */
struct thk_exec_result {
	__u32 result;
	__u32 flags;
	char reason[THK_MAX_REASON_LEN];
};

/**
 * struct thk_stats - Module statistics
 * @total_requests:	Total validation requests
 * @total_blocked:	Total commands blocked
 * @total_allowed:	Total commands allowed
 * @total_rate_limited:	Total rate-limited requests
 * @uptime_secs:	Seconds since module load
 */
struct thk_stats {
	__u64 total_requests;
	__u64 total_blocked;
	__u64 total_allowed;
	__u64 total_rate_limited;
	__u64 uptime_secs;
};

/**
 * struct thk_config - Runtime configuration
 * @audit_enabled:	Whether audit logging is active
 * @rate_limit:		Max requests per minute per UID (0 = unlimited)
 * @blocklist_count:	Number of active blocklist patterns
 */
struct thk_config {
	__u32 audit_enabled;
	__u32 rate_limit;
	__u32 blocklist_count;
	__u32 reserved;
};

/* ioctl magic number 0xBB */
#define THK_IOC_MAGIC		0xBB

#define THK_IOC_EXEC_VALIDATE	_IOW(THK_IOC_MAGIC, 0x01, struct thk_exec_request)
#define THK_IOC_EXEC_STATUS	_IOR(THK_IOC_MAGIC, 0x02, struct thk_exec_result)
#define THK_IOC_GET_STATS	_IOR(THK_IOC_MAGIC, 0x03, struct thk_stats)
#define THK_IOC_GET_CONFIG	_IOR(THK_IOC_MAGIC, 0x04, struct thk_config)
#define THK_IOC_SET_CONFIG	_IOW(THK_IOC_MAGIC, 0x05, struct thk_config)
#define THK_IOC_VERSION		_IOR(THK_IOC_MAGIC, 0x06, __u32)

#endif /* _UAPI_MISC_THK_H */
