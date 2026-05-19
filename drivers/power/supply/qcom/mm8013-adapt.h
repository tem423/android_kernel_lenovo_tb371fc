/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MM8013 Adaptation header for BMS replacement
 * Copyright (c) 2024. All rights reserved.
 */

#ifndef __MM8013_ADAPT_H__
#define __MM8013_ADAPT_H__

#include <linux/power_supply.h>
#include "mm8013c06_battery.h"

/* MM8013 voltage thresholds for step charging (mV) */
#define MM8013_STEP0_VOLTAGE_MV		4480
#define MM8013_STEP1_VOLTAGE_MV		4400
#define MM8013_STEP2_VOLTAGE_MV		4350
#define MM8013_STEP3_VOLTAGE_MV		4200

/* MM8013 recharge voltage thresholds (mV) */
#define MM8013_STEP0_RECHARGE_VOLTAGE_MV	4380
#define MM8013_STEP1_RECHARGE_VOLTAGE_MV	4250
#define MM8013_STEP2_RECHARGE_VOLTAGE_MV	4200
#define MM8013_STEP3_RECHARGE_VOLTAGE_MV	4100

/* MM8013 float voltage thresholds (uV) */
#define MM8013_STEP0_FLOAT_VOLTAGE_UV		4490000
#define MM8013_STEP1_FLOAT_VOLTAGE_UV		4420000
#define MM8013_STEP2_FLOAT_VOLTAGE_UV		4380000
#define MM8013_STEP3_FLOAT_VOLTAGE_UV		4250000

/* Battery ID values from MM8013 (ohms) */
#define FMT_BATTERY_ID_OHMS		10000
#define SWD_BATTERY_ID_OHMS		20000
#define LINDE_BATTERY_ID_OHMS		30000
#define UNKNOWN_BATTERY_ID_OHMS		0

/* Helper function to get battery ID in ohms from MM8013 */
static inline int mm8013_get_batt_id_ohms(void)
{
	enum hq_batteryID id = hq_batt_id_get();
	
	switch (id) {
	case HQ_BATTERY_FMT:
		return FMT_BATTERY_ID_OHMS;
	case HQ_BATTERY_SWD:
		return SWD_BATTERY_ID_OHMS;
	case HQ_BATTERY_LINDE:
		return LINDE_BATTERY_ID_OHMS;
	default:
		return UNKNOWN_BATTERY_ID_OHMS;
	}
}

/* Helper function to get gauge voltage from MM8013 (mV) */
static inline int mm8013_get_gauge_voltage_mv(void)
{
	int voltage_uv = 0;
	int ret;
	
	ret = mm8013_voltage(&voltage_uv);
	if (ret < 0)
		return -EINVAL;
	
	return voltage_uv / 1000;
}

#endif /* __MM8013_ADAPT_H__ */