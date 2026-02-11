// SPDX-License-Identifier: GPL-2.0-only
/*
 * THK - LLM Command Assistant for Linux
 *
 * sysfs attributes: stats, config, version, blocklist
 */

#include <linux/device.h>
#include <linux/sysfs.h>

#include "thk.h"

/* /sys/devices/virtual/misc/thk/version */
static ssize_t version_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	return sysfs_emit(buf, "%d.%d.%d\n",
			  (THK_VERSION >> 16) & 0xff,
			  (THK_VERSION >> 8) & 0xff,
			  THK_VERSION & 0xff);
}
static DEVICE_ATTR_RO(version);

/* /sys/devices/virtual/misc/thk/stats */
static ssize_t stats_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct thk_device *tdev = thk_dev;
	s64 uptime = ktime_get_seconds() -
		     ktime_to_ns(tdev->load_time) / NSEC_PER_SEC;

	return sysfs_emit(buf,
			  "requests: %llu\n"
			  "allowed: %llu\n"
			  "blocked: %llu\n"
			  "rate_limited: %llu\n"
			  "uptime_secs: %lld\n",
			  atomic64_read(&tdev->total_requests),
			  atomic64_read(&tdev->total_allowed),
			  atomic64_read(&tdev->total_blocked),
			  atomic64_read(&tdev->total_rate_limited),
			  uptime);
}
static DEVICE_ATTR_RO(stats);

/* /sys/devices/virtual/misc/thk/audit_enabled */
static ssize_t audit_enabled_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct thk_device *tdev = thk_dev;
	u32 val;

	spin_lock(&tdev->config_lock);
	val = tdev->audit_enabled;
	spin_unlock(&tdev->config_lock);

	return sysfs_emit(buf, "%u\n", val);
}

static ssize_t audit_enabled_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct thk_device *tdev = thk_dev;
	u32 val;

	if (kstrtouint(buf, 10, &val) || val > 1)
		return -EINVAL;

	spin_lock(&tdev->config_lock);
	tdev->audit_enabled = val;
	spin_unlock(&tdev->config_lock);

	return count;
}
static DEVICE_ATTR_RW(audit_enabled);

/* /sys/devices/virtual/misc/thk/rate_limit */
static ssize_t rate_limit_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct thk_device *tdev = thk_dev;
	u32 val;

	spin_lock(&tdev->config_lock);
	val = tdev->rate_limit;
	spin_unlock(&tdev->config_lock);

	return sysfs_emit(buf, "%u\n", val);
}

static ssize_t rate_limit_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct thk_device *tdev = thk_dev;
	u32 val;

	if (kstrtouint(buf, 10, &val))
		return -EINVAL;

	spin_lock(&tdev->config_lock);
	tdev->rate_limit = val;
	spin_unlock(&tdev->config_lock);

	return count;
}
static DEVICE_ATTR_RW(rate_limit);

/* /sys/devices/virtual/misc/thk/blocklist */
static ssize_t blocklist_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct thk_device *tdev = thk_dev;
	ssize_t len = 0;
	unsigned int i;

	spin_lock(&tdev->config_lock);
	for (i = 0; i < tdev->blocklist_count && len < PAGE_SIZE - 1; i++)
		len += sysfs_emit_at(buf, len, "%s\n", tdev->blocklist[i]);
	spin_unlock(&tdev->config_lock);

	return len;
}
static DEVICE_ATTR_RO(blocklist);

static struct attribute *thk_attrs[] = {
	&dev_attr_version.attr,
	&dev_attr_stats.attr,
	&dev_attr_audit_enabled.attr,
	&dev_attr_rate_limit.attr,
	&dev_attr_blocklist.attr,
	NULL,
};

static const struct attribute_group thk_attr_group = {
	.attrs = thk_attrs,
};

int thk_sysfs_init(struct device *dev)
{
	return sysfs_create_group(&dev->kobj, &thk_attr_group);
}

void thk_sysfs_exit(struct device *dev)
{
	sysfs_remove_group(&dev->kobj, &thk_attr_group);
}
