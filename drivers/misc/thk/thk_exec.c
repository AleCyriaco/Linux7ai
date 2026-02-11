// SPDX-License-Identifier: GPL-2.0-only
/*
 * THK - LLM Command Assistant for Linux
 *
 * Command validation, audit logging, blocklist enforcement
 */

#include <linux/string.h>
#include <linux/cred.h>
#include <linux/slab.h>
#include <linux/jhash.h>
#include <linux/audit.h>

#include "thk.h"

/* Default blocklist patterns for dangerous commands */
static const char * const default_blocklist[] = {
	"rm -rf /",
	"rm -rf /*",
	":(){ :|:& };:",		/* fork bomb */
	"dd if=/dev/zero of=/dev/sd",
	"dd if=/dev/random of=/dev/sd",
	"mkfs.",			/* mkfs on any device */
	"> /dev/sd",
	"chmod -R 777 /",
	"chown -R",
	"mv /* /dev/null",
	"wget|sh",
	"curl|sh",
	"wget|bash",
	"curl|bash",
	"\\x",				/* hex-encoded shellcode */
	"/dev/tcp/",			/* reverse shells */
	"nc -e",
	"ncat -e",
	"python -c.*import.*socket",
	"perl -e.*socket",
};

void thk_exec_init_blocklist(struct thk_device *dev)
{
	unsigned int i;

	spin_lock(&dev->config_lock);
	for (i = 0; i < ARRAY_SIZE(default_blocklist) &&
		    i < THK_MAX_BLOCKLIST_ENTRIES; i++) {
		strscpy(dev->blocklist[i], default_blocklist[i],
			THK_MAX_BLOCKLIST_PAT);
	}
	dev->blocklist_count = i;
	spin_unlock(&dev->config_lock);
}

static bool thk_check_blocklist(struct thk_device *dev, const char *cmd,
				char *reason, size_t reason_len)
{
	unsigned int i;

	spin_lock(&dev->config_lock);
	for (i = 0; i < dev->blocklist_count; i++) {
		if (strnstr(cmd, dev->blocklist[i], THK_MAX_CMD_LEN)) {
			snprintf(reason, reason_len,
				 "blocked: matches pattern '%s'",
				 dev->blocklist[i]);
			spin_unlock(&dev->config_lock);
			return true;
		}
	}
	spin_unlock(&dev->config_lock);
	return false;
}

static u32 thk_uid_hash(kuid_t uid)
{
	return jhash_1word(from_kuid_munged(&init_user_ns, uid),
			   0) & (THK_RATE_HASH_SIZE - 1);
}

static bool thk_check_rate_limit(struct thk_device *dev, kuid_t uid)
{
	u32 hash = thk_uid_hash(uid);
	struct thk_rate_entry *entry;
	ktime_t now = ktime_get();
	bool limited = false;
	u32 limit;

	spin_lock(&dev->config_lock);
	limit = dev->rate_limit;
	spin_unlock(&dev->config_lock);

	if (limit == 0)
		return false;

	spin_lock(&dev->rate_lock);
	hlist_for_each_entry(entry, &dev->rate_hash[hash], node) {
		if (uid_eq(entry->uid, uid)) {
			s64 elapsed = ktime_to_ns(ktime_sub(now,
						entry->window_start));

			if (elapsed > (s64)THK_RATE_WINDOW_SECS * NSEC_PER_SEC) {
				entry->window_start = now;
				entry->count = 1;
			} else if (entry->count >= limit) {
				limited = true;
			} else {
				entry->count++;
			}
			spin_unlock(&dev->rate_lock);
			return limited;
		}
	}

	/* New UID, allocate entry */
	entry = kzalloc(sizeof(*entry), GFP_ATOMIC);
	if (entry) {
		entry->uid = uid;
		entry->count = 1;
		entry->window_start = now;
		hlist_add_head(&entry->node, &dev->rate_hash[hash]);
	}
	spin_unlock(&dev->rate_lock);
	return false;
}

static void thk_audit_log(const char *cmd, kuid_t uid, u32 result)
{
#ifdef CONFIG_THK_AUDIT
	struct audit_buffer *ab;

	ab = audit_log_start(audit_context(), GFP_KERNEL, AUDIT_USER_CMD);
	if (!ab)
		return;

	audit_log_format(ab, "thk: uid=%u cmd=\"%.256s\" result=%s",
			 from_kuid_munged(&init_user_ns, uid),
			 cmd,
			 result == THK_RESULT_OK ? "allowed" :
			 result == THK_RESULT_BLOCKED ? "blocked" :
			 result == THK_RESULT_RATE_LIMITED ? "rate_limited" :
			 "invalid");
	audit_log_end(ab);
#endif
}

int thk_exec_validate(struct thk_device *dev, struct thk_exec_request *req)
{
	struct thk_exec_result result = {};
	kuid_t uid = make_kuid(&init_user_ns, req->uid);
	bool blocked, rate_limited;

	atomic64_inc(&dev->total_requests);

	/* Validate the command is not empty */
	if (req->command[0] == '\0') {
		result.result = THK_RESULT_INVALID;
		strscpy(result.reason, "empty command", THK_MAX_REASON_LEN);
		goto out;
	}

	/* Check rate limit */
	rate_limited = thk_check_rate_limit(dev, uid);
	if (rate_limited) {
		atomic64_inc(&dev->total_rate_limited);
		result.result = THK_RESULT_RATE_LIMITED;
		strscpy(result.reason, "rate limit exceeded",
			THK_MAX_REASON_LEN);
		goto out;
	}

	/* Check blocklist (skip if FORCE and CAP_SYS_ADMIN) */
	if (!(req->flags & THK_EXEC_F_FORCE) || !capable(CAP_SYS_ADMIN)) {
		blocked = thk_check_blocklist(dev, req->command,
					      result.reason,
					      THK_MAX_REASON_LEN);
		if (blocked) {
			atomic64_inc(&dev->total_blocked);
			result.result = THK_RESULT_BLOCKED;
			goto out;
		}
	}

	/* Command is allowed */
	atomic64_inc(&dev->total_allowed);
	result.result = THK_RESULT_OK;
	result.flags = req->flags;

out:
	/* Audit log if enabled */
	spin_lock(&dev->config_lock);
	if (dev->audit_enabled && (req->flags & THK_EXEC_F_AUDIT))
		thk_audit_log(req->command, uid, result.result);
	spin_unlock(&dev->config_lock);

	/* Store last result */
	spin_lock(&dev->result_lock);
	dev->last_result = result;
	spin_unlock(&dev->result_lock);

	return 0;
}
