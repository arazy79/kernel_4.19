// SPDX-License-Identifier: GPL-2.0-only
/*
 * Auto-cut charging for X00TD (kernel 4.19, qpnp-smb2 / smblib)
 *
 * Fitur:
 *  - Stop charging saat capacity >= max_soc
 *  - Resume charging saat capacity <= min_soc
 *  - Hysteresis: di antara min..max, biarkan state apa adanya
 *  - Enable/disable runtime via /sys/module/autocut/parameters/autocut_enabled
 *  - Pakai POWER_SUPPLY_PROP_CTM_CURRENT_MAX (CTM_VOTER), jadi TIDAK
 *    rebutan dengan ROM charging-limit slider (yang pakai USER_VOTER).
 *    usb_icl_votable pakai VOTE_MIN -> min dari semua vote menang.
 *  - Debounce USB present -> cegah false "force resume" saat VBUS glitch.
 *  - Reset state saat unplug, jadi plug berikutnya fresh.
 *  - Saat disable atau unload, CTM voter di-release (set -1) supaya
 *    charging kembali 100% dikontrol ROM/driver.
 *
 * Runtime control:
 *   echo 0 > /sys/module/autocut/parameters/autocut_enabled   # off
 *   echo 1 > /sys/module/autocut/parameters/autocut_enabled   # on
 *   echo 95 > /sys/module/autocut/parameters/min_soc
 *   echo 100 > /sys/module/autocut/parameters/max_soc
 *   cat /sys/module/autocut/parameters/autocut_enabled
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/workqueue.h>
#include <linux/power_supply.h>
#include <linux/delay.h>

/* ---------------- Tunables ---------------- */

static int  max_soc         = 100;
static int  min_soc         = 90;
static bool autocut_enabled = true;

module_param(max_soc, int, 0644);
MODULE_PARM_DESC(max_soc, "SOC (%) to stop charging (default 100)");

module_param(min_soc, int, 0644);
MODULE_PARM_DESC(min_soc, "SOC (%) to resume charging (default 90)");

module_param(autocut_enabled, bool, 0644);
MODULE_PARM_DESC(autocut_enabled, "1=aktif, 0=nonaktif (default 1)");

/* ---------------- Internal state ---------------- */

static struct delayed_work autocut_work;
static bool suspended;       /* autocut sedang hold charging? */
static bool was_connected;   /* transisi unplug -> plug */

/*
 * Set CTM voter:
 *   ua = -1  -> release (tidak suspend dari autocut)
 *   ua =  0  -> paksa 0mA -> effective USB_ICL = 0 -> hardware suspend
 * Nilai lain tidak dipakai supaya kita tidak pernah masuk jalur
 * forced_current() di smblib_set_icl_current() yang selalu un-suspend.
 */
static int set_ctm(int ua)
{
	struct power_supply *usb;
	union power_supply_propval val;
	int ret;

	usb = power_supply_get_by_name("usb");
	if (!usb)
		return -ENODEV;

	val.intval = ua;
	ret = power_supply_set_property(usb,
			POWER_SUPPLY_PROP_CTM_CURRENT_MAX, &val);
	if (ret < 0)
		pr_warn("autocut: CTM set %d failed rc=%d\n", ua, ret);
	power_supply_put(usb);
	return ret;
}

/* Baca PRESENT dua kali dengan jeda; harus dua-duanya 1 */
static int usb_present_stable(void)
{
	struct power_supply *usb;
	union power_supply_propval val;
	int a = 0, b = 0;

	usb = power_supply_get_by_name("usb");
	if (!usb)
		return 0;

	if (!power_supply_get_property(usb, POWER_SUPPLY_PROP_PRESENT, &val))
		a = !!val.intval;

	msleep(100);

	if (!power_supply_get_property(usb, POWER_SUPPLY_PROP_PRESENT, &val))
		b = !!val.intval;

	power_supply_put(usb);
	return (a && b) ? 1 : 0;
}

static int get_capacity(void)
{
	struct power_supply *batt;
	union power_supply_propval val;
	int cap = -1;

	batt = power_supply_get_by_name("battery");
	if (!batt)
		return -1;

	if (!power_supply_get_property(batt, POWER_SUPPLY_PROP_CAPACITY, &val))
		cap = val.intval;
	power_supply_put(batt);
	return cap;
}

static void autocut_work_fn(struct work_struct *work)
{
	int present, cap;

	/* ---- Fitur di-disable dari userspace ---- */
	if (!autocut_enabled) {
		if (suspended) {
			set_ctm(-1);
			suspended = false;
			pr_info("autocut: disabled -> release CTM\n");
		}
		goto resched;
	}

	/* ---- Deteksi USB dengan debounce ---- */
	present = usb_present_stable();

	if (!present) {
		if (was_connected) {
			was_connected = false;
			suspended = false;
			set_ctm(-1);
			pr_info("autocut: unplugged, state reset\n");
		}
		goto resched;
	}

	cap = get_capacity();
	if (cap < 0)
		goto resched;

	/* ---- Fresh plug-in ---- */
	if (!was_connected) {
		was_connected = true;
		suspended = false;
		set_ctm(-1);            /* serahkan dulu ke ROM/driver */
		pr_info("autocut: plug-in (cap=%d), CTM released\n", cap);
	}

	/* ---- State machine dengan HOLD enforcement ---- */
	if (!suspended && cap >= max_soc) {
		if (set_ctm(0) == 0) {
			suspended = true;
			pr_info("autocut: STOP at %d%% (max=%d)\n",
				cap, max_soc);
		}
	} else if (suspended && cap <= min_soc) {
		if (set_ctm(-1) == 0) {
			suspended = false;
			pr_info("autocut: RESUME at %d%% (min=%d)\n",
				cap, min_soc);
		}
	} else if (suspended) {
		/*
		 * HOLD: re-assert tiap tick. Kalau ada voter lain
		 * (JEITA FORCE_FAST_CHARGE, thermal, dll) yang coba
		 * un-suspend, kita tetap menang karena VOTE_MIN.
		 */
		set_ctm(0);
	}
	/* else: belum suspend, cap < max -> biarkan charging */

resched:
	schedule_delayed_work(&autocut_work, msecs_to_jiffies(3000));
}

static int __init autocut_init(void)
{
	if (min_soc > max_soc) {
		pr_err("autocut: min_soc(%d) > max_soc(%d), swap?\n",
			min_soc, max_soc);
		return -EINVAL;
	}

	INIT_DELAYED_WORK(&autocut_work, autocut_work_fn);
	schedule_delayed_work(&autocut_work, msecs_to_jiffies(3000));

	pr_info("autocut: loaded (enabled=%d max=%d min=%d voter=CTM)\n",
		autocut_enabled, max_soc, min_soc);
	return 0;
}

static void __exit autocut_exit(void)
{
	cancel_delayed_work_sync(&autocut_work);
	set_ctm(-1);            /* restore charging saat unload */
	pr_info("autocut: unloaded (CTM released)\n");
}

module_init(autocut_init);
module_exit(autocut_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Auto-cut charging via CTM_CURRENT_MAX, coexists with ROM limit");
MODULE_AUTHOR("X00TD Porter");
