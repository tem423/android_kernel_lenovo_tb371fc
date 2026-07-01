// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#include <linux/types.h>
#include <linux/init.h>		/* For init/exit macros */
#include <linux/module.h>	/* For MODULE_ marcros  */
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#endif
//#include <mt-plat/mtk_boot.h>
//#include <mt-plat/upmu_common.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/power_supply.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include "sgm415xx_buck_charge.h"
#include <v1/charger_class.h>
#include <v1/mtk_charger.h>
/**********************************************************
 *
 *   [I2C Slave Setting]
 *
 *********************************************************/

#define SGM4154x_REG_NUM    (0xF)

/* SGM4154x REG06 BOOST_LIM[5:4], uV */
static const unsigned int BOOST_VOLT_LIMIT[] = {
	4850000, 5000000, 5150000, 5300000
};
 /* SGM4154x REG02 BOOST_LIM[7:7], uA */
#if (defined(__SGM41542_CHIP_ID__) || defined(__SGM41541_CHIP_ID__)|| defined(__SGM41543_CHIP_ID__)|| defined(__SGM41543D_CHIP_ID__))
static const unsigned int BOOST_CURRENT_LIMIT[] = {
	1200000, 2000000
};
#else
static const unsigned int BOOST_CURRENT_LIMIT[] = {
	500000, 1200000
};
#endif

#if (defined(__SGM41513_CHIP_ID__) || defined(__SGM41513A_CHIP_ID__) || defined(__SGM41513D_CHIP_ID__))

static const unsigned int IPRECHG_CURRENT_STABLE[] = {
	5000, 10000, 15000, 20000, 30000, 40000, 50000, 60000,
	80000, 100000, 120000, 140000, 160000, 180000, 200000, 240000
};

static const unsigned int ITERM_CURRENT_STABLE[] = {
	5000, 10000, 15000, 20000, 30000, 40000, 50000, 60000,
	80000, 100000, 120000, 140000, 160000, 180000, 200000, 240000
};
#endif
#if 0
static enum power_supply_usb_type sgm4154x_usb_type[] = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP,	
};
#endif
static const struct charger_properties sgm4154x_chg_props = {
	.alias_name = SGM4154x_NAME,
};


enum {
	SGM_DP_DM_VOL_HIZ,
	SGM_DP_DM_VOL_0P0,
	SGM_DP_DM_VOL_0P6,
	SGM_DP_DM_VOL_3P3,
};

enum SGM4154x_QC_VOLT {
	QC_20_5000mV,
	QC_20_9000mV,
	QC_20_12000mV,
};

/**********************************************************
 *
 *   [Global Variable]
 *
 *********************************************************/
static struct charger_device *s_chg_dev_otg;

/**********************************************************
 *
 *   [I2C Function For Read/Write sgm4154x]
 *
 *********************************************************/
static int __sgm4154x_read_byte(struct sgm4154x_device *sgm, u8 reg, u8 *data)
{
    s32 ret;

    ret = i2c_smbus_read_byte_data(sgm->client, reg);
    if (ret < 0) {
        pr_err("i2c read fail: can't read from reg 0x%02X\n", reg);
        return ret;
    }

    *data = (u8) ret;

    return 0;
}

static int __sgm4154x_write_byte(struct sgm4154x_device *sgm, int reg, u8 val)
{
    s32 ret;

    ret = i2c_smbus_write_byte_data(sgm->client, reg, val);
    if (ret < 0) {
        pr_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n",
               val, reg, ret);
        return ret;
    }
    return 0;
}

static int sgm4154x_read_reg(struct sgm4154x_device *sgm, u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&sgm->i2c_rw_lock);
	ret = __sgm4154x_read_byte(sgm, reg, data);
	mutex_unlock(&sgm->i2c_rw_lock);

	return ret;
}
#if 0
static int sgm4154x_write_reg(struct sgm4154x_device *sgm, u8 reg, u8 val)
{
	int ret;

	mutex_lock(&sgm->i2c_rw_lock);
	ret = __sgm4154x_write_byte(sgm, reg, val);
	mutex_unlock(&sgm->i2c_rw_lock);

	if (ret)
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);

	return ret;
}
#endif
static int sgm4154x_update_bits(struct sgm4154x_device *sgm, u8 reg,
					u8 mask, u8 val)
{
	int ret;
	u8 tmp;

	mutex_lock(&sgm->i2c_rw_lock);
	ret = __sgm4154x_read_byte(sgm, reg, &tmp);
	if (ret) {
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= val & mask;

	ret = __sgm4154x_write_byte(sgm, reg, tmp);
	if (ret)
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);

out:
	mutex_unlock(&sgm->i2c_rw_lock);
	return ret;
}

/**********************************************************
 *
 *   [Internal Function]
 *
 *********************************************************/
static int sgm4154x_enable_term(struct sgm4154x_device *sgm, bool en)
{
	u8 reg_val;

	reg_val = en ? SGM4154x_TERM_EN : 0;
	return sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_5,
				  SGM4154x_TERM_EN, reg_val);
}

static int sgm4154x_set_hiz_en(struct sgm4154x_device *sgm, bool hiz_en)
{
	u8 reg_val;

	dev_notice(sgm->dev, "%s:%d", __func__, hiz_en);
	reg_val = hiz_en ? SGM4154x_HIZ_EN : 0;

	return sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_0,
				  SGM4154x_HIZ_EN, reg_val);
}

/*start  for reserved interface
static int sgm4154x_set_batfet_dis(struct sgm4154x_device *sgm, bool batfet_dis)
{
	u8 reg_val;

	dev_notice(sgm->dev, "%s:%d", __func__, batfet_dis);
	reg_val = batfet_dis ? SGM4154x_BATFET_DIS : 0;
	return sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_7, SGM4154x_BATFET_DIS, reg_val);
}

static int sgm4154x_set_batfet_dly(struct sgm4154x_device *sgm, bool batfet_dly)
{
	u8 reg_val;

	dev_notice(sgm->dev, "%s:%d", __func__, batfet_dly);
	reg_val = batfet_dly ? SGM4154x_BATFET_DLY : 0;
	return sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_7, SGM4154x_BATFET_DLY, reg_val);
}
*/

 static int sgm4154x_set_watchdog_timer(struct sgm4154x_device *sgm, int time)
{
	int ret;
	u8 reg_val;

	if (time == 0)
		reg_val = SGM4154x_WDT_TIMER_DISABLE;
	else if (time == 40)
		reg_val = SGM4154x_WDT_TIMER_40S;
	else if (time == 80)
		reg_val = SGM4154x_WDT_TIMER_80S;
	else
		reg_val = SGM4154x_WDT_TIMER_160S;

	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_5,
				SGM4154x_WDT_TIMER_MASK, reg_val);

	return ret;
}

 #if 0
 static int sgm4154x_get_term_curr(struct sgm4154x_device *sgm)
{
	int ret;
	u8 reg_val;
	int curr;
	int offset = SGM4154x_TERMCHRG_I_MIN_uA;

	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_3, &reg_val);
	if (ret)
		return ret;

	reg_val &= SGM4154x_TERMCHRG_CUR_MASK;
	curr = reg_val * SGM4154x_TERMCHRG_CURRENT_STEP_uA + offset;
	return curr;
}

static int sgm4154x_get_prechrg_curr(struct sgm4154x_device *sgm)
{
	int ret;
	u8 reg_val;
	int curr;
	int offset = SGM4154x_PRECHRG_I_MIN_uA;

	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_3, &reg_val);
	if (ret)
		return ret;

	reg_val = (reg_val&SGM4154x_PRECHRG_CUR_MASK)>>4;
	curr = reg_val * SGM4154x_PRECHRG_CURRENT_STEP_uA + offset;
	return curr;
}

static int sgm4154x_get_ichg_curr(struct sgm4154x_device *sgm)
{
	int ret;
	u8 ichg;
    unsigned int curr;

	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_2, &ichg);
	if (ret)
		return ret;

	ichg &= SGM4154x_ICHRG_I_MASK;
#if (defined(__SGM41513_CHIP_ID__) || defined(__SGM41513A_CHIP_ID__) || defined(__SGM41513D_CHIP_ID__))
	if (ichg <= 0x8)
		curr = ichg * 5000;
	else if (ichg <= 0xF)
		curr = 40000 + (ichg - 0x8) * 10000;
	else if (ichg <= 0x17)
		curr = 110000 + (ichg - 0xF) * 20000;
	else if (ichg <= 0x20)
		curr = 270000 + (ichg - 0x17) * 30000;
	else if (ichg <= 0x30)
		curr = 540000 + (ichg - 0x20) * 60000;
	else if (ichg <= 0x3C)
		curr = 1500000 + (ichg - 0x30) * 120000;
	else
		curr = 3000000;
#else
	curr = ichg * SGM4154x_ICHRG_I_STEP_uA;
#endif
	return curr;
}
#endif

static int sgm4154x_set_term_curr(struct sgm4154x_device *sgm, int uA)
{
	u8 reg_val;
#if (defined(__SGM41513_CHIP_ID__) || defined(__SGM41513A_CHIP_ID__) || defined(__SGM41513D_CHIP_ID__))

	for(reg_val = 1; reg_val < 16 && uA >= ITERM_CURRENT_STABLE[reg_val]; reg_val++)
		;
	reg_val--;
#else
	if (uA < SGM4154x_TERMCHRG_I_MIN_uA)
		uA = SGM4154x_TERMCHRG_I_MIN_uA;
	else if (uA > SGM4154x_TERMCHRG_I_MAX_uA)
		uA = SGM4154x_TERMCHRG_I_MAX_uA;

	reg_val = (uA - SGM4154x_TERMCHRG_I_MIN_uA) / SGM4154x_TERMCHRG_CURRENT_STEP_uA;
#endif

	return sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_3,
				  SGM4154x_TERMCHRG_CUR_MASK, reg_val);
}

static int sgm4154x_set_prechrg_curr(struct sgm4154x_device *sgm, int uA)
{
	u8 reg_val;

#if (defined(__SGM41513_CHIP_ID__) || defined(__SGM41513A_CHIP_ID__) || defined(__SGM41513D_CHIP_ID__))
	for(reg_val = 1; reg_val < 16 && uA >= IPRECHG_CURRENT_STABLE[reg_val]; reg_val++)
		;
	reg_val--;
#else
	if (uA < SGM4154x_PRECHRG_I_MIN_uA)
		uA = SGM4154x_PRECHRG_I_MIN_uA;
	else if (uA > SGM4154x_PRECHRG_I_MAX_uA)
		uA = SGM4154x_PRECHRG_I_MAX_uA;

	reg_val = (uA - SGM4154x_PRECHRG_I_MIN_uA) / SGM4154x_PRECHRG_CURRENT_STEP_uA;
#endif
	reg_val = reg_val << 4;
	return sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_3,
				  SGM4154x_PRECHRG_CUR_MASK, reg_val);
}

static int sgm4154x_set_ichrg_curr(struct sgm4154x_device *sgm, unsigned int uA)
{
	int ret;
	u8 reg_val;

	if (uA < SGM4154x_ICHRG_I_MIN_uA)
		uA = SGM4154x_ICHRG_I_MIN_uA;
	else if ( uA > sgm->init_data.max_ichg)
		uA = sgm->init_data.max_ichg;
#if (defined(__SGM41513_CHIP_ID__) || defined(__SGM41513A_CHIP_ID__) || defined(__SGM41513D_CHIP_ID__))
	if (uA <= 40000)
		reg_val = uA / 5000;
	else if (uA <= 110000)
		reg_val = 0x08 + (uA -40000) / 10000;
	else if (uA <= 270000)
		reg_val = 0x0F + (uA -110000) / 20000;
	else if (uA <= 540000)
		reg_val = 0x17 + (uA -270000) / 30000;
	else if (uA <= 1500000)
		reg_val = 0x20 + (uA -540000) / 60000;
	else if (uA <= 2940000)
		reg_val = 0x30 + (uA -1500000) / 120000;
	else
		reg_val = 0x3d;
#else
	reg_val = uA / SGM4154x_ICHRG_I_STEP_uA;
#endif
	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_2,
				  SGM4154x_ICHRG_I_MASK, reg_val);

	return ret;
}

static int sgm4154x_dev_set_ichrg_curr(struct charger_device *chg_dev, unsigned int uA)
{
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	return sgm4154x_set_ichrg_curr(sgm, uA);
}

static int sgm4154x_enable_power_path(struct charger_device *chg_dev, bool en)
{
	int reg_val, ret;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	reg_val = en ?  0 : SGM4154x_HIZ_EN;
	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_0,
				  SGM4154x_HIZ_EN, reg_val);
	if (ret)
		return ret;

	return ret;
}

static int sgm4154x_get_is_power_path_enable(struct charger_device *chg_dev, bool *en)
{
	u8 vreg_val;
	int ret;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_0, &vreg_val);
	if (ret)
		return ret;
	*en = !(vreg_val & SGM4154x_HIZ_EN)>>SGM4154x_HIZ_EN_SHIFT;

	return ret;
}

/* Spruce code for OSPURCET-450 by zhangjb18 at 2022.12.05 start */
static int sgm4154x_dev_set_hiz_en(struct charger_device *chg_dev, bool en)
{
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	return sgm4154x_set_hiz_en(sgm, en);
}
/* Spruce code for OSPURCET-450 by zhangjb18 at 2022.12.05 end */

static int sgm4154x_set_chrg_volt(struct sgm4154x_device *sgm, unsigned int chrg_volt)
{
	int ret;
	u8 reg_val;

	if (chrg_volt < SGM4154x_VREG_V_MIN_uV)
		chrg_volt = SGM4154x_VREG_V_MIN_uV;
	else if (chrg_volt > sgm->init_data.max_vreg)
		chrg_volt = sgm->init_data.max_vreg;

	reg_val = (chrg_volt-SGM4154x_VREG_V_MIN_uV) / SGM4154x_VREG_V_STEP_uV;
	reg_val = reg_val<<3;
	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_4,
				  SGM4154x_VREG_V_MASK, reg_val);

	return ret;
}

static int sgm4154x_dev_set_chrg_volt(struct charger_device *chg_dev, unsigned int chrg_volt)
{
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	return sgm4154x_set_chrg_volt(sgm, chrg_volt);
}

static int sgm4154x_get_chrg_volt(struct charger_device *chg_dev,unsigned int *volt)
{
	int ret;
	u8 vreg_val;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_4, &vreg_val);
	if (ret)
		return ret;

	vreg_val = (vreg_val & SGM4154x_VREG_V_MASK)>>3;

	if (15 == vreg_val)
		*volt = 4352000; //default
	else if (vreg_val < 25)
		*volt = vreg_val*SGM4154x_VREG_V_STEP_uV + SGM4154x_VREG_V_MIN_uV;

	return 0;
}

static int sgm4154x_get_vindpm_offset_os(struct sgm4154x_device *sgm)
{
	int ret;
	u8 reg_val;

	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_f, &reg_val);
	if (ret)
		return ret;

	reg_val = reg_val & SGM4154x_VINDPM_OS_MASK;

	return reg_val;
}

static int sgm4154x_set_vindpm_offset_os(struct sgm4154x_device *sgm,u8 offset_os)
{
	int ret;

	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_f,
				  SGM4154x_VINDPM_OS_MASK, offset_os);

	if (ret){
		pr_err("%s fail\n",__func__);
		return ret;
	}

	return ret;
}
static int sgm4154x_set_input_volt_lim(struct charger_device *chg_dev, unsigned int vindpm)
{
	int ret;
	unsigned int offset;
	u8 reg_val;
	u8 os_val;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	if (vindpm < SGM4154x_VINDPM_V_MIN_uV ||
	    vindpm > SGM4154x_VINDPM_V_MAX_uV)
 		return -EINVAL;

	if (vindpm < 5900000){
		os_val = 0;
		offset = 3900000;
	}
	else if (vindpm >= 5900000 && vindpm < 7500000){
		os_val = 1;
		offset = 5900000; //uv
	}
	else if (vindpm >= 7500000 && vindpm < 10500000){
		os_val = 2;
		offset = 7500000; //uv
	}
	else{
		os_val = 3;
		offset = 10500000; //uv
	}

	sgm4154x_set_vindpm_offset_os(sgm,os_val);
	reg_val = (vindpm - offset) / SGM4154x_VINDPM_STEP_uV;

	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_6,
				  SGM4154x_VINDPM_V_MASK, reg_val); 

	return ret;
}

static int sgm4154x_get_input_volt_lim(struct sgm4154x_device *sgm)
{
	int ret;
	int offset;
	u8 vlim;
	int temp;

	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_6, &vlim);
	if (ret)
		return ret;

	temp = sgm4154x_get_vindpm_offset_os(sgm);
	if (0 == temp)
		offset = 3900000; //uv
	else if (1 == temp)
		offset = 5900000;
	else if (2 == temp)
		offset = 7500000;
	else if (3 == temp)
		offset = 10500000;

	temp = offset + (vlim & 0x0F) * SGM4154x_VINDPM_STEP_uV;
	return temp;
}

static int sgm4154x_set_input_curr_lim(struct sgm4154x_device *sgm, unsigned int iindpm)
{
	int ret;
	u8 reg_val;

	if (iindpm < SGM4154x_IINDPM_I_MIN_uA ||
			iindpm > SGM4154x_IINDPM_I_MAX_uA)
		return -EINVAL;

#if (defined(__SGM41513_CHIP_ID__) || defined(__SGM41513A_CHIP_ID__) || defined(__SGM41513D_CHIP_ID__))
	reg_val = (iindpm-SGM4154x_IINDPM_I_MIN_uA) / SGM4154x_IINDPM_STEP_uA;
#else
	if (iindpm >= SGM4154x_IINDPM_I_MIN_uA && iindpm <= 3100000)//default
		reg_val = (iindpm-SGM4154x_IINDPM_I_MIN_uA) / SGM4154x_IINDPM_STEP_uA;
	else if (iindpm > 3100000 && iindpm < SGM4154x_IINDPM_I_MAX_uA)
		reg_val = 0x1E;
	else
		reg_val = SGM4154x_IINDPM_I_MASK;
#endif
	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_0,
				  SGM4154x_IINDPM_I_MASK, reg_val);
	return ret;
}

static int sgm4154x_dev_set_input_curr_lim(struct charger_device *chg_dev, unsigned int iindpm)
{
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	return sgm4154x_set_input_curr_lim(sgm, iindpm);
}

static int sgm4154x_get_input_curr_lim(struct charger_device *chg_dev,unsigned int *ilim)
{
	int ret;
	u8 reg_val;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_0, &reg_val);
	if (ret)
		return ret;
	if (SGM4154x_IINDPM_I_MASK == (reg_val & SGM4154x_IINDPM_I_MASK))
		*ilim =  SGM4154x_IINDPM_I_MAX_uA;
	else
		*ilim = (reg_val & SGM4154x_IINDPM_I_MASK)*SGM4154x_IINDPM_STEP_uA + SGM4154x_IINDPM_I_MIN_uA;

	return 0;
}

#if 0
static int32_t sgm4154x_set_dpdm(
	struct sgm4154x_device *sgm, uint8_t dp_val, uint8_t dm_val)
{
	uint8_t data_reg = 0;
	
	uint8_t mask = SGM4154x_DP_VSEL_MASK|SGM4154x_DM_VSEL_MASK;
	
	data_reg  = (dp_val & SGM4154x_DP_VSEL_MASK) << SGM4154x_DP_VOLT_SHIFT;
	data_reg |= (dm_val & SGM4154x_DM_VSEL_MASK) << SGM4154x_DM_VOLT_SHIFT;
	
	
	return sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_d,
				  mask, data_reg);

}
#endif

bool sgm4154x_is_hvdcp(struct sgm4154x_device *sgm,enum SGM4154x_QC_VOLT val)
{	
    int i = 20;	
	int vlim;
	u8 temp;

	vlim = sgm4154x_get_input_volt_lim(sgm);

	if (QC_20_9000mV == val)
	{
		sgm4154x_set_input_volt_lim(sgm->chg_dev,8000000); //8v
	}
	else if (QC_20_12000mV == val)
	{
		sgm4154x_set_input_volt_lim(sgm->chg_dev,11000000); //11v
	}
	else
	{
		sgm4154x_set_input_volt_lim(sgm->chg_dev,4500000); //4.5v
		return 0;
	}
	mdelay(1);
	while(i--){
	 	sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_a, &temp);

		if(0 == (temp&0x40)){
			return 1;
		}
		else if(1 == !!(temp&0x40) && i == 1){
			sgm4154x_set_input_volt_lim(sgm->chg_dev,vlim);
			return 0;
		}
		mdelay(10);
	}
	return 0;
}


static int sgm4154x_enable_charger(struct sgm4154x_device *sgm)
{
    int ret;

    ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_1, SGM4154x_CHRG_EN,
                     SGM4154x_CHRG_EN);

    return ret;
}

static int sgm4154x_disable_charger(struct sgm4154x_device *sgm)
{
    int ret;

    ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_1, SGM4154x_CHRG_EN,
                     0);
    return ret;
}

static int sgm4154x_charging_switch(struct charger_device *chg_dev,bool enable)
{
	int ret;
	u8 val;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	if (enable)
		ret = sgm4154x_enable_charger(sgm);
	else
		ret = sgm4154x_disable_charger(sgm);

	pr_err("%s sgm4154x charger %s\n", enable ? "enable" : "disable",
	       !ret ? "successfully" : "failed");

	ret = __sgm4154x_read_byte(sgm, SGM4154x_CHRG_CTRL_1, &val);
	if (!ret)
		sgm->charge_enabled = !!(val & SGM4154x_CHRG_EN);
	return ret;
}

static int sgm4154x_is_charging_enable(struct charger_device *chg_dev, bool *en)
{
	struct sgm4154x_device *sgm = dev_get_drvdata(&chg_dev->dev);

	*en = sgm->charge_enabled;

	return 0;
}

/* Spruce code for OSPURCET-2816 by yeyz5 at 2023.03.08 start */
static int sgm4154x_get_charging_current(struct charger_device *chg_dev, u32 *curr)
{
	int ret = 0;
	int ichg = 0;
	u8 reg_val = 0;
	struct sgm4154x_device *sgm = dev_get_drvdata(&chg_dev->dev);

	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_2, &reg_val);
	if (ret)
		return ret;
	ichg = reg_val & SGM4154x_ICHRG_I_MASK;
	if (ichg <= 0x08)
		*curr = ichg * 5000;
	else if (ichg <= 0x0F)
		*curr = 40000 + (10000 * (ichg - 0x08));
	else if (ichg <= 0x17)
		*curr = 110000 + (20000 * (ichg - 0x0F));
	else if (ichg <= 0x20)
		*curr = 270000 + (30000 * (ichg - 0x17));
	else if (ichg <= 0x30)
		*curr = 540000 + (60000 * (ichg - 0x20));
	else if (ichg < 0x3d)
		*curr = 1500000 + (120000 * (ichg - 0x30));
	else
		*curr = 3000000;

	pr_info("sgm4154x_get_charging_current curr = %d\n", *curr);

	return 0;
}
/* Spruce code for OSPURCET-2816 by yeyz5 at 2023.03.08 end */

static int sgm4154x_set_recharge_volt(struct sgm4154x_device *sgm, int mV)
{
	u8 reg_val;

	reg_val = (mV - SGM4154x_VRECHRG_OFFSET_mV) / SGM4154x_VRECHRG_STEP_mV;

	return sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_4,
				  SGM4154x_VRECHARGE, reg_val);
}

static int sgm4154x_set_wdt_rst(struct sgm4154x_device *sgm, bool is_rst)
{
	u8 val;

	if (is_rst)
		val = SGM4154x_WDT_RST_MASK;
	else
		val = 0;
	return sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_1,
				  SGM4154x_WDT_RST_MASK, val);
}

/**********************************************************
 *
 *   [Internal Function]
 *
 *********************************************************/
static int sgm4154x_dump_register(struct charger_device *chg_dev)
{
	unsigned char i = 0;
	unsigned int ret = 0;
	unsigned char sgm4154x_reg[SGM4154x_REG_NUM+1] = { 0 };
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	char buffer[1024] = {0};
	char *ptr = buffer;

	for (i = 0; i < SGM4154x_REG_NUM + 1; i++) {
		ret = sgm4154x_read_reg(sgm,i, &sgm4154x_reg[i]);
		if (ret == 0)
			ptr += sprintf(ptr, "[0x%0x=0x%0x]",
			       i, sgm4154x_reg[i]);
	}

	pr_err("[%s]%s\n",__func__, buffer);

	return 0;
}


/**********************************************************
 *
 *   [Internal Function]
 *
 *********************************************************/
static int sgm4154x_hw_chipid_detect(struct sgm4154x_device *sgm)
{
	int ret = 0;
	u8 val = 0;
	ret = sgm4154x_read_reg(sgm,SGM4154x_CHRG_CTRL_b,&val);
	if (ret < 0)
	{
		pr_info("[%s] read SGM4154x_CHRG_CTRL_b fail\n", __func__);
		return ret;
	}
	val = val & SGM4154x_PN_MASK;
	pr_info("[%s] Reg[0x0B]=0x%x\n", __func__,val);

	return val;
}

static int sgm4154x_reset_watch_dog_timer(struct charger_device
		*chg_dev)
{
	int ret;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	pr_info("charging_reset_watch_dog_timer\n");

	ret = sgm4154x_set_wdt_rst(sgm,0x1);	/* RST watchdog */

	return ret;
}


static int sgm4154x_get_charging_status(struct charger_device *chg_dev,
				       bool *is_done)
{
	//struct sgm4154x_state state;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	//sgm4154x_get_state(sgm, &state);

	if (sgm->state.chrg_stat == SGM4154x_TERM_CHRG)
		*is_done = true;
	else
		*is_done = false;

	return 0;
}

/* Spruce code for OSPURCET-2816 by yeyz5 at 2023.03.08 start */
static int sgm4154x_get_min_charging_current(struct charger_device *chg_dev,
		u32 *curr)
{
	*curr = 5 * 1000;

	return 0;
}
/* Spruce code for OSPURCET-2816 by yeyz5 at 2023.03.08 end */

static int sgm4154x_set_en_timer(struct sgm4154x_device *sgm)
{
	int ret;

	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_5,
				SGM4154x_SAFETY_TIMER_EN, SGM4154x_SAFETY_TIMER_EN);

	return ret;
}

static int sgm4154x_set_disable_timer(struct sgm4154x_device *sgm)
{
	int ret;

	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_5,
				SGM4154x_SAFETY_TIMER_EN, 0);

	return ret;
}

static int sgm4154x_enable_safetytimer(struct charger_device *chg_dev,bool en)
{
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	int ret = 0;

	if (en)
		ret = sgm4154x_set_en_timer(sgm);
	else
		ret = sgm4154x_set_disable_timer(sgm);
	return ret;
}

static int sgm4154x_get_is_safetytimer_enable(struct charger_device
		*chg_dev,bool *en)
{
	int ret = 0;
	u8 val = 0;

	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	ret = sgm4154x_read_reg(sgm,SGM4154x_CHRG_CTRL_5,&val);
	if (ret < 0)
	{
		pr_info("[%s] read SGM4154x_CHRG_CTRL_5 fail\n", __func__);
		return ret;
	}
	*en = !!(val & SGM4154x_SAFETY_TIMER_EN);
	return 0;
}

#if (defined(__SGM41542_CHIP_ID__)|| defined(__SGM41516D_CHIP_ID__)|| defined(__SGM41543D_CHIP_ID__))
static int sgm4154x_en_pe_current_partern(struct charger_device
		*chg_dev,bool is_up)
{
	int ret = 0;

	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_d,
				SGM4154x_EN_PUMPX, SGM4154x_EN_PUMPX);
	if (ret < 0)
	{
		pr_info("[%s] read SGM4154x_CHRG_CTRL_d fail\n", __func__);
		return ret;
	}
	if (is_up)
		ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_d,
				SGM4154x_PUMPX_UP, SGM4154x_PUMPX_UP);
	else
		ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_d,
				SGM4154x_PUMPX_DN, SGM4154x_PUMPX_DN);
	return ret;
}
#endif

static int sgm4154x_hw_init(struct sgm4154x_device *sgm)
{
	int ret = 0;	
	struct power_supply_battery_info bat_info = { };	

	bat_info.constant_charge_current_max_ua =
			SGM4154x_ICHRG_I_DEF_uA;

	bat_info.constant_charge_voltage_max_uv =
			SGM4154x_VREG_V_DEF_uV;

	bat_info.precharge_current_ua =
			SGM4154x_PRECHRG_I_DEF_uA;

	bat_info.charge_term_current_ua =
			SGM4154x_TERMCHRG_I_DEF_uA;

	sgm->init_data.max_ichg =
			SGM4154x_ICHRG_I_MAX_uA;

	sgm->init_data.max_vreg =
			SGM4154x_VREG_V_MAX_uV;

	sgm4154x_set_watchdog_timer(sgm,0);

	ret = sgm4154x_set_ichrg_curr(sgm,
				bat_info.constant_charge_current_max_ua);
	if (ret)
		goto err_out;

	ret = sgm4154x_set_prechrg_curr(sgm, bat_info.precharge_current_ua);
	if (ret)
		goto err_out;

	ret = sgm4154x_set_chrg_volt(sgm,
				bat_info.constant_charge_voltage_max_uv);
	if (ret)
		goto err_out;
	ret = sgm4154x_set_term_curr(sgm, bat_info.charge_term_current_ua);
	if (ret)
		goto err_out;

	ret = sgm4154x_set_input_curr_lim(sgm, sgm->init_data.ilim);
	if (ret)
		goto err_out;

	ret = sgm4154x_set_recharge_volt(sgm, 200);//100~200mv
	if (ret)
		goto err_out;

	ret = sgm4154x_set_hiz_en(sgm, false);
	if (ret)
		goto err_out;

	ret = sgm4154x_enable_term(sgm, false);
	if (ret)
		goto err_out;

/* Spruce code for OSPURCET-1500 by gujy6 at 2023.03.03 start */
	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_1, SGM4154x_CHRG_EN,
                     false);
	if (ret)
		goto err_out;
/* Spruce code for OSPURCET-1500 by gujy6 at 2023.03.03 end */

	dev_notice(sgm->dev, "ichrg_curr:%d prechrg_curr:%d chrg_vol:%d"
		" term_curr:%d input_curr_lim:%d",
		bat_info.constant_charge_current_max_ua,
		bat_info.precharge_current_ua,
		bat_info.constant_charge_voltage_max_uv,
		bat_info.charge_term_current_ua,
		sgm->init_data.ilim);

	return 0;

err_out:
	return ret;

}

static int sgm4154x_parse_dt(struct sgm4154x_device *sgm)
{
	int ret;

	ret = device_property_read_u32(sgm->dev,
				       "input-voltage-limit-microvolt",
				       &sgm->init_data.vlim);
	if (ret)
		sgm->init_data.vlim = SGM4154x_VINDPM_DEF_uV;

	if (sgm->init_data.vlim > SGM4154x_VINDPM_V_MAX_uV ||
	    sgm->init_data.vlim < SGM4154x_VINDPM_V_MIN_uV)
		return -EINVAL;

	ret = device_property_read_u32(sgm->dev,
				       "input-current-limit-microamp",
				       &sgm->init_data.ilim);
	if (ret)
		sgm->init_data.ilim = SGM4154x_IINDPM_DEF_uA;

	if (sgm->init_data.ilim > SGM4154x_IINDPM_I_MAX_uA ||
	    sgm->init_data.ilim < SGM4154x_IINDPM_I_MIN_uA)
		return -EINVAL;

	sgm->en_chip_gpio = of_get_named_gpio(sgm->dev->of_node, "sgm,chg-en-gpio", 0);
	if (!gpio_is_valid(sgm->en_chip_gpio)) {
		dev_err(sgm->dev, "Failed to get the sgm en chip-gpios\n");
	}

	if(gpio_is_valid(sgm->en_chip_gpio)) {
		ret = devm_gpio_request_one(sgm->dev, sgm->en_chip_gpio, GPIOF_OUT_INIT_LOW, //default enable
			"sgm_en_gpio");
	}
	return 0;
}

static int sgm4154x_enable_vbus(struct regulator_dev *rdev)
{
	int ret = 0;
	struct sgm4154x_device *sgm = charger_get_data(s_chg_dev_otg);

	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_1, SGM4154x_OTG_EN,
                     SGM4154x_OTG_EN);
	return ret;
}

static int sgm4154x_disable_vbus(struct regulator_dev *rdev)
{
	int ret = 0;
	struct sgm4154x_device *sgm = charger_get_data(s_chg_dev_otg);

	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_1, SGM4154x_OTG_EN,
                     0);

	return ret;
}

static int sgm4154x_enable_otg(struct charger_device *chg_dev, bool en)
{
	int ret = 0;

	pr_info("%s en = %d\n", __func__, en);
	if (en) {
		ret = sgm4154x_enable_vbus(NULL);
	} else {
		ret = sgm4154x_disable_vbus(NULL);
	}
	return ret;
}

static int sgm4154x_set_boost_current_limit(struct charger_device *chg_dev, u32 uA)
{	
	int ret = 0;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	if (uA == BOOST_CURRENT_LIMIT[0]){
		ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_2, SGM4154x_BOOST_LIM,
                     0); 
	}

	else if (uA == BOOST_CURRENT_LIMIT[1]){
		ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_2, SGM4154x_BOOST_LIM,
                     BIT(7)); 
	}
	return ret;
}

static struct charger_ops sgm4154x_chg_ops = {
	.enable_hz = sgm4154x_dev_set_hiz_en,

	/* Normal charging */
	.dump_registers = sgm4154x_dump_register,
	.enable = sgm4154x_charging_switch,
	.is_enabled = sgm4154x_is_charging_enable,
  /* Spruce code for OSPURCET-2816 by yeyz5 at 2023.03.08 start */
	.get_charging_current = sgm4154x_get_charging_current,
  /* Spruce code for OSPURCET-2816 by yeyz5 at 2023.03.08 end */
	.set_charging_current = sgm4154x_dev_set_ichrg_curr,
	.get_input_current = sgm4154x_get_input_curr_lim,
	.set_input_current = sgm4154x_dev_set_input_curr_lim,
	.get_constant_voltage = sgm4154x_get_chrg_volt,
	.set_constant_voltage = sgm4154x_dev_set_chrg_volt,
	.kick_wdt = sgm4154x_reset_watch_dog_timer,
	.set_mivr = sgm4154x_set_input_volt_lim,
	.is_charging_done = sgm4154x_get_charging_status,
  /* Spruce code for OSPURCET-2816 by yeyz5 at 2023.03.08 start */
	.get_min_charging_current = sgm4154x_get_min_charging_current,
  /* Spruce code for OSPURCET-2816 by yeyz5 at 2023.03.08 end */

	/* Safety timer */
	.enable_safety_timer = sgm4154x_enable_safetytimer,
	.is_safety_timer_enabled = sgm4154x_get_is_safetytimer_enable,

	/* Power path */
	.enable_powerpath = sgm4154x_enable_power_path,
	.is_powerpath_enabled = sgm4154x_get_is_power_path_enable,


	/* Chip enable*/
	.enable_chip = sgm4154x_charging_switch,
	.is_chip_enabled = sgm4154x_is_charging_enable,

	/* OTG */
	.enable_otg = sgm4154x_enable_otg,
	.set_boost_current_limit = sgm4154x_set_boost_current_limit,
	//.event = sgm4154x_do_event,

	/* PE+/PE+20 */
#if (defined(__SGM41542_CHIP_ID__)|| defined(__SGM41516D_CHIP_ID__)|| defined(__SGM41543D_CHIP_ID__))
	.send_ta_current_pattern = sgm4154x_en_pe_current_partern,
#else
	.send_ta_current_pattern = NULL,
#endif
	.set_pe20_efficiency_table = NULL,
	.send_ta20_current_pattern = NULL,
//	.set_ta20_reset = NULL,
	.enable_cable_drop_comp = NULL,
};

static int sgm4154x_driver_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	int ret = 0;
	struct device *dev = &client->dev;
	struct sgm4154x_device *sgm;

	pr_info("[%s]\n", __func__);

	sgm = devm_kzalloc(dev, sizeof(*sgm), GFP_KERNEL);
	if (!sgm)
		return -ENOMEM;

	sgm->client = client;
	sgm->dev = dev;

	mutex_init(&sgm->lock);
	mutex_init(&sgm->i2c_rw_lock);

	i2c_set_clientdata(client, sgm);

	ret = sgm4154x_hw_chipid_detect(sgm);
	if (ret != 8){
		pr_info("[%s] device not found ID=%d!!!\n", __func__, ret);
		return ret;
	}

	ret = sgm4154x_parse_dt(sgm);
	if (ret)
		return ret;

	ret = sgm4154x_hw_init(sgm);
	if (ret) {
		pr_err("Failed to init device\n");
		return ret;
	}

	/* Register charger device */
	sgm->chg_dev = charger_device_register("secondary_chg",
						&client->dev, sgm,
						&sgm4154x_chg_ops,
						&sgm4154x_chg_props);
	if (IS_ERR_OR_NULL(sgm->chg_dev)) {
		pr_info("%s: register charger device  failed\n", __func__);
		ret = PTR_ERR(sgm->chg_dev);
		return ret;
	}

	hardinfo_set_vendor_chginfo(HRADINFO_BUCK_CHG_INFO, SGM41513);

	return 0;

}

static int sgm4154x_charger_remove(struct i2c_client *client)
{
	struct sgm4154x_device *sgm = i2c_get_clientdata(client);

	regulator_unregister(sgm->otg_rdev);

	power_supply_unregister(sgm->charger); 

	mutex_destroy(&sgm->lock);
	mutex_destroy(&sgm->i2c_rw_lock);

	return 0;
}

static void sgm4154x_charger_shutdown(struct i2c_client *client)
{
	int ret = 0;

	struct sgm4154x_device *sgm = i2c_get_clientdata(client);
	if(IS_ERR_OR_NULL(sgm))
		return;
	ret = sgm4154x_disable_charger(sgm);
	if (ret) {
		pr_err("Failed to disable charger, ret = %d\n", ret);
	}
/* Spruce code for OSPURCET-599 by zhangjb18 at 2022.12.06 start */
	ret = sgm4154x_set_hiz_en(sgm, true);
	if (ret) {
		pr_err("Failed to hiz charger, ret = %d\n", ret);
	}
/* Spruce code for OSPURCET-599 by zhangjb18 at 2022.12.06 end */

/*start  for reserved interface
	ret =sgm4154x_set_batfet_dly(sgm, false);
	if (ret) {
		pr_err("Failed to BATFET_DLY\n");
	}

	ret = sgm4154x_set_batfet_dis(sgm, true);
	if (ret) {
		pr_err("Failed to BATFET DIS\n");
	}
end*/

	pr_info("sgm4154x_charger_shutdown\n");
}

static const struct i2c_device_id sgm4154x_i2c_ids[] = {
	{ "sgm41541", 0 },
	{ "sgm41542", 1 },
	{ "sgm41543", 2 },
	{ "sgm41543D", 3 },
	{ "sgm41513", 4 },
	{ "sgm41513A", 5 },
	{ "sgm41513D", 6 },
	{ "sgm41516", 7 },
	{ "sgm41516D", 8 },
	{},
};
MODULE_DEVICE_TABLE(i2c, sgm4154x_i2c_ids);

static const struct of_device_id sgm4154x_of_match[] = {
	{ .compatible = "sgm,sgm41541", },
	{ .compatible = "sgm,sgm41542", },
	{ .compatible = "sgm,sgm41543", },
	{ .compatible = "sgm,sgm41543D", },
	{ .compatible = "sgm,sgm41513", },
	{ .compatible = "sgm,sgm41513A", },
	{ .compatible = "sgm,sgm41513D", },
	{ .compatible = "sgm,sgm41516", },
	{ .compatible = "sgm,sgm41516D", },
	{ },
};
MODULE_DEVICE_TABLE(of, sgm4154x_of_match);


static struct i2c_driver sgm4154x_driver = {
	.driver = {
		.name = "sgm4154x-charger",
		.of_match_table = sgm4154x_of_match,
	},
	.probe = sgm4154x_driver_probe,
	.remove = sgm4154x_charger_remove,
	.shutdown = sgm4154x_charger_shutdown,
	.id_table = sgm4154x_i2c_ids,
};
module_i2c_driver(sgm4154x_driver);

MODULE_AUTHOR(" qhq <Allen_qin@sg-micro.com>");
MODULE_DESCRIPTION("sgm4154x charger driver");
MODULE_LICENSE("GPL v2");
