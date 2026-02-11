// SPDX-License-Identifier: GPL-2.0-only
/*
 * THK - LLM Command Assistant for Linux
 *
 * Main module: misc_register, file_ops, ioctl dispatch
 */

#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "thk.h"

struct thk_device *thk_dev;

static int thk_open(struct inode *inode, struct file *file)
{
	file->private_data = thk_dev;
	return 0;
}

static int thk_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long thk_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct thk_device *dev = file->private_data;
	void __user *uarg = (void __user *)arg;
	int ret;

	switch (cmd) {
	case THK_IOC_VERSION: {
		u32 version = THK_VERSION;

		if (copy_to_user(uarg, &version, sizeof(version)))
			return -EFAULT;
		return 0;
	}

	case THK_IOC_EXEC_VALIDATE: {
		struct thk_exec_request req;

		if (copy_from_user(&req, uarg, sizeof(req)))
			return -EFAULT;

		/* Ensure null termination */
		req.command[THK_MAX_CMD_LEN - 1] = '\0';

		ret = thk_exec_validate(dev, &req);
		return ret;
	}

	case THK_IOC_EXEC_STATUS: {
		struct thk_exec_result result;

		spin_lock(&dev->result_lock);
		result = dev->last_result;
		spin_unlock(&dev->result_lock);

		if (copy_to_user(uarg, &result, sizeof(result)))
			return -EFAULT;
		return 0;
	}

	case THK_IOC_GET_STATS: {
		struct thk_stats stats;

		stats.total_requests = atomic64_read(&dev->total_requests);
		stats.total_blocked = atomic64_read(&dev->total_blocked);
		stats.total_allowed = atomic64_read(&dev->total_allowed);
		stats.total_rate_limited = atomic64_read(&dev->total_rate_limited);
		stats.uptime_secs = ktime_get_seconds() -
				    ktime_to_ns(dev->load_time) / NSEC_PER_SEC;

		if (copy_to_user(uarg, &stats, sizeof(stats)))
			return -EFAULT;
		return 0;
	}

	case THK_IOC_GET_CONFIG: {
		struct thk_config cfg;

		spin_lock(&dev->config_lock);
		cfg.audit_enabled = dev->audit_enabled;
		cfg.rate_limit = dev->rate_limit;
		cfg.blocklist_count = dev->blocklist_count;
		cfg.reserved = 0;
		spin_unlock(&dev->config_lock);

		if (copy_to_user(uarg, &cfg, sizeof(cfg)))
			return -EFAULT;
		return 0;
	}

	case THK_IOC_SET_CONFIG: {
		struct thk_config cfg;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (copy_from_user(&cfg, uarg, sizeof(cfg)))
			return -EFAULT;

		spin_lock(&dev->config_lock);
		dev->audit_enabled = cfg.audit_enabled;
		dev->rate_limit = cfg.rate_limit;
		spin_unlock(&dev->config_lock);

		return 0;
	}

	default:
		return -ENOTTY;
	}
}

static const struct file_operations thk_fops = {
	.owner		= THIS_MODULE,
	.open		= thk_open,
	.release	= thk_release,
	.unlocked_ioctl	= thk_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
};

static struct miscdevice thk_misc = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= THK_NAME,
	.fops	= &thk_fops,
	.mode	= 0666,
};

static int __init thk_init(void)
{
	int ret;

	thk_dev = kzalloc(sizeof(*thk_dev), GFP_KERNEL);
	if (!thk_dev)
		return -ENOMEM;

	spin_lock_init(&thk_dev->config_lock);
	spin_lock_init(&thk_dev->rate_lock);
	spin_lock_init(&thk_dev->result_lock);

	thk_dev->audit_enabled = 1;
	thk_dev->rate_limit = 10;
	thk_dev->load_time = ktime_get();

	thk_exec_init_blocklist(thk_dev);

	ret = misc_register(&thk_misc);
	if (ret) {
		pr_err("thk: failed to register misc device: %d\n", ret);
		kfree(thk_dev);
		return ret;
	}

	ret = thk_sysfs_init(thk_misc.this_device);
	if (ret) {
		misc_deregister(&thk_misc);
		kfree(thk_dev);
		return ret;
	}

	pr_info("thk: LLM Command Assistant v%d.%d.%d loaded\n",
		(THK_VERSION >> 16) & 0xff,
		(THK_VERSION >> 8) & 0xff,
		THK_VERSION & 0xff);

	return 0;
}

static void __exit thk_exit(void)
{
	thk_sysfs_exit(thk_misc.this_device);
	misc_deregister(&thk_misc);
	kfree(thk_dev);
	pr_info("thk: unloaded\n");
}

module_init(thk_init);
module_exit(thk_exit);

MODULE_AUTHOR("Linux7ai");
MODULE_DESCRIPTION("THK - LLM Command Assistant kernel support");
MODULE_LICENSE("GPL");
