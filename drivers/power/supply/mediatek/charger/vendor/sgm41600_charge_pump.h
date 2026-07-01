/* SPDX-License-Identifier: GPL-2.0-only */
// sgm41600 Charger Driver  version 2021-08-17 V01
/* Copyright (C) 2020 sgmicro Incorporated - http://www.sg-micro.com/ */

#ifndef _SGM41600_CHARGER_H
#define _SGM41600_CHARGER_H

#include <linux/i2c.h>

#define SGM41600_MANUFACTURER	"sgmicro"
#define SGM41600_NAME		    "sgm41600"
#define SGM41600_DEV_ID		    0x08
/******************* define ********************/
#define SGM41600_BUSOCP_STEP_uA		100000
#define SGM41600_BUSOCP_MIN_uA	    500000
#define SGM41600_BUSOCP_MAX_uA	    3600000
#define SGM41600_BUSOCP_DEF_uA		3000000

#define SGM41600_AC_OVP_STEP_V		1
#define SGM41600_AC_OVP_MIN_V	    4
#define SGM41600_AC_OVP_MAX_V	    19
#define SGM41600_AC_OVP_DEF_V       12

#define SGM41600_VDRP_OVP_STEP_uV	  50000
#define SGM41600_VDRP_OVP_MIN_uV	  50000
#define SGM41600_VDRP_OVP_MAX_uV	  400000
#define SGM41600_VDRP_OVP_DEF_uV      300000

#define SGM41600_BUS_OVP_MAX_uV       14000000
#define SGM41600_BUS_OVP_MIN_uV       4000000
#define SGM41600_BUS_OVP_STEP_uV      100000
#define SGM41600_BUS_OVP_DEF_uV       11500000

#define SGM41600_IBUS_OCP_MAX_uA       3600000
#define SGM41600_IBUS_OCP_MIN_uA       500000
#define SGM41600_IBUS_OCP_STEP_uA      100000

#define SGM41600_IBUSOCP_VD_STEP_uA	    100000
#define SGM41600_IBUSOCP_VD_OFFSET_uA	500000
#define SGM41600_IBUSOCP_VD_MAX_uA	    3600000
#define SGM41600_IBUSOCP_VD_DEF_uA		3000000

#define SGM41600_IBUSOCP_BYP_STEP_uA	100000
#define SGM41600_IBUSOCP_BYP_OFFSET_uA	2500000
#define SGM41600_IBUSOCP_BYP_MAX_uA	    5600000
#define SGM41600_IBUSOCP_BYP_DEF_uA		5000000

#define SGM41600_BYPASS_OCP_OFFSET_uA  2000000

#define SGM41600_BAT_OVP_MAX_uV       5000000
#define SGM41600_BAT_OVP_MIN_uV       4000000
#define SGM41600_BAT_OVP_STEP_uV      25000
#define SGM41600_BAT_OVP_DEF_uV		  4350000

#define SGM41600_IBAT_OCP_MAX_uA       7200000
#define SGM41600_IBAT_OCP_MIN_uA       2000000
#define SGM41600_IBAT_OCP_STEP_uA      100000
#define SGM41600_IBAT_OCP_DEF_uA	   7200000

#define SGM41600_DP_DAC	     GENMASK(7, 5)
#define SGM41600_DM_DAC	     GENMASK(4, 2)
#define SGM41600_EN_HVDCP	 BIT(1)

#define SGM41600_CHARGE_MODE_OFF          0
#define SGM41600_CHARGE_MODE_BYPASS       1
#define SGM41600_CHARGE_MODE_DIV2		  2

#define SGM41600_FAULT_REG 0x0D

struct sgm41600_fields {
	u8 reg;
	u8 shift;
	u8 mask;	
};


struct sgm41600_cfg {
	bool bat_ovp_disable;	
	bool bat_ocp_disable;	
	bool vdrop_ovp_disable;	
	bool bus_ovp_disable;	
	bool bus_ocp_disable;
	bool bus_ucp_disable;	
	int bat_ovp_th;
	int bat_ocp_th;
	int bus_ovp_th;
	int bus_ocp_th;
	int ac_ovp_th;
	
};

struct sgm41600_state {
	u8  flt_flag1;
	u8  flt_flag2;
	u8  flt_flag3;
	u32 vbat_adc;
	u32 vbus_adc;
	u32 ibus_adc;
	u32 ibat_adc;
	u32 tdie_adc;
};

struct sgm41600_device {
	struct i2c_client *client;
	struct device *dev;
	struct power_supply *charger;	
	struct mutex lock;
	struct mutex i2c_rw_lock;
	struct mutex irq_complete;

	struct usb_phy *usb2_phy;
	struct usb_phy *usb3_phy;
	struct notifier_block usb_nb;
	struct work_struct usb_work;
	unsigned long usb_event;
	struct regmap *regmap;

	char model_name[I2C_NAME_SIZE];
	int device_id;
	int mode;
	bool usb_present;
	bool bypass_mode_enable;

	struct sgm41600_cfg *cfg;
	struct sgm41600_state state;
	u32 watchdog_timer;
	#if defined(CONFIG_MTK_GAUGE_VERSION) && (CONFIG_MTK_GAUGE_VERSION == 30)
	struct charger_device *chg_dev;
	#endif
	//struct regulator_dev *otg_rdev;

	struct delayed_work charge_detect_delayed_work;
	struct delayed_work charge_monitor_work;
	struct notifier_block pm_nb;
	bool sgm41600_suspend_flag;
	bool irq_waiting;
	bool irq_disabled;
	bool resume_completed;

	int vbat_volt;
	int vbus_volt;
	int vout_volt;
	int vac_volt;
	int ibat_curr;
	int ibus_curr;
	int vbus_error;

	int die_temp;

	struct wakeup_source *charger_wakelock;	
};

#endif /* _SGM41600_CHARGER_H */
