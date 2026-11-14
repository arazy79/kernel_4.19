/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_DYN_SYNC_CNTRL_H
#define _LINUX_DYN_SYNC_CNTRL_H

#include <linux/types.h>

#ifdef CONFIG_DYNAMIC_FSYNC
extern bool suspend_active;
extern bool dyn_fsync_active;
#else
static const bool suspend_active = false;
static const bool dyn_fsync_active = false;
#endif

#endif /* _LINUX_DYN_SYNC_CNTRL_H */
