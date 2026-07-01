/*
 * Huaqin  Inc. (C) 2011. All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef __HARDINFO_CHARGER_H__
#define __HARDINFO_CHARGER_H__

#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/fs.h>

enum hardware_id {
    HRADINFO_MAIN_BUCK_INFO = 0,
    HRADINFO_BUCK_CHG_INFO,
    HRADINFO_CHG_PUMP_INFO,
    HRADINFO_GAUGE_INFO,
    HRADINFO_REAL_TYPE,
    HRADINFO_HIZ_EN,
/* Spruce code for OSPURCET-450 by zhangjb18 at 2022.12.05 start */
    HRADINFO_CHARGING_ENABLED,
    HRADINFO_VBUS_NOW,
    HRADINFO_IBUS_NOW,
/* Spruce code for OSPURCET-450 by zhangjb18 at 2022.12.05 end */
/* Spruce code for OSPURCET-33 by zhangjb18 at 2022.12.12 start */
    BATTERY_MAINTENANCE_ENABLE,
    BATTERY_PROTECTION_ENABLE,
    BATTERY_MAINTENANCE_FAKE_CYCLE,
    BATTERY_FAKE_TEMP,
/* Spruce code for OSPURCET-33 by zhangjb18 at 2022.12.12 end */
/* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 start */
    BATTERY_THERMAL_CONTROL_LIMIT,
    BATTERY_THERMAL_CONTROL_LIMIT_MAX,
/* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 start */
/* Spruce code for OSPURCET-463 by zhangjb18 at 2022.12.28 start */
    HRADINFO_MAIN_CHARGING_CURRENT,
    HRADINFO_SLAVE_CHARGING_CURRENT,
    HRADINFO_CHGPUMP_CHARGING_CURRENT,
/* Spruce code for OSPURCET-463 by zhangjb18 at 2022.12.28 end */
};

enum hardinfo_vendor_chg_info {
    //main
    MT6360 = 0,
    //pl
    SGM41513,
    SC8560,
    //cp
    SC8549,
    SGM41600,
    //battery
    SCUD,
    SUNWODA,
};

/* Spruce code for OSPURCET-33 by zhangjb18 at 2022.12.12 start */
enum hardinfo_aging_level {
    HW_LEVEL_0 = 0,
    HW_LEVEL_1,
    HW_LEVEL_2,
    HW_LEVEL_3,
};
/* Spruce code for OSPURCET-33 by zhangjb18 at 2022.12.12 end */

#define HARDINFO_ERROR -1

extern void hardinfo_set_vendor_chginfo(enum hardware_id id,
        enum hardinfo_vendor_chg_info data);
extern int hardinfo_get_vendor_chginfo(enum hardware_id id);
/* Spruce code for OSPURCET-450 by zhangjb18 at 2022.12.05 start */
extern int hardinfo_get_chg_online(void);
extern int hardinfo_get_charging(void);
/* Spruce code for OSPURCET-450 by zhangjb18 at 2022.12.05 end */
/* Spruce code for OSPURCET-33 by zhangjb18 at 2022.12.12 start */
extern bool hardinfo_get_bp(void);
extern bool hardinfo_get_bm(void);
extern int bm_updata_func(void);
extern int bp_updata_func(void);
extern int hardinfo_get_fake_temp(void);
/* Spruce code for OSPURCET-33 by zhangjb18 at 2022.12.12 end */
/* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 start */
extern void hardinfo_set_charging(bool en);
extern int hardinfo_get_thermal_limit(void);
extern int usbpd_pm_get_cp_status(void);
extern int usbpd_pm_set_cp_status(bool enable);
/* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 end */
#endif