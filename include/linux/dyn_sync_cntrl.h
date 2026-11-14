/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_DYN_SYNC_CNTRL_H
#define _LINUX_DYN_SYNC_CNTRL_H

#include <linux/types.h>

struct task_struct;

/* Runtime defaults and version reported via sysfs */
#define DYN_FSYNC_ACTIVE_DEFAULT	true
#define DYN_FSYNC_VERSION_MAJOR		1
#define DYN_FSYNC_VERSION_MINOR		1

#ifdef CONFIG_DYNAMIC_FSYNC
extern bool suspend_active;
extern bool dyn_fsync_active;
extern void sync_filesystems(int nowait);
extern void inc_syscfs(struct task_struct *p);
#else
static const bool suspend_active = false;
static const bool dyn_fsync_active = false;
#endif

#endif /* _LINUX_DYN_SYNC_CNTRL_H */
