/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * THK - LLM Command Assistant for Linux
 *
 * Internal kernel module header
 */
#ifndef _DRIVERS_MISC_THK_H
#define _DRIVERS_MISC_THK_H

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/ktime.h>
#include <uapi/misc/thk.h>

/* Rate limit tracking per UID */
#define THK_RATE_HASH_BITS	8
#define THK_RATE_HASH_SIZE	(1 << THK_RATE_HASH_BITS)
#define THK_RATE_WINDOW_SECS	60

struct thk_rate_entry {
	kuid_t uid;
	unsigned long count;
	ktime_t window_start;
	struct hlist_node node;
};

/* Global module state */
struct thk_device {
	/* Statistics (atomic) */
	atomic64_t total_requests;
	atomic64_t total_blocked;
	atomic64_t total_allowed;
	atomic64_t total_rate_limited;
	ktime_t load_time;

	/* Configuration (protected by config_lock) */
	spinlock_t config_lock;
	u32 audit_enabled;
	u32 rate_limit;		/* requests per minute per UID, 0=unlimited */

	/* Blocklist (protected by config_lock) */
	u32 blocklist_count;
	char blocklist[THK_MAX_BLOCKLIST_ENTRIES][THK_MAX_BLOCKLIST_PAT];

	/* Rate limit hash table (protected by rate_lock) */
	spinlock_t rate_lock;
	struct hlist_head rate_hash[THK_RATE_HASH_SIZE];

	/* Last validation result (per-open, but simplified to global) */
	spinlock_t result_lock;
	struct thk_exec_result last_result;
};

extern struct thk_device *thk_dev;

/* thk_exec.c */
int thk_exec_validate(struct thk_device *dev, struct thk_exec_request *req);
void thk_exec_init_blocklist(struct thk_device *dev);

/* thk_sysfs.c */
int thk_sysfs_init(struct device *dev);
void thk_sysfs_exit(struct device *dev);

#endif /* _DRIVERS_MISC_THK_H */
