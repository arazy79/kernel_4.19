// SPDX-License-Identifier: GPL-2.0-only
/*
 * Auto-cut charging for X00TD (4.19)
 * Target: pc_port (main USB charger), fallback usb (USB-PD)
 * All params writable via /sys/module/autocut/parameters/ (root)
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/workqueue.h>
#include <linux/power_supply.h>

static int max_soc = 100;
static int min_soc = 90;
static int enabled = 1;

module_param(max_soc, int, 0644);
MODULE_PARM_DESC(max_soc, "Stop charging at this SOC %%");
module_param(min_soc, int, 0644);
MODULE_PARM_DESC(min_soc, "Resume charging at this SOC %%");
module_param(enabled, int, 0644);
MODULE_PARM_DESC(enabled, "1=on, 0=off (charging forced resume)");

static struct delayed_work autocut_work;
static int last_usb_present = -1;

static void autocut_set_suspend(struct power_supply *chg_psy, int suspend)
{
	union power_supply_propval val;
	int ret;

	if (!chg_psy)
		return;

	val.intval = suspend;
	ret = power_supply_set_property(chg_psy,
					POWER_SUPPLY_PROP_INPUT_SUSPEND, &val);
	if (ret)
		pr_err("autocut: set suspend=%d failed (%d)\n", suspend, ret);
}

static void autocut_work_fn(struct work_struct *work)
{
	struct power_supply *batt_psy = NULL, *chg_psy = NULL;
	union power_supply_propval val;
	int ret, capacity = -1, usb_present = 0;

	/* Primary: pc_port (main charger), fallback: usb (PD) */
	chg_psy = power_supply_get_by_name("pc_port");
	if (!chg_psy) {
		chg_psy = power_supply_get_by_name("usb");
		if (chg_psy)
			pr_info_once("autocut: using usb (PD) as charger\n");
	}

	if (chg_psy) {
		ret = power_supply_get_property(chg_psy,
						POWER_SUPPLY_PROP_PRESENT, &val);
		if (!ret)
			usb_present = val.intval;
	}

	batt_psy = power_supply_get_by_name("battery");
	if (batt_psy) {
		ret = power_supply_get_property(batt_psy,
						POWER_SUPPLY_PROP_CAPACITY, &val);
		if (!ret)
			capacity = val.intval;
	}

	/* MODULE DISABLED: force resume, skip all logic */
	if (!enabled) {
		autocut_set_suspend(chg_psy, 0);
		last_usb_present = usb_present;
		goto out;
	}

	/* Charger just plugged in: always resume first */
	if (usb_present == 1 && last_usb_present != 1) {
		autocut_set_suspend(chg_psy, 0);
		pr_info("autocut: plugged, force resume (soc=%d)\n", capacity);
	} else if (usb_present == 1 && capacity >= 0) {
		if (capacity >= max_soc) {
			autocut_set_suspend(chg_psy, 1);
			pr_info("autocut: STOP at %d%% (max=%d)\n",
				capacity, max_soc);
		} else if (capacity <= min_soc) {
			autocut_set_suspend(chg_psy, 0);
			pr_info("autocut: RESUME at %d%% (min=%d)\n",
				capacity, min_soc);
		}
		/* between min and max: hold current state */
	}

	last_usb_present = usb_present;

out:
	if (chg_psy)
		power_supply_put(chg_psy);
	if (batt_psy)
		power_supply_put(batt_psy);

	schedule_delayed_work(&autocut_work, msecs_to_jiffies(3000));
}

static int __init autocut_init(void)
{
	INIT_DELAYED_WORK(&autocut_work, autocut_work_fn);
	/* wait 5s for power_supply devices to register at boot */
	schedule_delayed_work(&autocut_work, msecs_to_jiffies(5000));
	pr_info("autocut: loaded (max=%d min=%d enabled=%d)\n",
		max_soc, min_soc, enabled);
	return 0;
}

static void __exit autocut_exit(void)
{
	cancel_delayed_work_sync(&autocut_work);
	pr_info("autocut: unloaded\n");
}

module_init(autocut_init);
module_exit(autocut_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Auto-cut charging for X00TD (pc_port target)");
MODULE_AUTHOR("X00TD Porter");
