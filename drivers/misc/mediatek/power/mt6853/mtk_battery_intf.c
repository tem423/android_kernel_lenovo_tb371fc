/* SPDX-License-Identifier: GPL-2.0 */
/*
* Copyright (C) 2021 MediaTek Inc.
*/
#include <linux/types.h>
#include <mt-plat/v1/mtk_battery.h>
#include <mt-plat/v1/mtk_charger.h>
#include <mt-plat/mtk_boot.h>
#include <mtk_gauge_class.h>
#include <mtk_battery_internal.h>

#ifdef CONFIG_CUSTOM_BATTERY_EXTERNAL_CHANNEL
#include <custome_external_battery.h>
#endif
#include <linux/of.h>

struct tag_bootmode {
	u32 size;
	u32 tag;
	u32 bootmode;
	u32 boottype;
};

signed int battery_get_uisoc(void)
{
	struct mtk_battery *gm = get_mtk_battery();
	if (gm != NULL) {
		int boot_mode = gm->boot_mode;

		if ((boot_mode == META_BOOT) ||
			(boot_mode == ADVMETA_BOOT) ||
			(boot_mode == FACTORY_BOOT) ||
			(boot_mode == ATE_FACTORY_BOOT))
			return 75;
		else if (boot_mode == 0)
			return gm->ui_soc;
	}

	return 50;
}

int __attribute__((weak)) charger_get_vbus(void)
{
	return 4500;
}

#if (CONFIG_MTK_GAUGE_VERSION != 30)
signed int battery_get_bat_voltage(void)
{
	return 4000;
}

signed int battery_get_bat_current(void)
{
	return 0;
}

signed int battery_get_bat_current_mA(void)
{
	return 0;
}

signed int battery_get_soc(void)
{
	return 50;
}

/*
signed int battery_get_uisoc(void)
{
	int boot_mode = get_boot_mode();

	if ((boot_mode == META_BOOT) ||
		(boot_mode == ADVMETA_BOOT) ||
		(boot_mode == FACTORY_BOOT) ||
		(boot_mode == ATE_FACTORY_BOOT))
		return 75;

	return 50;
}*/

signed int battery_get_bat_temperature(void)
{
	return 25;
}

signed int battery_get_ibus(void)
{
	return 0;
}

signed int battery_get_vbus(void)
{
	return 0;
}

signed int battery_get_bat_avg_current(void)
{
	return 0;
}
#else

	#ifdef CONFIG_CUSTOM_BATTERY_EXTERNAL_CHANNEL
_CODE_DEFINEDE
	#else
/* Spruce code for OSPURCET-8 by zhangjb18 at 2022.11.29 start */
int dual_swchg_get_gauge_info(enum power_supply_property psp, u32 *ival)
{
	union power_supply_propval val = {.intval = 0};
	int ret = 0;
	static struct power_supply *bat_psy;

	if(!bat_psy) {
		bat_psy = power_supply_get_by_name("battery");
		if(!bat_psy) {
			pr_notice("%s no battery psy\n", __func__);
			return -ENODEV;
		}
	}

	ret = power_supply_get_property(bat_psy, psp, &val);
	if (ret) {
		pr_notice("not find psp %d\n", psp);
		return ret;
	}

	*ival = val.intval;

	return ret;
}

signed int battery_get_bat_voltage(void)
{
	union power_supply_propval val = {.intval = 0};
	dual_swchg_get_gauge_info(POWER_SUPPLY_PROP_VOLTAGE_NOW, &val.intval);

	return val.intval;
}

signed int battery_get_bat_current(void)
{
	union power_supply_propval val = {.intval = 0};
	dual_swchg_get_gauge_info(POWER_SUPPLY_PROP_CURRENT_NOW, &val.intval);

	return val.intval;
}

signed int battery_get_bat_current_mA(void)
{
	return battery_get_bat_current() / 10;
}

signed int battery_get_soc(void)
{
	union power_supply_propval val = {.intval = 0};
	dual_swchg_get_gauge_info(POWER_SUPPLY_PROP_CAPACITY, &val.intval);

	return val.intval;
}

/*
signed int battery_get_uisoc(void)
{
	int boot_mode = get_boot_mode();
	struct mtk_battery *gm = get_mtk_battery();

	if ((boot_mode == META_BOOT) ||
		(boot_mode == ADVMETA_BOOT) ||
		(boot_mode == FACTORY_BOOT) ||
		(boot_mode == ATE_FACTORY_BOOT))
		return 75;

	if (gm != NULL)
		return gm->ui_soc;
	else
		return 50;
}*/

signed int battery_get_bat_temperature(void)
{
	union power_supply_propval val = {.intval = 0};
	dual_swchg_get_gauge_info(POWER_SUPPLY_PROP_TEMP, &val.intval);

	return val.intval/10;
}
/* Spruce code for OSPURCET-8 by zhangjb18 at 2022.11.29 end */
signed int battery_get_ibus(void)
{
	return pmic_get_ibus();
}

signed int battery_get_vbus(void)
{
	return charger_get_vbus();
}

signed int battery_get_bat_avg_current(void)
{
	bool valid;

	return gauge_get_average_current(&valid);
}

/* 4.9 already remove, only leave in 4.4 */
signed int battery_get_bat_uisoc(void)
{
	return battery_get_uisoc();
}

signed int battery_get_bat_soc(void)
{
	return battery_get_soc();
}

bool battery_get_bat_current_sign(void)
{
	int curr_val;

	return gauge_get_current(&curr_val);
}

int get_ui_soc(void)
{
	return battery_get_uisoc();
}

signed int battery_get_bat_avg_voltage(void)
{
	return gauge_get_nag_vbat();
}

signed int battery_meter_get_battery_current(void)
{
	return battery_get_bat_current();
}

bool battery_meter_get_battery_current_sign(void)
{
	return battery_get_bat_current_sign();
}

signed int battery_meter_get_battery_temperature(void)
{
	return battery_get_bat_temperature();
}

signed int battery_meter_get_charger_voltage(void)
{
	return battery_get_vbus();
}

unsigned long BAT_Get_Battery_Current(int polling_mode)
{
	return (long)battery_get_bat_avg_current();
}

unsigned long BAT_Get_Battery_Voltage(int polling_mode)
{
	long int ret;

	ret = (long)battery_get_bat_voltage();
	return ret;
}

unsigned int bat_get_ui_percentage(void)
{
	return battery_get_uisoc();
}

unsigned int battery_get_is_kpoc(void)
{
	return is_kernel_power_off_charging();
}

bool battery_is_battery_exist(void)
{
	return pmic_is_battery_exist();
}


void wake_up_bat(void)
{
}
EXPORT_SYMBOL(wake_up_bat);
/* end of legacy API */


/* GM25 only */
int en_intr_VBATON_UNDET(int en)
{
	return 0;
}

int reg_VBATON_UNDET(void (*callback)(void))
{
	return 0;
}

	#endif
#endif
