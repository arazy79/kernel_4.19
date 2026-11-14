// SPDX-License-Identifier: GPL-2.0
/*
 * Dynamic sync control driver V2
 *
 * by andip71 (alias Lord Boeffla)
 *
 * All credits for original implementation to faux123
 *
 * Ported to SDM660 / kernel 4.19:
 *   - state_notifier  ->  PM notifier
 *   - added inc_syscfs() stub (no Samsung syscfs counter on this platform)
 */

#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/writeback.h>
#include <linux/suspend.h>
#include <linux/sched.h>
#include <linux/dyn_sync_cntrl.h>

// fsync_mutex protects dyn_fsync_active during suspend / late resume transitions
static DEFINE_MUTEX(fsync_mutex);


// Declarations

bool suspend_active = false;
bool dyn_fsync_active = DYN_FSYNC_ACTIVE_DEFAULT;

extern void sync_filesystems(int wait);

/*
 * Stub for Samsung's syscfs accounting.
 * fs/sync.c (patched) calls inc_syscfs(current) inside do_fsync(),
 * but kernel 4.19 mainline / SDM660 has no such counter.
 */
void inc_syscfs(struct task_struct *p)
{
	/* no-op */
}
EXPORT_SYMBOL(inc_syscfs);


// Functions

static void dyn_fsync_force_flush(void)
{
	sync_filesystems(0);
	sync_filesystems(1);
}


static ssize_t dyn_fsync_active_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", (dyn_fsync_active ? 1 : 0));
}


static ssize_t dyn_fsync_active_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;

	if (sscanf(buf, "%u\n", &data) == 1)
	{
		if (data == 1)
		{
			pr_info("%s: dynamic fsync enabled\n", __func__);
			dyn_fsync_active = true;
		}
		else if (data == 0)
		{
			pr_info("%s: dynamic fsync disabled\n", __func__);
			dyn_fsync_active = false;

			// force a flush
			dyn_fsync_force_flush();
		}
		else
			pr_info("%s: bad value: %u\n", __func__, data);
	}
	else
		pr_info("%s: unknown input!\n", __func__);

	return count;
}


static ssize_t dyn_fsync_version_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "version: %u.%u\n",
		DYN_FSYNC_VERSION_MAJOR,
		DYN_FSYNC_VERSION_MINOR);
}


static ssize_t dyn_fsync_suspend_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "suspend active: %u\n", suspend_active);
}


static int dyn_fsync_panic_event(struct notifier_block *this,
		unsigned long event, void *ptr)
{
	// kernel panic, force flush now
	suspend_active = false;
	dyn_fsync_force_flush();
	pr_warn("dynamic fsync: panic - force flush!\n");

	return NOTIFY_DONE;
}


static int dyn_fsync_notify_sys(struct notifier_block *this, unsigned long code,
				void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT || code == SYS_POWER_OFF)
	{
		// system shutdown or reboot, disable dynamic fsync and force flush
		suspend_active = false;
		dyn_fsync_active = false;
		dyn_fsync_force_flush();
		pr_warn("dynamic fsync: reboot - force flush!\n");
	}
	return NOTIFY_DONE;
}

/*
 * PM notifier replacing Samsung's state_notifier.
 *
 *   PM_SUSPEND_PREPARE : device is about to enter suspend
 *                        -> mark suspend_active, fsync will be skipped
 *   PM_POST_SUSPEND    : device resumed
 *                        -> clear suspend_active, flush if still enabled
 */
static int dyn_fsync_pm_notify(struct notifier_block *nb,
				unsigned long mode, void *unused)
{
	switch (mode)
	{
		case PM_SUSPEND_PREPARE:
			mutex_lock(&fsync_mutex);
			suspend_active = true;
			mutex_unlock(&fsync_mutex);
			break;

		case PM_POST_SUSPEND:
			mutex_lock(&fsync_mutex);
			suspend_active = false;

			if (dyn_fsync_active)
			{
				dyn_fsync_force_flush();
			}

			mutex_unlock(&fsync_mutex);
			break;

		default:
			break;
	}

	return NOTIFY_OK;
}

// Module structures

static struct notifier_block dyn_fsync_notifier =
{
	.notifier_call = dyn_fsync_notify_sys,
};

static struct notifier_block dyn_fsync_pm_nb =
{
	.notifier_call = dyn_fsync_pm_notify,
};

static struct kobj_attribute dyn_fsync_active_attribute =
	__ATTR(Dyn_fsync_active, 0664,
		dyn_fsync_active_show,
		dyn_fsync_active_store);

static struct kobj_attribute dyn_fsync_version_attribute =
	__ATTR(Dyn_fsync_version, 0444, dyn_fsync_version_show, NULL);

static struct kobj_attribute dyn_fsync_suspend_attribute =
	__ATTR(Dyn_fsync_suspend, 0444, dyn_fsync_suspend_show, NULL);

static struct attribute *dyn_fsync_active_attrs[] =
{
	&dyn_fsync_active_attribute.attr,
	&dyn_fsync_version_attribute.attr,
	&dyn_fsync_suspend_attribute.attr,
	NULL,
};

static struct attribute_group dyn_fsync_active_attr_group =
{
	.attrs = dyn_fsync_active_attrs,
};

static struct notifier_block dyn_fsync_panic_block =
{
	.notifier_call  = dyn_fsync_panic_event,
	.priority       = INT_MAX,
};

static struct kobject *dyn_fsync_kobj;


// Module init/exit

static int __init dyn_fsync_init(void)
{
	int sysfs_result;

	register_reboot_notifier(&dyn_fsync_notifier);

	atomic_notifier_chain_register(&panic_notifier_list,
		&dyn_fsync_panic_block);

	dyn_fsync_kobj = kobject_create_and_add("dyn_fsync", kernel_kobj);

	if (!dyn_fsync_kobj)
	{
		pr_err("%s dyn_fsync_kobj create failed!\n", __func__);
		return -ENOMEM;
	}

	sysfs_result = sysfs_create_group(dyn_fsync_kobj,
			&dyn_fsync_active_attr_group);

	if (sysfs_result)
	{
		pr_err("%s dyn_fsync sysfs create failed!\n", __func__);
		kobject_put(dyn_fsync_kobj);
	}

	if (register_pm_notifier(&dyn_fsync_pm_nb) != 0)
	{
		pr_err("%s: Failed to register PM notifier\n", __func__);

		unregister_reboot_notifier(&dyn_fsync_notifier);

		atomic_notifier_chain_unregister(&panic_notifier_list,
			&dyn_fsync_panic_block);

		if (dyn_fsync_kobj != NULL)
			kobject_put(dyn_fsync_kobj);

		return -EFAULT;
	}

	pr_info("%s dynamic fsync initialisation complete\n", __func__);

	return sysfs_result;
}


static void __exit dyn_fsync_exit(void)
{
	unregister_reboot_notifier(&dyn_fsync_notifier);

	atomic_notifier_chain_unregister(&panic_notifier_list,
		&dyn_fsync_panic_block);

	unregister_pm_notifier(&dyn_fsync_pm_nb);

	if (dyn_fsync_kobj != NULL)
		kobject_put(dyn_fsync_kobj);

	pr_info("%s dynamic fsync unregistration complete\n", __func__);
}

module_init(dyn_fsync_init);
module_exit(dyn_fsync_exit);

MODULE_AUTHOR("andip71");
MODULE_DESCRIPTION("dynamic fsync - automatic fs sync optimization ported to SDM660 / kernel 4.19");
MODULE_LICENSE("GPL v2");
