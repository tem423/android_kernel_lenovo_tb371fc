/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
*/

#ifndef _MTK_BATTERY_H
#define _MTK_BATTERY_H

#ifndef _DEA_MODIFY_
#include <linux/list.h>
#include <linux/notifier.h>
#else
#include "simulator_kernel.h"
#endif
/* Spruce code for OSPURCET-3 by zhangjb18 at 2022.11.26 start */
#include <linux/power_supply.h>
/* Spruce code for OSPURCET-3 by zhangjb18 at 2022.11.26 end */


/* ============================================================ */
/* typedef */
/* ============================================================ */

/* coulomb service */
struct gauge_consumer {
	char *name;
	struct device *dev;
	long start;
	long end;
	int variable;

	int (*callback)(struct gauge_consumer *gc);
	struct list_head list;
};

extern void gauge_coulomb_service_init(void);
extern void gauge_coulomb_consumer_init(struct gauge_consumer *coulomb,
	struct device *dev, char *name);
extern void gauge_coulomb_start(struct gauge_consumer *coulomb, int car);
extern void gauge_coulomb_stop(struct gauge_consumer *coulomb);
extern void gauge_coulomb_dump_list(void);
extern void gauge_coulomb_before_reset(void);
extern void gauge_coulomb_after_reset(void);
extern void gauge_coulomb_set_log_level(int x);
/* coulomb sub system end */


/* battery notify charger_consumer */
enum {
	EVENT_BATTERY_PLUG_OUT,
};

extern int register_battery_notifier(struct notifier_block *nb);
extern int unregister_battery_notifier(struct notifier_block *nb);
/* battery notify charger_consumer end*/


/* battery common interface */
extern signed int battery_get_bat_voltage(void);
extern signed int battery_get_bat_current(void);
extern signed int battery_get_bat_current_mA(void);
extern signed int battery_get_soc(void);
extern signed int battery_get_uisoc(void);
extern signed int battery_get_bat_temperature(void);
extern signed int battery_get_ibus(void);
extern signed int battery_get_vbus(void);
extern signed int battery_get_bat_avg_current(void);


/* Spruce code for OSPURCET-3 by zhangjb18 at 2022.11.26 start */
extern int mm8013_get_info(enum power_supply_property info_type, int *val);
extern int mm8013_set_info(enum power_supply_property info_type, int val);
/* Spruce code for OSPURCET-3 by zhangjb18 at 2022.11.26 end */
/* Spruce code for OSPURCET-6 by zhangjb18 at 2022.11.29 start */
extern int dual_swchg_get_gauge_info(enum power_supply_property psp, u32 *ival);
/* Spruce code for OSPURCET-6 by zhangjb18 at 2022.11.29 end */



#endif /* End of _FUEL_GAUGE_GM_30_H */
