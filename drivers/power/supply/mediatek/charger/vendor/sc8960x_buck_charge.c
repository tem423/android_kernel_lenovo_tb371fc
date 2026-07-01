// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Southchip Semiconductor Technology(Shanghai) Co., Ltd.
 */
#define pr_fmt(fmt)	"[sc8960x]:%s: " fmt, __func__

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/err.h>
#include <linux/bitops.h>
#include <linux/math64.h>
#include <v1/charger_type.h>

#include <v1/charger_class.h>
#include <v1/mtk_charger.h>
#include "sc8960x_buck_reg.h"
#include "sc8960x_buck.h"

#define SC89601D_1P0

enum {
	PN_SC89601D,
};

enum sc8960x_part_no {
	SC89601D = 0x08,
};

static int pn_data[] = {
	[PN_SC89601D] = 0x08,
};

static char *pn_str[] = {
	[PN_SC89601D] = "sc89601d",
};

struct sc8960x {
	struct device *dev;
	struct i2c_client *client;

	enum sc8960x_part_no part_no;
	int revision;

	const char *chg_dev_name;
	const char *eint_name;

	bool chg_det_enable;

	enum charger_type chg_type;

	int status;
	int irq;

	struct mutex i2c_rw_lock;

	bool charge_enabled;	/* Register bit status */
	bool power_good;

	struct sc8960x_platform_data *platform_data;
	struct charger_device *chg_dev;

	struct power_supply *psy;

	int en_chip_gpio;
};

static const struct charger_properties sc8960x_chg_props = {
	.alias_name = "sc8960x",
};

static int __sc8960x_read_reg(struct sc8960x *sc, u8 reg, u8 *data)
{
	s32 ret;

	ret = i2c_smbus_read_byte_data(sc->client, reg);
	if (ret < 0) {
		pr_err("i2c read fail: can't read from reg 0x%02X\n", reg);
		return ret;
	}

	*data = (u8) ret;

	return 0;
}

static int __sc8960x_write_reg(struct sc8960x *sc, int reg, u8 val)
{
	s32 ret;

	ret = i2c_smbus_write_byte_data(sc->client, reg, val);
	if (ret < 0) {
		pr_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n",
		       val, reg, ret);
		return ret;
	}
	return 0;
}

static int sc8960x_read_byte(struct sc8960x *sc, u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&sc->i2c_rw_lock);
	ret = __sc8960x_read_reg(sc, reg, data);
	mutex_unlock(&sc->i2c_rw_lock);

	return ret;
}

static int sc8960x_write_byte(struct sc8960x *sc, u8 reg, u8 data)
{
	int ret;

	mutex_lock(&sc->i2c_rw_lock);
	ret = __sc8960x_write_reg(sc, reg, data);
	mutex_unlock(&sc->i2c_rw_lock);

	if (ret)
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);

	return ret;
}

static int sc8960x_update_bits(struct sc8960x *sc, u8 reg, u8 mask, u8 data)
{
	int ret;
	u8 tmp;

	mutex_lock(&sc->i2c_rw_lock);
	ret = __sc8960x_read_reg(sc, reg, &tmp);
	if (ret) {
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= data & mask;

	ret = __sc8960x_write_reg(sc, reg, tmp);
	if (ret)
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);

out:
	mutex_unlock(&sc->i2c_rw_lock);
	return ret;
}

#if 0
static int sc8960x_enable_otg(struct sc8960x *sc)
{
	u8 val = REG01_OTG_ENABLE << REG01_OTG_CONFIG_SHIFT;

	return sc8960x_update_bits(sc, SC8960X_REG_01, REG01_OTG_CONFIG_MASK,
				   val);

}
#endif

static int sc8960x_disable_otg(struct sc8960x *sc)
{
	u8 val = REG01_OTG_DISABLE << REG01_OTG_CONFIG_SHIFT;

	return sc8960x_update_bits(sc, SC8960X_REG_01, REG01_OTG_CONFIG_MASK,
				   val);

}

static int sc8960x_enable_charger(struct sc8960x *sc)
{
	int ret;
	u8 val = REG01_CHG_ENABLE << REG01_CHG_CONFIG_SHIFT;

	ret =
	    sc8960x_update_bits(sc, SC8960X_REG_01, REG01_CHG_CONFIG_MASK, val);

	return ret;
}

static int sc8960x_disable_charger(struct sc8960x *sc)
{
	int ret;
	u8 val = REG01_CHG_DISABLE << REG01_CHG_CONFIG_SHIFT;

	ret =
	    sc8960x_update_bits(sc, SC8960X_REG_01, REG01_CHG_CONFIG_MASK, val);
	return ret;
}

int sc8960x_set_chargecurrent(struct sc8960x *sc, int curr)
{
	u8 ichg;

	if (curr < REG02_ICHG_BASE)
		curr = REG02_ICHG_BASE;

	ichg = (curr - REG02_ICHG_BASE) / REG02_ICHG_LSB;
	return sc8960x_update_bits(sc, SC8960X_REG_02, REG02_ICHG_MASK,
				   ichg << REG02_ICHG_SHIFT);

}

int sc8960x_set_term_current(struct sc8960x *sc, int curr)
{
	u8 iterm;

	if (curr < REG03_ITERM_BASE)
		curr = REG03_ITERM_BASE;

	iterm = (curr - REG03_ITERM_BASE) / REG03_ITERM_LSB;

	return sc8960x_update_bits(sc, SC8960X_REG_03, REG03_ITERM_MASK,
				   iterm << REG03_ITERM_SHIFT);
}
EXPORT_SYMBOL_GPL(sc8960x_set_term_current);

int sc8960x_set_prechg_current(struct sc8960x *sc, int curr)
{
	u8 iprechg;

	if (curr < REG03_IPRECHG_BASE)
		curr = REG03_IPRECHG_BASE;

	iprechg = (curr - REG03_IPRECHG_BASE) / REG03_IPRECHG_LSB;

	return sc8960x_update_bits(sc, SC8960X_REG_03, REG03_IPRECHG_MASK,
				   iprechg << REG03_IPRECHG_SHIFT);
}
EXPORT_SYMBOL_GPL(sc8960x_set_prechg_current);

int sc8960x_set_chargevolt(struct sc8960x *sc, int volt)
{
	u8 val;

	if (volt < REG04_VREG_BASE)
		volt = REG04_VREG_BASE;

	val = (volt - REG04_VREG_BASE) / REG04_VREG_LSB;
	return sc8960x_update_bits(sc, SC8960X_REG_04, REG04_VREG_MASK,
				   val << REG04_VREG_SHIFT);
}

int sc8960x_set_input_volt_limit(struct sc8960x *sc, int volt)
{
	u8 val;

	if (volt < REG06_VINDPM_BASE)
		volt = REG06_VINDPM_BASE;

	val = (volt - REG06_VINDPM_BASE) / REG06_VINDPM_LSB;
	return sc8960x_update_bits(sc, SC8960X_REG_06, REG06_VINDPM_MASK,
				   val << REG06_VINDPM_SHIFT);
}

int sc8960x_set_input_current_limit(struct sc8960x *sc, int curr)
{
	u8 val;

	if (curr < REG00_IINLIM_BASE)
		curr = REG00_IINLIM_BASE;

	val = (curr - REG00_IINLIM_BASE) / REG00_IINLIM_LSB;
	return sc8960x_update_bits(sc, SC8960X_REG_00, REG00_IINLIM_MASK,
				   val << REG00_IINLIM_SHIFT);
}

int sc8960x_set_watchdog_timer(struct sc8960x *sc, u8 timeout)
{
	u8 temp;

	temp = (u8) (((timeout -
		       REG05_WDT_BASE) / REG05_WDT_LSB) << REG05_WDT_SHIFT);

	return sc8960x_update_bits(sc, SC8960X_REG_05, REG05_WDT_MASK, temp);
}
EXPORT_SYMBOL_GPL(sc8960x_set_watchdog_timer);

int sc8960x_disable_watchdog_timer(struct sc8960x *sc)
{
	u8 val = REG05_WDT_DISABLE << REG05_WDT_SHIFT;

	return sc8960x_update_bits(sc, SC8960X_REG_05, REG05_WDT_MASK, val);
}
EXPORT_SYMBOL_GPL(sc8960x_disable_watchdog_timer);

int sc8960x_reset_watchdog_timer(struct sc8960x *sc)
{
	u8 val = REG01_WDT_RESET << REG01_WDT_RESET_SHIFT;

	return sc8960x_update_bits(sc, SC8960X_REG_01, REG01_WDT_RESET_MASK,
				   val);
}
EXPORT_SYMBOL_GPL(sc8960x_reset_watchdog_timer);

int sc8960x_reset_chip(struct sc8960x *sc)
{
	int ret;
	u8 val = REG0B_REG_RESET << REG0B_REG_RESET_SHIFT;

	ret =
	    sc8960x_update_bits(sc, SC8960X_REG_0B, REG0B_REG_RESET_MASK, val);
	return ret;
}
EXPORT_SYMBOL_GPL(sc8960x_reset_chip);

int sc8960x_enter_hiz_mode(struct sc8960x *sc)
{
	u8 val = REG00_HIZ_ENABLE << REG00_ENHIZ_SHIFT;

	return sc8960x_update_bits(sc, SC8960X_REG_00, REG00_ENHIZ_MASK, val);

}
EXPORT_SYMBOL_GPL(sc8960x_enter_hiz_mode);

int sc8960x_exit_hiz_mode(struct sc8960x *sc)
{

	u8 val = REG00_HIZ_DISABLE << REG00_ENHIZ_SHIFT;

	return sc8960x_update_bits(sc, SC8960X_REG_00, REG00_ENHIZ_MASK, val);

}
EXPORT_SYMBOL_GPL(sc8960x_exit_hiz_mode);

int sc8960x_get_hiz_mode(struct sc8960x *sc, u8 *state)
{
	u8 val;
	int ret;

	ret = sc8960x_read_byte(sc, SC8960X_REG_00, &val);
	if (ret)
		return ret;
	*state = (val & REG00_ENHIZ_MASK) >> REG00_ENHIZ_SHIFT;

	return 0;
}
EXPORT_SYMBOL_GPL(sc8960x_get_hiz_mode);

static int sc8960x_enable_term(struct sc8960x *sc, bool enable)
{
	u8 val;
	int ret;

	if (enable)
		val = REG05_TERM_ENABLE << REG05_EN_TERM_SHIFT;
	else
		val = REG05_TERM_DISABLE << REG05_EN_TERM_SHIFT;

	ret = sc8960x_update_bits(sc, SC8960X_REG_05, REG05_EN_TERM_MASK, val);

	return ret;
}
EXPORT_SYMBOL_GPL(sc8960x_enable_term);

int sc8960x_set_boost_current(struct sc8960x *sc, int curr)
{
	u8 val;

	val = REG02_BOOST_LIM_0P5A;
	if (curr == BOOSTI_1200)
		val = REG02_BOOST_LIM_1P2A;

	return sc8960x_update_bits(sc, SC8960X_REG_02, REG02_BOOST_LIM_MASK,
				   val << REG02_BOOST_LIM_SHIFT);
}

int sc8960x_set_boost_voltage(struct sc8960x *sc, int volt)
{
	u8 val;

	if (volt == BOOSTV_4850)
		val = REG06_BOOSTV_4P85V;
	else if (volt == BOOSTV_5150)
		val = REG06_BOOSTV_5P15V;
	else if (volt == BOOSTV_5300)
		val = REG06_BOOSTV_5P3V;
	else
		val = REG06_BOOSTV_5V;

	return sc8960x_update_bits(sc, SC8960X_REG_06, REG06_BOOSTV_MASK,
				   val << REG06_BOOSTV_SHIFT);
}
EXPORT_SYMBOL_GPL(sc8960x_set_boost_voltage);

static int sc8960x_set_acovp_threshold(struct sc8960x *sc, int volt)
{
	u8 val;

	if (volt == VAC_OVP_14000)
		val = REG06_OVP_14V;
	else if (volt == VAC_OVP_10500)
		val = REG06_OVP_10P5V;
	else if (volt == VAC_OVP_6500)
		val = REG06_OVP_6P5V;
	else
		val = REG06_OVP_5P8V;

	return sc8960x_update_bits(sc, SC8960X_REG_06, REG06_OVP_MASK,
				   val << REG06_OVP_SHIFT);
}
EXPORT_SYMBOL_GPL(sc8960x_set_acovp_threshold);

static int sc8960x_set_stat_ctrl(struct sc8960x *sc, int ctrl)
{
	u8 val;

	val = ctrl;

	return sc8960x_update_bits(sc, SC8960X_REG_00, REG00_STAT_CTRL_MASK,
				   val << REG00_STAT_CTRL_SHIFT);
}

static int sc8960x_set_int_mask(struct sc8960x *sc, int mask)
{
	u8 val;

	val = mask;

	return sc8960x_update_bits(sc, SC8960X_REG_0A, REG0A_INT_MASK_MASK,
				   val << REG0A_INT_MASK_SHIFT);
}

static int sc8960x_enable_batfet(struct sc8960x *sc)
{
	const u8 val = REG07_BATFET_ON << REG07_BATFET_DIS_SHIFT;

	return sc8960x_update_bits(sc, SC8960X_REG_07, REG07_BATFET_DIS_MASK,
				   val);
}
EXPORT_SYMBOL_GPL(sc8960x_enable_batfet);

static int sc8960x_disable_batfet(struct sc8960x *sc)
{
	const u8 val = REG07_BATFET_OFF << REG07_BATFET_DIS_SHIFT;

	return sc8960x_update_bits(sc, SC8960X_REG_07, REG07_BATFET_DIS_MASK,
				   val);
}
EXPORT_SYMBOL_GPL(sc8960x_disable_batfet);

static int sc8960x_set_batfet_delay(struct sc8960x *sc, uint8_t delay)
{
	u8 val;

	if (delay == 0)
		val = REG07_BATFET_DLY_0S;
	else
		val = REG07_BATFET_DLY_10S;

	val <<= REG07_BATFET_DLY_SHIFT;

	return sc8960x_update_bits(sc, SC8960X_REG_07, REG07_BATFET_DLY_MASK,
				   val);
}
EXPORT_SYMBOL_GPL(sc8960x_set_batfet_delay);

static int sc8960x_enable_safety_timer(struct sc8960x *sc)
{
	const u8 val = REG05_CHG_TIMER_ENABLE << REG05_EN_TIMER_SHIFT;

	return sc8960x_update_bits(sc, SC8960X_REG_05, REG05_EN_TIMER_MASK,
				   val);
}
EXPORT_SYMBOL_GPL(sc8960x_enable_safety_timer);

static int sc8960x_disable_safety_timer(struct sc8960x *sc)
{
	const u8 val = REG05_CHG_TIMER_DISABLE << REG05_EN_TIMER_SHIFT;

	return sc8960x_update_bits(sc, SC8960X_REG_05, REG05_EN_TIMER_MASK,
				   val);
}
EXPORT_SYMBOL_GPL(sc8960x_disable_safety_timer);

static struct sc8960x_platform_data *sc8960x_parse_dt(struct device_node *np,
						      struct sc8960x *sc)
{
	int ret;
	struct sc8960x_platform_data *pdata;

	pdata = devm_kzalloc(sc->dev, sizeof(struct sc8960x_platform_data),
			     GFP_KERNEL);
	if (!pdata)
		return NULL;

	if (of_property_read_string(np, "charger_name", &sc->chg_dev_name) < 0) {
		sc->chg_dev_name = "primary_chg";
		pr_warn("no sc charger name\n");
	}

	if (of_property_read_string(np, "eint_name", &sc->eint_name) < 0) {
		sc->eint_name = "chr_stat";
		pr_warn("no eint name\n");
	}

	sc->chg_det_enable =
	    of_property_read_bool(np, "sc,sc8960x,charge-detect-enable");

	ret = of_property_read_u32(np, "sc,sc8960x,usb-vlim", &pdata->usb.vlim);
	if (ret) {
		pdata->usb.vlim = 4500;
		pr_err("Failed to read node of sc,sc8960x,usb-vlim\n");
	}

	ret = of_property_read_u32(np, "sc,sc8960x,usb-ilim", &pdata->usb.ilim);
	if (ret) {
		pdata->usb.ilim = 2000;
		pr_err("Failed to read node of sc,sc8960x,usb-ilim\n");
	}

	ret = of_property_read_u32(np, "sc,sc8960x,usb-vreg", &pdata->usb.vreg);
	if (ret) {
		pdata->usb.vreg = 4200;
		pr_err("Failed to read node of sc,sc8960x,usb-vreg\n");
	}

	ret = of_property_read_u32(np, "sc,sc8960x,usb-ichg", &pdata->usb.ichg);
	if (ret) {
		pdata->usb.ichg = 2000;
		pr_err("Failed to read node of sc,sc8960x,usb-ichg\n");
	}

	ret = of_property_read_u32(np, "sc,sc8960x,stat-pin-ctrl",
				   &pdata->statctrl);
	if (ret) {
		pdata->statctrl = 0;
		pr_err("Failed to read node of sc,sc8960x,stat-pin-ctrl\n");
	}

	ret = of_property_read_u32(np, "sc,sc8960x,precharge-current",
				   &pdata->iprechg);
	if (ret) {
		pdata->iprechg = 180;
		pr_err("Failed to read node of sc,sc8960x,precharge-current\n");
	}

	ret = of_property_read_u32(np, "sc,sc8960x,termination-current",
				   &pdata->iterm);
	if (ret) {
		pdata->iterm = 180;
		pr_err
		    ("Failed to read node of sc,sc8960x,termination-current\n");
	}

	ret = of_property_read_u32(np, "sc,sc8960x,boost-voltage",
				 &pdata->boostv);
	if (ret) {
		pdata->boostv = 5000;
		pr_err("Failed to read node of sc,sc8960x,boost-voltage\n");
	}

	ret = of_property_read_u32(np, "sc,sc8960x,boost-current",
				 &pdata->boosti);
	if (ret) {
		pdata->boosti = 1200;
		pr_err("Failed to read node of sc,sc8960x,boost-current\n");
	}

	ret = of_property_read_u32(np, "sc,sc8960x,vac-ovp-threshold",
				   &pdata->vac_ovp);
	if (ret) {
		pdata->vac_ovp = 6500;
		pr_err("Failed to read node of sc,sc8960x,vac-ovp-threshold\n");
	}

	sc->en_chip_gpio = of_get_named_gpio(np, "sc,chg-en-gpio", 0);
	if (!gpio_is_valid(sc->en_chip_gpio)) {
		dev_err(sc->dev, "Failed to get the sc en chip-gpios\n");
	}

	if(gpio_is_valid(sc->en_chip_gpio)) {
		ret = devm_gpio_request_one(sc->dev, sc->en_chip_gpio, GPIOF_OUT_INIT_LOW, //default enable
			"sc_en_gpio");
	}
	return pdata;
}

static int sc8960x_init_device(struct sc8960x *sc)
{
	int ret;

	sc8960x_disable_watchdog_timer(sc);

	ret = sc8960x_set_stat_ctrl(sc, sc->platform_data->statctrl);
	if (ret)
		pr_err("Failed to set stat pin control mode, ret = %d\n", ret);

	ret = sc8960x_set_prechg_current(sc, sc->platform_data->iprechg);
	if (ret)
		pr_err("Failed to set prechg current, ret = %d\n", ret);

	ret = sc8960x_set_term_current(sc, sc->platform_data->iterm);
	if (ret)
		pr_err("Failed to set termination current, ret = %d\n", ret);

	ret = sc8960x_set_boost_voltage(sc, sc->platform_data->boostv);
	if (ret)
		pr_err("Failed to set boost voltage, ret = %d\n", ret);

	ret = sc8960x_set_boost_current(sc, sc->platform_data->boosti);
	if (ret)
		pr_err("Failed to set boost current, ret = %d\n", ret);

	ret = sc8960x_set_acovp_threshold(sc, sc->platform_data->vac_ovp);
	if (ret)
		pr_err("Failed to set acovp threshold, ret = %d\n", ret);

	ret = sc8960x_set_int_mask(sc,
				   REG0A_IINDPM_INT_MASK |
				   REG0A_VINDPM_INT_MASK);
	if (ret)
		pr_err("Failed to set vindpm and iindpm int mask\n");

	ret = sc8960x_enable_term(sc, false);
	if (ret)
		pr_err("Failed to set disable term\n");

	ret = sc8960x_exit_hiz_mode(sc); //defualt  not hiz
	if (ret)
		pr_err("Failed to set hiz mode\n");

	ret = sc8960x_disable_otg(sc);
	if (ret)
		pr_err("Failed to disable otg mode\n");

/* Spruce code for OSPURCET-1500 by gujy6 at 2023.03.03 start */
	ret = sc8960x_disable_charger(sc);
	if (ret)
		pr_err("Failed to disable chg mode\n");
/* Spruce code for OSPURCET-1500 by gujy6 at 2023.03.03 end */

#ifdef SC89601D_1P0
	sc8960x_write_byte(sc, 0x7E, 0x48);
	sc8960x_write_byte(sc, 0x7E, 0x54);
	sc8960x_write_byte(sc, 0x7E, 0x30);
	sc8960x_write_byte(sc, 0x7E, 0x4C);
	sc8960x_write_byte(sc, 0x94, 0x10);
	sc8960x_write_byte(sc, 0x96, 0x09);
	sc8960x_write_byte(sc, 0x92, 0x71);
	sc8960x_write_byte(sc, 0x93, 0x19);
	sc8960x_write_byte(sc, 0x7E, 0x48);
	sc8960x_write_byte(sc, 0x7E, 0x54);
	sc8960x_write_byte(sc, 0x7E, 0x30);
	sc8960x_write_byte(sc, 0x7E, 0x4C);
#endif

	return 0;
}

static int sc8960x_detect_device(struct sc8960x *sc)
{
	int ret;
	u8 data;

	ret = sc8960x_read_byte(sc, SC8960X_REG_0B, &data);
	if (!ret) {
		sc->part_no = (data & REG0B_PN_MASK) >> REG0B_PN_SHIFT;
		sc->revision =
		    (data & REG0B_DEV_REV_MASK) >> REG0B_DEV_REV_SHIFT;
	}

	return ret;
}

static void sc8960x_dump_regs(struct sc8960x *sc)
{
	int ret, addr;
	char buffer[1024] = {0};
	char *ptr = buffer;
	u8 val;

	for (addr = 0x0; addr <= 0x0B; addr++)  {
		ret = sc8960x_read_byte(sc, addr, &val);
		if (ret == 0)
			ptr += sprintf(ptr, "[0x%0x=0x%0x]",
			       addr, val);
	}
	pr_err("[%s]%s\n",__func__, buffer);
}

static ssize_t
sc8960x_show_registers(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	struct sc8960x *sc = dev_get_drvdata(dev);
	u8 addr;
	u8 val;
	u8 tmpbuf[200];
	int len;
	int idx = 0;
	int ret;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "sc8960x Reg");
	for (addr = 0x0; addr <= 0x0B; addr++) {
		ret = sc8960x_read_byte(sc, addr, &val);
		if (ret == 0) {
			len = snprintf(tmpbuf, PAGE_SIZE - idx,
				       "Reg[%.2x] = 0x%.2x\n", addr, val);
			memcpy(&buf[idx], tmpbuf, len);
			idx += len;
		}
	}

	return idx;
}

static ssize_t
sc8960x_store_registers(struct device *dev,
			struct device_attribute *attr, const char *buf,
			size_t count)
{
	struct sc8960x *sc = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg < 0x0B) {
		sc8960x_write_byte(sc, (unsigned char) reg,
				   (unsigned char) val);
	}

	return count;
}

static DEVICE_ATTR(registers, S_IRUGO | S_IWUSR, sc8960x_show_registers,
		   sc8960x_store_registers);

static struct attribute *sc8960x_attributes[] = {
	&dev_attr_registers.attr,
	NULL,
};

static const struct attribute_group sc8960x_attr_group = {
	.attrs = sc8960x_attributes,
};

// extern int mt6360_uud_ctrl_on(bool en);
static int sc8960x_charging(struct charger_device *chg_dev, bool enable)
{

	struct sc8960x *sc = dev_get_drvdata(&chg_dev->dev);
	int ret = 0;
	u8 val;

	if (enable){
		// mt6360_uud_ctrl_on(true);
		ret = sc8960x_enable_charger(sc);
	}

	else{
		// mt6360_uud_ctrl_on(false);
		ret = sc8960x_disable_charger(sc);
	}


	pr_err("%s sc8960x charger %s\n", enable ? "enable" : "disable",
	       !ret ? "successfully" : "failed");

	ret = sc8960x_read_byte(sc, SC8960X_REG_01, &val);

	if (!ret)
		sc->charge_enabled = !!(val & REG01_CHG_CONFIG_MASK);

	return ret;	
}

static int sc8960x_plug_in(struct charger_device *chg_dev)
{

	int ret;

	ret = sc8960x_charging(chg_dev, true);

	if (ret)
		pr_err("Failed to enable charging:%d\n", ret);

	return ret;
}

static int sc8960x_plug_out(struct charger_device *chg_dev)
{
	int ret;

	ret = sc8960x_charging(chg_dev, false);

	if (ret)
		pr_err("Failed to disable charging:%d\n", ret);

	return ret;
}

static int sc8960x_dump_register(struct charger_device *chg_dev)
{
	struct sc8960x *sc = dev_get_drvdata(&chg_dev->dev);

	sc8960x_dump_regs(sc);

	return 0;
}

static int sc8960x_is_charging_enable(struct charger_device *chg_dev, bool *en)
{
	struct sc8960x *sc = dev_get_drvdata(&chg_dev->dev);

	*en = sc->charge_enabled;

	return 0;
}

static int sc8960x_is_charging_done(struct charger_device *chg_dev, bool *done)
{
	struct sc8960x *sc = dev_get_drvdata(&chg_dev->dev);
	int ret;
	u8 val;

	ret = sc8960x_read_byte(sc, SC8960X_REG_08, &val);
	if (!ret) {
		val = val & REG08_CHRG_STAT_MASK;
		val = val >> REG08_CHRG_STAT_SHIFT;
		*done = (val == REG08_CHRG_STAT_CHGDONE);
	}

	return ret;
}

static int sc8960x_set_ichg(struct charger_device *chg_dev, u32 curr)
{
	struct sc8960x *sc = dev_get_drvdata(&chg_dev->dev);

	pr_err("charge curr = %d\n", curr);

	return sc8960x_set_chargecurrent(sc, curr / 1000);
}

static int sc8960x_get_ichg(struct charger_device *chg_dev, u32 *curr)
{
	struct sc8960x *sc = dev_get_drvdata(&chg_dev->dev);
	u8 reg_val;
	int ichg;
	int ret;

	ret = sc8960x_read_byte(sc, SC8960X_REG_02, &reg_val);
	if (!ret) {
		ichg = (reg_val & REG02_ICHG_MASK) >> REG02_ICHG_SHIFT;
		ichg = ichg * REG02_ICHG_LSB + REG02_ICHG_BASE;
		*curr = ichg * 1000;
	}

	return ret;
}

static int sc8960x_get_min_ichg(struct charger_device *chg_dev, u32 *curr)
{
	*curr = 60 * 1000;

	return 0;
}

static int sc8960x_set_vchg(struct charger_device *chg_dev, u32 volt)
{
	struct sc8960x *sc = dev_get_drvdata(&chg_dev->dev);

	pr_err("charge volt = %d\n", volt);

	return sc8960x_set_chargevolt(sc, volt / 1000);
}

static int sc8960x_get_vchg(struct charger_device *chg_dev, u32 *volt)
{
	struct sc8960x *sc = dev_get_drvdata(&chg_dev->dev);
	u8 reg_val;
	int vchg;
	int ret;

	ret = sc8960x_read_byte(sc, SC8960X_REG_04, &reg_val);
	if (!ret) {
		vchg = (reg_val & REG04_VREG_MASK) >> REG04_VREG_SHIFT;
		vchg = vchg * REG04_VREG_LSB + REG04_VREG_BASE;
		*volt = vchg * 1000;
	}

	return ret;
}

static int sc8960x_set_ivl(struct charger_device *chg_dev, u32 volt)
{
	struct sc8960x *sc = dev_get_drvdata(&chg_dev->dev);

	pr_err("vindpm volt = %d\n", volt);

	return sc8960x_set_input_volt_limit(sc, volt / 1000);

}

static int sc8960x_set_icl(struct charger_device *chg_dev, u32 curr)
{
	struct sc8960x *sc = dev_get_drvdata(&chg_dev->dev);

	pr_err("indpm curr = %d\n", curr);

	return sc8960x_set_input_current_limit(sc, curr / 1000);
}

static int sc8960x_get_icl(struct charger_device *chg_dev, u32 *curr)
{
	struct sc8960x *sc = dev_get_drvdata(&chg_dev->dev);
	u8 reg_val;
	int icl;
	int ret;

	ret = sc8960x_read_byte(sc, SC8960X_REG_00, &reg_val);
	if (!ret) {
		icl = (reg_val & REG00_IINLIM_MASK) >> REG00_IINLIM_SHIFT;
		icl = icl * REG00_IINLIM_LSB + REG00_IINLIM_BASE;
		*curr = icl * 1000;
	}

	return ret;
}

static int sc8960x_kick_wdt(struct charger_device *chg_dev)
{
	struct sc8960x *sc = dev_get_drvdata(&chg_dev->dev);

	return sc8960x_reset_watchdog_timer(sc);
}

#if 0
static int sc8960x_set_otg(struct charger_device *chg_dev, bool en)
{
	int ret;
	struct sc8960x *sc = dev_get_drvdata(&chg_dev->dev);

	if (en) {
        ret = sc8960x_disable_charger(sc);
		ret = sc8960x_enable_otg(sc);
    }
	else {
        ret = sc8960x_disable_otg(sc);
        ret = sc8960x_enable_otg(sc);
    }

	pr_err("%s OTG %s\n", en ? "enable" : "disable",
	       !ret ? "successfully" : "failed");

	return ret;
}
#endif 

static int sc8960x_set_safety_timer(struct charger_device *chg_dev, bool en)
{
	struct sc8960x *sc = dev_get_drvdata(&chg_dev->dev);
	int ret;

	if (en)
		ret = sc8960x_enable_safety_timer(sc);
	else
		ret = sc8960x_disable_safety_timer(sc);

	return ret;
}

static int sc8960x_is_safety_timer_enabled(struct charger_device *chg_dev,
					   bool *en)
{
	struct sc8960x *sc = dev_get_drvdata(&chg_dev->dev);
	int ret;
	u8 reg_val;

	ret = sc8960x_read_byte(sc, SC8960X_REG_05, &reg_val);

	if (!ret)
		*en = !!(reg_val & REG05_EN_TIMER_MASK);

	return ret;
}

static int sc8960x_set_boost_ilmt(struct charger_device *chg_dev, u32 curr)
{
	struct sc8960x *sc = dev_get_drvdata(&chg_dev->dev);
	int ret;

	pr_err("otg curr = %d\n", curr);

	ret = sc8960x_set_boost_current(sc, curr / 1000);

	return ret;
}

static int sc8960x_is_chip_enabled(struct charger_device *chg_dev, bool *en)
{
	int ret;
	u8 reg_val;
	struct sc8960x *sc = dev_get_drvdata(&chg_dev->dev);

	ret = sc8960x_read_byte(sc, SC8960X_REG_00, &reg_val);
	if (ret)
		return ret;
	*en = !(reg_val & REG00_ENHIZ_MASK) >> REG00_ENHIZ_SHIFT;

	return ret;
}
/* Spruce code for OSPURCET-450 by zhangjb18 at 2022.12.05 start */
static int sc8960x_set_hiz(struct charger_device *chg_dev, bool en)
{
	struct sc8960x *sc = dev_get_drvdata(&chg_dev->dev);
	if(en)
		sc8960x_enter_hiz_mode(sc);
	else
		sc8960x_exit_hiz_mode(sc);
	return 0;
}
/* Spruce code for OSPURCET-450 by zhangjb18 at 2022.12.05 end */
static int sc8960x_enable_chip(struct charger_device *chg_dev, bool en)
{
	int ret;
	u8 reg_val = en ? 0 : REG00_ENHIZ_MASK ;
	struct sc8960x *sc = dev_get_drvdata(&chg_dev->dev);

	ret = sc8960x_update_bits(sc, SC8960X_REG_00, REG00_ENHIZ_MASK, reg_val);
	if (ret)
		return ret;



	ret = sc8960x_read_byte(sc, SC8960X_REG_00, &reg_val);
	if (ret)
		return ret;
	en = !(reg_val & REG00_ENHIZ_MASK) >> REG00_ENHIZ_SHIFT;

	pr_notice("%s en =%d  reg_val = %d\n", __func__, en, reg_val);
	return ret;
}

static struct charger_ops sc8960x_chg_ops = {
	/* Normal charging */
	.plug_in = sc8960x_plug_in,
	.plug_out = sc8960x_plug_out,
	.dump_registers = sc8960x_dump_register,
	.enable = sc8960x_charging,
	.is_enabled = sc8960x_is_charging_enable,
	.get_charging_current = sc8960x_get_ichg,
	.set_charging_current = sc8960x_set_ichg,
	.get_input_current = sc8960x_get_icl,
	.set_input_current = sc8960x_set_icl,
	.get_constant_voltage = sc8960x_get_vchg,
	.set_constant_voltage = sc8960x_set_vchg,
	.kick_wdt = sc8960x_kick_wdt,
	.set_mivr = sc8960x_set_ivl,
	.is_charging_done = sc8960x_is_charging_done,
	.get_min_charging_current = sc8960x_get_min_ichg,

	/* Safety timer */
	.enable_safety_timer = sc8960x_set_safety_timer,
	.is_safety_timer_enabled = sc8960x_is_safety_timer_enabled,

	/* Power path */
	.is_powerpath_enabled = sc8960x_is_chip_enabled,
	.enable_powerpath = sc8960x_enable_chip,

	/* Chip enable */
	.is_chip_enabled = sc8960x_is_charging_enable,
	.enable_chip = sc8960x_charging,

	/* OTG */
	// .enable_otg = sc8960x_set_otg,
	.set_boost_current_limit = sc8960x_set_boost_ilmt,
	.enable_discharge = NULL,

	/* PE+/PE+20 */
	.send_ta_current_pattern = NULL,
	.set_pe20_efficiency_table = NULL,
	.send_ta20_current_pattern = NULL,
	.enable_cable_drop_comp = NULL,

	/* ADC */
	.get_tchg_adc = NULL,
/* Spruce code for OSPURCET-450 by zhangjb18 at 2022.12.05 start */
	.enable_hz = sc8960x_set_hiz,
/* Spruce code for OSPURCET-450 by zhangjb18 at 2022.12.05 end */
};

static struct of_device_id sc8960x_charger_match_table[] = {
	{
	 .compatible = "sc,sc89601d",
	 .data = &pn_data[PN_SC89601D],
	 },
	{},
};
MODULE_DEVICE_TABLE(of, sc8960x_charger_match_table);

static int sc8960x_charger_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	struct sc8960x *sc;
	const struct of_device_id *match;
	struct device_node *node = client->dev.of_node;

	int ret = 0;

	sc = devm_kzalloc(&client->dev, sizeof(struct sc8960x), GFP_KERNEL);
	if (!sc)
		return -ENOMEM;

	sc->dev = &client->dev;
	sc->client = client;

	i2c_set_clientdata(client, sc);

	mutex_init(&sc->i2c_rw_lock);

	ret = sc8960x_detect_device(sc);
	if (ret) {
		pr_err("No sc8960x device found!\n");
		return -ENODEV;
	}

	match = of_match_node(sc8960x_charger_match_table, node);
	if (match == NULL) {
		pr_err("device tree match not found\n");
		return -EINVAL;
	}

	if (sc->part_no != *(int *)match->data)
		pr_info("part no mismatch, hw:%s, devicetree:%s\n",
			pn_str[sc->part_no], pn_str[*(int *) match->data]);

	sc->platform_data = sc8960x_parse_dt(node, sc);
	if (!sc->platform_data) {
		pr_err("No platform data provided.\n");
		return -EINVAL;
	}

	ret = sc8960x_init_device(sc);
	if (ret) {
		pr_err("Failed to init device\n");
		return ret;
	}

	sc->chg_dev = charger_device_register(sc->chg_dev_name,
					      &client->dev, sc,
					      &sc8960x_chg_ops,
					      &sc8960x_chg_props);
	if (IS_ERR_OR_NULL(sc->chg_dev)) {
		ret = PTR_ERR(sc->chg_dev);
		return ret;
	}

	ret = sysfs_create_group(&sc->dev->kobj, &sc8960x_attr_group);
	if (ret)
		dev_err(sc->dev, "failed to register sysfs. err: %d\n", ret);

	hardinfo_set_vendor_chginfo(HRADINFO_BUCK_CHG_INFO, SC8560);

	pr_err("sc8960x probe successfully, Part Num:%d, Revision:%d\n!",
	       sc->part_no, sc->revision);

	return 0;
}

static int sc8960x_charger_remove(struct i2c_client *client)
{
	struct sc8960x *sc = i2c_get_clientdata(client);

	mutex_destroy(&sc->i2c_rw_lock);

	sysfs_remove_group(&sc->dev->kobj, &sc8960x_attr_group);

	return 0;
}

static void sc8960x_charger_shutdown(struct i2c_client *client)
{
/* Spruce code for OSPURCET-599 by zhangjb18 at 2022.12.06 start */
	int ret = 0;
	struct sc8960x *sc = i2c_get_clientdata(client);
	if(IS_ERR_OR_NULL(sc))
		return;

	ret = sc8960x_disable_charger(sc);
	if (ret) {
		pr_err("Failed to disable charge\n");
	}

	ret = sc8960x_enter_hiz_mode(sc);
	if (ret) {
		pr_err("Failed to enter hiz\n");
	}

	/*start  for reserved interface
	ret = sc8960x_set_batfet_delay(sc, 0);
	if (ret) {
		pr_err("Failed to BATFET_DLY\n");
	}

	ret = sc8960x_enable_batfet(sc);
	if (ret) {
		pr_err("Failed to BATFET DIS\n");
	}
	end*/

	pr_info("sc8960x_charger_shutdown\n");
/* Spruce code for OSPURCET-599 by zhangjb18 at 2022.12.06 end */
}

static struct i2c_driver sc8960x_charger_driver = {
	.driver = {
		   .name = "sc8960x-charger",
		   .owner = THIS_MODULE,
		   .of_match_table = sc8960x_charger_match_table,
		   },

	.probe = sc8960x_charger_probe,
	.remove = sc8960x_charger_remove,
	.shutdown = sc8960x_charger_shutdown,

};

module_i2c_driver(sc8960x_charger_driver);

MODULE_DESCRIPTION("SC SC8960x Charger Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("South Chip");
