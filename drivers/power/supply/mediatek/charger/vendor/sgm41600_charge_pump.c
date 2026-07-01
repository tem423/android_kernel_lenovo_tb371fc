// SPDX-License-Identifier: GPL-2.0
// SGM41600 driver version 2021-08-17 V01
// Copyright (C) 2021 Texas Instruments Incorporated - http://www.sg-micro.com
#include <linux/regmap.h>
#include <linux/types.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/usb/phy.h>
#include <linux/acpi.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/power_supply.h>
#include <v1/mtk_charger.h>
#include "sgm41600_charge_pump.h"
#include "pd_policy_manager.h"


static const u32 s_sgm41600_wdt_mS[] = {500,1000,2000,5000,10000,20000,40000,80000};

static const u32 s_sgm41600_vbatreg_mV[] = {50,100,150,200};

static const u32 s_sgm41600_ibatreg_mA[] = {50,100,150,200};

static const u32 s_freq_hz[] = {200,375,500,750,1000,1250,1500};

#define SGM41600_ROLE_STDALONE   0
#define SGM41600_ROLE_SLAVE	     1
#define SGM41600_ROLE_MASTER	 2

enum {
	SGM41600_STDALONE,
	SGM41600_SLAVE,
	SGM41600_MASTER,
};

int sgm41600_mode_data[] = {
	[SGM41600_STDALONE] = SGM41600_ROLE_STDALONE,
	[SGM41600_SLAVE] = SGM41600_ROLE_SLAVE,
	[SGM41600_MASTER] = SGM41600_ROLE_MASTER,
};

enum SGM41600_BIT_OPTION {
	/* REG 00*/	
	F_BIT_REG_RST,
	F_BIT_CHG_MODE,
	F_BIT_WDT_DIS,
	F_BIT_WDT_TIMER,
	
	/* REG 01*/
	F_BIT_FSW_SET,
	F_BIT_FSW_SHIFT,
	F_BIT_CONV_OCP_THRE_SEL,
	F_BIT_CONV_OCP_TIME_SEL,
	F_BIT_CONV_OCP_RST_EN,
	
	/* REG 02*/
	F_BIT_PIN_DIAG_EN,
	F_BIT_VBUS_LO_EN,
	F_BIT_VBUS_HI_EN,
	F_BIT_VBUS_LO,
	F_BIT_VBUS_HI,
	
	/* REG 03*/	
	F_BIT_DEV_REV,
	F_BIT_DEV_ID,
	/* REG 04*/	
	F_BIT_AC_OVP_EN,
	F_BIT_AC_OVP,
	
	/* REG 05*/
	F_BIT_AC_PDN_EN,
	F_BIT_BUS_PDN_EN,
	F_BIT_VDRP_OVP_EN,
	F_BIT_VDRP_OVP_DEG,
	F_BIT_VDRP_OVP,
	
	/* REG 06*/
	F_BIT_BUS_OVP_EN,
	F_BIT_BUS_OVP,
	
	/* REG 07*/
	F_BIT_IBUS_UCP_EN,
	F_BIT_IBUS_UCP,
	F_BIT_IBUS_OCP_EN,
	F_BIT_IBUS_OCP,	
	
	/* REG 08 */
	F_BIT_BAT_OVP_EN,
	F_BIT_BAT_OVP,	
	
	/* REG 09 */
	F_BIT_IBAT_OCP_EN,
	F_BIT_IBAT_OCP,	
	
	/* REG 0A*/
	F_BIT_REG_TIMEOUT_DIS,
	F_BIT_IBAT_REG_EN,
	F_BIT_IBAT_REG,
	F_BIT_VBAT_REG_EN,
	F_BIT_VBAT_REG,	
	
	/* REG 0B*/
	F_BIT_AC_OVP_FLAG,
	F_BIT_AC_PDN_FLAG,
	F_BIT_BUS_PDN_FLAG,
	F_BIT_VDRP_OVP_FLAG,
	F_BIT_BUS_OVP_FLAG,
	F_BIT_IBUS_OCP_FLAG,
	F_BIT_IBUS_UCP_RISE_FLAG,
	F_BIT_IBUS_UCP_FALL_FLAG,
	
	/* REG 0C*/
	F_BIT_AC_OVP_MASK,
	F_BIT_AC_PDN_MASK,
	F_BIT_BUS_PDN_MASK,
	F_BIT_VDRP_OVP_MASK,
	F_BIT_BUS_OVP_MASK,
	F_BIT_IBUS_OCP_MASK,
	F_BIT_IBUS_UCP_RISE_MASK,
	F_BIT_IBUS_UCP_FALL_MASK,
	
	/* REG 0D*/	
	F_BIT_BAT_OVP_FLAG,
	F_BIT_IBAT_OCP_FLAG,
	F_BIT_VBAT_REG_FLAG,
	F_BIT_IBAT_REG_FLAG,
	F_BIT_TDIE_OTP_FLAG,
	F_BIT_VBUS_LO_FLAG,
	F_BIT_VBUS_HI_FLAG,
	F_BIT_CONV_OCP_FLAG,

	/* REG 0E*/	
	F_BIT_BAT_OVP_MASK,
	F_BIT_IBAT_OCP_MASK,
	F_BIT_VBAT_REG_MASK,
	F_BIT_IBAT_REG_MASK,
	F_BIT_TDIE_OTP_MASK,
	F_BIT_VBUS_LO_MASK,
	F_BIT_VBUS_HI_MASK,
	F_BIT_CONV_OCP_MASK,
	
	/* REG 0F*/	
	F_BIT_BUS_INSERT_FLAG,
	F_BIT_BAT_INSERT_FLAG,
	F_BIT_WD_TIMEOUT_FLAG,
	F_BIT_AC_ABSENT_FLAG,
	F_BIT_BUS_ABSENT_FLAG,
	F_BIT_IBUS_UCP_TIMEOUT_FLAG,
	F_BIT_ADC_DONE_FLAG,
	F_BIT_PIN_DIAG_FLAG,
	
	/* REG 10*/	
	F_BIT_BUS_INSERT_MASK,
	F_BIT_BAT_INSERT_MASK,
	F_BIT_WD_TIMEOUT_MASK,
	F_BIT_AC_ABSENT_MASK,
	F_BIT_BUS_ABSENT_MASK,
	F_BIT_IBUS_UCP_TIMEOUT_MASK,
	F_BIT_ADC_DONE_MASK,
	F_BIT_PIN_DIAG_MASK,
	
	/* REG 11*/	
	F_BIT_ADC_EN,
	F_BIT_ADC_RATE,
	F_BIT_VBUS_ADC_DIS,
	F_BIT_IBUS_ADC_DIS,
	F_BIT_VBAT_ADC_DIS,
	F_BIT_IBAT_ADC_DIS,
	F_BIT_TDIE_ADC_DIS,
	
	/* REG 12*/	
	F_BIT_H_VBUS_ADC,
	
	/* REG 13*/	
	F_BIT_L_VBUS_ADC,
	
	/* REG 14*/	
	F_BIT_H_IBUS_ADC,
	
	/* REG 15*/	
	F_BIT_L_IBUS_ADC,
	
	/* REG 16*/	
	F_BIT_H_VBAT_ADC,
	
	/* REG 17*/	
	F_BIT_L_VBAT_ADC,
	
	/* REG 18*/	
	F_BIT_H_IBAT_ADC,
	
	/* REG 19*/	
	F_BIT_L_IBAT_ADC,
	
	/* REG 1A*/	
	F_BIT_TDIE_ADC,
	
	/* REG 36*/	
	F_BIT_DP_DAC,
	F_BIT_DM_DAC,
	F_BIT_EN_HVDCP,
	
	F_BIT_MAX,
};

static const struct sgm41600_fields sgm41600_regs[F_BIT_MAX] = {	
		[F_BIT_REG_RST]               = {0x00,7,GENMASK(7, 7)},
		[F_BIT_CHG_MODE]              = {0x00,4,GENMASK(6, 4)},
		[F_BIT_WDT_DIS]               = {0x00,3,GENMASK(3, 3)},
		[F_BIT_WDT_TIMER]             = {0x00,0,GENMASK(2, 0)},
		[F_BIT_FSW_SET]               = {0x01,5,GENMASK(7, 5)},
		[F_BIT_FSW_SHIFT]             = {0x01,3,GENMASK(4, 3)},
		[F_BIT_CONV_OCP_THRE_SEL]     = {0x01,2,GENMASK(2, 2)},
		[F_BIT_CONV_OCP_TIME_SEL]     = {0x01,1,GENMASK(1, 1)},
		[F_BIT_CONV_OCP_RST_EN]       = {0x01,0,GENMASK(0, 0)},			
		[F_BIT_PIN_DIAG_EN]           = {0x02,7,GENMASK(7, 7)},
		[F_BIT_VBUS_LO_EN]            = {0x02,5,GENMASK(5, 5)},
		[F_BIT_VBUS_HI_EN]            = {0x02,4,GENMASK(4, 4)},
		[F_BIT_VBUS_LO]               = {0x02,2,GENMASK(3, 2)},
		[F_BIT_VBUS_HI]               = {0x02,0,GENMASK(1, 0)},	
		[F_BIT_DEV_REV]               = {0x03,4,GENMASK(7, 4)},
		[F_BIT_DEV_ID]                = {0x03,0,GENMASK(3, 0)},		
		[F_BIT_AC_OVP_EN]             = {0x04,4,GENMASK(4, 4)},
		[F_BIT_AC_OVP]                = {0x04,0,GENMASK(3, 0)},		
		[F_BIT_AC_PDN_EN]             = {0x05,7,GENMASK(7, 7)},
		[F_BIT_BUS_PDN_EN]            = {0x05,6,GENMASK(6, 6)},
		[F_BIT_VDRP_OVP_EN]           = {0x05,5,GENMASK(5, 5)},
		[F_BIT_VDRP_OVP_DEG]          = {0x05,4,GENMASK(4, 4)},
		[F_BIT_VDRP_OVP]              = {0x05,0,GENMASK(2, 0)},	
		[F_BIT_BUS_OVP_EN]            = {0x06,7,GENMASK(7, 7)},
		[F_BIT_BUS_OVP]               = {0x06,0,GENMASK(6, 0)},		
		[F_BIT_IBUS_UCP_EN]           = {0x07,7,GENMASK(7, 7)},
		[F_BIT_IBUS_UCP]              = {0x07,6,GENMASK(6, 6)},
		[F_BIT_IBUS_OCP_EN]           = {0x07,5,GENMASK(5, 5)},
		[F_BIT_IBUS_OCP]              = {0x07,0,GENMASK(4, 0)},
		[F_BIT_BAT_OVP_EN]            = {0x08,7,GENMASK(7, 7)},
		[F_BIT_BAT_OVP]               = {0x08,0,GENMASK(5, 0)},
		[F_BIT_IBAT_OCP_EN]           = {0x09,7,GENMASK(7, 7)},
		[F_BIT_IBAT_OCP]              = {0x09,0,GENMASK(5, 0)},		
		[F_BIT_REG_TIMEOUT_DIS]       = {0x0A,6,GENMASK(6, 6)},
		[F_BIT_IBAT_REG_EN]           = {0x0A,5,GENMASK(5, 5)},
		[F_BIT_IBAT_REG]              = {0x0A,3,GENMASK(4, 3)},
		[F_BIT_VBAT_REG_EN]           = {0x0A,2,GENMASK(2, 2)},
		[F_BIT_VBAT_REG]              = {0x0A,0,GENMASK(1, 0)},
		[F_BIT_AC_OVP_FLAG]           = {0x0B,7,GENMASK(7, 7)},
		[F_BIT_AC_PDN_FLAG]           = {0x0B,6,GENMASK(6, 6)},
		[F_BIT_BUS_PDN_FLAG]          = {0x0B,5,GENMASK(5, 5)},
		[F_BIT_VDRP_OVP_FLAG]         = {0x0B,4,GENMASK(4, 4)},
		[F_BIT_BUS_OVP_FLAG]          = {0x0B,3,GENMASK(3, 3)},
		[F_BIT_IBUS_OCP_FLAG]         = {0x0B,2,GENMASK(2, 2)},
		[F_BIT_IBUS_UCP_RISE_FLAG]    = {0x0B,1,GENMASK(1, 1)},
		[F_BIT_IBUS_UCP_FALL_FLAG]    = {0x0B,0,GENMASK(0, 0)},		
		[F_BIT_AC_OVP_MASK]           = {0x0C,7,GENMASK(7, 7)},
		[F_BIT_AC_PDN_MASK]           = {0x0C,6,GENMASK(6, 6)},
		[F_BIT_BUS_PDN_MASK]          = {0x0C,5,GENMASK(5, 5)},
		[F_BIT_VDRP_OVP_MASK]         = {0x0C,4,GENMASK(4, 4)},
		[F_BIT_BUS_OVP_MASK]       	  = {0x0C,3,GENMASK(3, 3)},
		[F_BIT_IBUS_OCP_MASK]         = {0x0C,2,GENMASK(2, 2)},
		[F_BIT_IBUS_UCP_RISE_MASK]    = {0x0C,1,GENMASK(1, 1)},
		[F_BIT_IBUS_UCP_FALL_MASK]    = {0x0C,0,GENMASK(0, 0)},		
		[F_BIT_BAT_OVP_FLAG]          = {0x0D,7,GENMASK(7, 7)},
		[F_BIT_IBAT_OCP_FLAG]         = {0x0D,6,GENMASK(6, 6)},
		[F_BIT_VBAT_REG_FLAG]         = {0x0D,5,GENMASK(5, 5)},
		[F_BIT_IBAT_REG_FLAG]         = {0x0D,4,GENMASK(4, 4)},
		[F_BIT_TDIE_OTP_FLAG]         = {0x0D,3,GENMASK(3, 3)},
		[F_BIT_VBUS_LO_FLAG]          = {0x0D,2,GENMASK(2, 2)},
		[F_BIT_VBUS_HI_FLAG]          = {0x0D,1,GENMASK(1, 1)},
		[F_BIT_CONV_OCP_FLAG]         = {0x0D,0,GENMASK(0, 0)},
		[F_BIT_BAT_OVP_MASK]          = {0x0E,7,GENMASK(7, 7)},			
		[F_BIT_IBAT_OCP_MASK]         = {0x0E,6,GENMASK(6, 6)},
		[F_BIT_VBAT_REG_MASK]         = {0x0E,5,GENMASK(5, 5)},
		[F_BIT_IBAT_REG_MASK]         = {0x0E,4,GENMASK(4, 4)},
		[F_BIT_TDIE_OTP_MASK]         = {0x0E,3,GENMASK(3, 3)},
		[F_BIT_VBUS_LO_MASK]          = {0x0E,2,GENMASK(2, 2)},	
		[F_BIT_VBUS_HI_MASK]          = {0x0E,1,GENMASK(1, 1)},
		[F_BIT_CONV_OCP_MASK]         = {0x0E,0,GENMASK(0, 0)},
	    [F_BIT_BUS_INSERT_FLAG]       = {0x0F,7,GENMASK(7, 7)},
		[F_BIT_BAT_INSERT_FLAG]       = {0x0F,6,GENMASK(6, 6)},
		[F_BIT_WD_TIMEOUT_FLAG]       = {0x0F,5,GENMASK(5, 5)},
		[F_BIT_AC_ABSENT_FLAG]        = {0x0F,4,GENMASK(4, 4)},
		[F_BIT_BUS_ABSENT_FLAG]       = {0x0F,3,GENMASK(3, 3)},
		[F_BIT_IBUS_UCP_TIMEOUT_FLAG] = {0x0F,2,GENMASK(2, 2)},
		[F_BIT_ADC_DONE_FLAG]         = {0x0F,1,GENMASK(1, 1)},
		[F_BIT_PIN_DIAG_FLAG]         = {0x0F,0,GENMASK(0, 0)},
		[F_BIT_BUS_INSERT_MASK]       = {0x10,7,GENMASK(7, 7)},
		[F_BIT_BAT_INSERT_MASK]       = {0x10,6,GENMASK(6, 6)},
		[F_BIT_WD_TIMEOUT_MASK]       = {0x10,5,GENMASK(5, 5)},
		[F_BIT_AC_ABSENT_MASK]        = {0x10,4,GENMASK(4, 4)},
		[F_BIT_BUS_ABSENT_MASK]       = {0x10,3,GENMASK(3, 3)},
		[F_BIT_IBUS_UCP_TIMEOUT_MASK] = {0x10,2,GENMASK(2, 2)},
		[F_BIT_ADC_DONE_MASK]         = {0x10,1,GENMASK(1, 1)},
		[F_BIT_PIN_DIAG_MASK]         = {0x10,0,GENMASK(0, 0)},
		[F_BIT_ADC_EN]                = {0x11,7,GENMASK(7, 7)},
		[F_BIT_ADC_RATE]              = {0x11,6,GENMASK(6, 6)},
		[F_BIT_VBUS_ADC_DIS]          = {0x11,5,GENMASK(5, 5)},
		[F_BIT_IBUS_ADC_DIS]          = {0x11,4,GENMASK(4, 4)},
		[F_BIT_VBAT_ADC_DIS]          = {0x11,3,GENMASK(3, 3)},
		[F_BIT_IBAT_ADC_DIS]          = {0x11,2,GENMASK(2, 2)},
		[F_BIT_TDIE_ADC_DIS]          = {0x11,1,GENMASK(1, 1)},
		[F_BIT_H_VBUS_ADC]            = {0x12,0,GENMASK(3, 0)},
		[F_BIT_L_VBUS_ADC]            = {0x13,0,GENMASK(7, 0)},
		[F_BIT_H_IBUS_ADC]            = {0x14,0,GENMASK(3, 0)},
		[F_BIT_L_IBUS_ADC]            = {0x15,0,GENMASK(7, 0)},	
		[F_BIT_H_VBAT_ADC]            = {0x16,0,GENMASK(3, 0)},
		[F_BIT_L_VBAT_ADC]            = {0x17,0,GENMASK(7, 0)},
		[F_BIT_H_IBAT_ADC]            = {0x18,0,GENMASK(3, 0)},
		[F_BIT_L_IBAT_ADC]            = {0x19,0,GENMASK(7, 0)},
		[F_BIT_TDIE_ADC]              = {0x1A,0,GENMASK(7, 0)},
		[F_BIT_DP_DAC]                = {0x36,5,GENMASK(7, 5)},
		[F_BIT_DM_DAC]                = {0x36,2,GENMASK(4, 2)},
		[F_BIT_EN_HVDCP]              = {0x36,1,GENMASK(1, 1)},
};

/**********************************************************
 *
 *   [I2C Function For Read/Write sgm41600_device]
 *
 *********************************************************/
static int __sgm41600_read_byte(struct sgm41600_device *sgm, u8 reg, u8 *data)
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

static int __sgm41600_write_byte(struct sgm41600_device *sgm, int reg, u8 val)
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

static int sgm41600_read_reg(struct sgm41600_device *sgm, u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&sgm->i2c_rw_lock);
	ret = __sgm41600_read_byte(sgm, reg, data);
	mutex_unlock(&sgm->i2c_rw_lock);

	return ret;
}
#if 0
static int sgm41600_write_reg(struct sgm41600_device *sgm, u8 reg, u8 val)
{
	int ret;

	mutex_lock(&sgm->i2c_rw_lock);
	ret = __sgm41600_write_byte(sgm, reg, val);
	mutex_unlock(&sgm->i2c_rw_lock);

	if (ret)
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);

	return ret;
}
#endif
static int sgm41600_update_bits(struct sgm41600_device *sgm, u8 reg,
					u8 mask, u8 val)
{
	int ret;
	u8 tmp;

	mutex_lock(&sgm->i2c_rw_lock);
	ret = __sgm41600_read_byte(sgm, reg, &tmp);
	if (ret) {
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= val & mask;

	ret = __sgm41600_write_byte(sgm, reg, tmp);
	if (ret)
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);

out:
	mutex_unlock(&sgm->i2c_rw_lock);
	return ret;
}

static int sgm41600_field_read(struct sgm41600_device *sgm,
			      enum SGM41600_BIT_OPTION field_id)
{
	int ret;
	u8 val;	

	ret = sgm41600_read_reg(sgm,sgm41600_regs[field_id].reg, &val);
	if (ret < 0)
		return ret;
	
	val &= sgm41600_regs[field_id].mask;
	val >>= sgm41600_regs[field_id].shift;

	return val;
}

static int sgm41600_field_write(struct sgm41600_device *sgm,
			       enum SGM41600_BIT_OPTION field_id, u8 val)
{
	return sgm41600_update_bits(sgm,sgm41600_regs[field_id].reg,
									sgm41600_regs[field_id].mask,
									val<<sgm41600_regs[field_id].shift);
}

static int sgm41600_get_vbus_adc(struct sgm41600_device *sgm, int *result)
{
	int ret;
	u8 vbus_adc_lsb, vbus_adc_msb;
	/* Spruce code for OSPURCET-1748 by zhongxc7 at 2023.03.16 start */
	#if 0
	ret = sgm41600_field_write(sgm, F_BIT_ADC_EN,1);
	if (ret)
		return ret;
	#endif
	/* Spruce code for OSPURCET-1748 by zhongxc7 at 2023.03.08 end */

	vbus_adc_msb = sgm41600_field_read(sgm, F_BIT_H_VBUS_ADC);
	if (vbus_adc_msb < 0)
		return vbus_adc_msb;
	
	ret = sgm41600_read_reg(sgm,sgm41600_regs[F_BIT_L_VBUS_ADC].reg, &vbus_adc_lsb);
	if (ret < 0)
		return ret;	

	*result = ((vbus_adc_msb << 8) | vbus_adc_lsb)*4;

	return ret;
}

static int sgm41600_get_ibus_adc(struct sgm41600_device *sgm, int *result)
{
	int ret;
	u8 ibus_adc_lsb, ibus_adc_msb;
	/* Spruce code for OSPURCET-1748 by zhongxc7 at 2023.03.16 start */
	#if 0
	ret = sgm41600_field_write(sgm, F_BIT_ADC_EN,1);
	if (ret)
		return ret;
	#endif
	/* Spruce code for OSPURCET-1748 by zhongxc7 at 2023.03.08 end */
	ibus_adc_msb = sgm41600_field_read(sgm, F_BIT_H_IBUS_ADC);
	if (ibus_adc_msb < 0)
		return ibus_adc_msb;
	
	ret = sgm41600_read_reg(sgm,sgm41600_regs[F_BIT_L_IBUS_ADC].reg, &ibus_adc_lsb);
	if (ret < 0)
		return ret;

	*result = ((ibus_adc_msb << 8) | ibus_adc_lsb)*2;

	return ret;
}

static int sgm41600_get_vbat_adc(struct sgm41600_device *sgm, int *result)
{
	int ret;
	u8 vbat_adc_lsb, vbat_adc_msb;
	/* Spruce code for OSPURCET-1748 by zhongxc7 at 2023.03.16 start */
	#if 0
	ret = sgm41600_field_write(sgm, F_BIT_ADC_EN,1);
	if (ret)
		return ret;
	#endif
	/* Spruce code for OSPURCET-1748 by zhongxc7 at 2023.03.08 end */
	vbat_adc_msb = sgm41600_field_read(sgm, F_BIT_H_VBAT_ADC);
	if (vbat_adc_msb < 0)
		return vbat_adc_msb;
	
	ret = sgm41600_read_reg(sgm,sgm41600_regs[F_BIT_L_VBAT_ADC].reg, &vbat_adc_lsb);
	if (ret < 0)
		return ret;
	

	*result = ((vbat_adc_msb << 8) | vbat_adc_lsb)*2;

	return ret;
}

static int sgm41600_get_ibat_adc(struct sgm41600_device *sgm, int *result)
{
	int ret;
	u8 ibat_adc_lsb, ibat_adc_msb;
	/* Spruce code for OSPURCET-1748 by zhongxc7 at 2023.03.16 start */
	#if 0
	ret = sgm41600_field_write(sgm, F_BIT_ADC_EN,1);
	if (ret)
		return ret;
	#endif
	/* Spruce code for OSPURCET-1748 by zhongxc7 at 2023.03.08 end */
	ibat_adc_msb = sgm41600_field_read(sgm, F_BIT_H_IBAT_ADC);
	if (ibat_adc_msb < 0)
		return ibat_adc_msb;
	
	ret = sgm41600_read_reg(sgm,sgm41600_regs[F_BIT_L_IBAT_ADC].reg, &ibat_adc_lsb);
	if (ret < 0)
		return ret;

	*result = ((ibat_adc_msb << 8) | ibat_adc_lsb)* 2;

	return ret;
}

static int sgm41600_get_tdie_adc(struct sgm41600_device *sgm, int *result)
{
	int ret;	
	u8 tdie_adc;
	/* Spruce code for OSPURCET-1748 by zhongxc7 at 2023.03.16 start */
	#if 0
	ret = sgm41600_field_write(sgm, F_BIT_ADC_EN,1);
	if (ret)
		return ret;
	#endif
	/* Spruce code for OSPURCET-1748 by zhongxc7 at 2023.03.08 end */
	ret = sgm41600_read_reg(sgm,sgm41600_regs[F_BIT_TDIE_ADC].reg, &tdie_adc);
	if (ret < 0)
		return ret;
	
	*result = tdie_adc - 40;
	return ret;
}

static int sgm41600_set_bus_ovp_enable(struct sgm41600_device *sgm,bool en)
{	
	return sgm41600_field_write(sgm, F_BIT_BUS_OVP_EN,en); 
}

static int sgm41600_set_bus_ovp_th(struct sgm41600_device *sgm,u32 th)
{
	int ret;	
	u8 val;

	if (th > SGM41600_BUS_OVP_MAX_uV)
		th = SGM41600_BUS_OVP_MAX_uV;
	else if (th < SGM41600_BUS_OVP_MIN_uV)
		th = SGM41600_BUS_OVP_MIN_uV;
	
	val = (th - SGM41600_BUS_OVP_MIN_uV)/SGM41600_BUS_OVP_STEP_uV;
	ret = sgm41600_field_write(sgm, F_BIT_BUS_OVP,val);
	
	if (ret){
		pr_err("%s fail\n",__func__);
		return ret;
	}
	
	return 0;
}

static int sgm41600_set_ibus_ocp_enable(struct sgm41600_device *sgm,bool en)
{	
	return sgm41600_field_write(sgm, F_BIT_IBUS_OCP_EN,en); 
}

static int sgm41600_set_ibus_ocp_th(struct sgm41600_device *sgm,u32 th)
{
	int ret;	
	u8 val;
	int mode;
	
	mode = sgm41600_field_read(sgm, F_BIT_CHG_MODE);
	if (mode < 0)
		return mode;
	if (1 == mode){
		if (th > SGM41600_BAT_OVP_MAX_uV + SGM41600_BYPASS_OCP_OFFSET_uA ||
			th < SGM41600_BAT_OVP_MAX_uV)
			return -EINVAL;
		
		th -= SGM41600_BYPASS_OCP_OFFSET_uA;	
		
	}
	
	val = (th - SGM41600_IBUS_OCP_MIN_uA)/SGM41600_IBUS_OCP_STEP_uA;	
	ret = sgm41600_field_write(sgm, F_BIT_IBUS_OCP,val);
	
	if (ret){
		pr_err("%s fail\n",__func__);
		return ret;
	}
	
	return 0;
}

static int sgm41600_set_bat_ovp_enable(struct sgm41600_device *sgm,bool en)
{	
	return sgm41600_field_write(sgm, F_BIT_BAT_OVP_EN,en); 
}

static int sgm41600_set_bat_ovp_th(struct sgm41600_device *sgm,u32 th)
{
	int ret;	
	u8 val;
	
	if (th > SGM41600_BAT_OVP_MAX_uV)
		th = SGM41600_BAT_OVP_MAX_uV;
	else if (th < SGM41600_BAT_OVP_MIN_uV)
		th = SGM41600_BAT_OVP_MIN_uV;
	
	val = (th - SGM41600_BAT_OVP_MIN_uV)/SGM41600_BAT_OVP_STEP_uV;
	ret = sgm41600_field_write(sgm, F_BIT_BAT_OVP,val);
	
	if (ret){
		pr_err("%s fail\n",__func__);
		return ret;
	}
	
	return 0;
}

static int sgm41600_set_ibat_ocp_enable(struct sgm41600_device *sgm,bool en)
{	
	return sgm41600_field_write(sgm, F_BIT_IBAT_OCP_EN,en); 
}

static int sgm41600_set_ibat_ocp_th(struct sgm41600_device *sgm,u32 th)
{
	int ret;	
	u8 val;
	if (th > SGM41600_IBAT_OCP_MAX_uA)
		th = SGM41600_IBAT_OCP_MAX_uA;
	else if (th < SGM41600_IBAT_OCP_MIN_uA)
		th = SGM41600_IBAT_OCP_MIN_uA;
	
	val = (th - SGM41600_IBAT_OCP_MIN_uA)/SGM41600_IBAT_OCP_STEP_uA;
	ret = sgm41600_field_write(sgm, F_BIT_IBAT_OCP,val);
	
	if (ret){
		pr_err("%s fail\n",__func__);
		return ret;
	}
	
	return 0;
}

static int sgm41600_set_ac_ovp_th(struct sgm41600_device *sgm,u32 th)
{
	int ret;	
	u8 val;
	if (th > SGM41600_AC_OVP_MAX_V)
		th = SGM41600_AC_OVP_MAX_V;
	if (th < SGM41600_AC_OVP_MIN_V)
		th = SGM41600_AC_OVP_MIN_V;
	
	val = (th - SGM41600_AC_OVP_MIN_V)/SGM41600_AC_OVP_STEP_V;
	ret = sgm41600_field_write(sgm, F_BIT_AC_OVP,val);
	
	if (ret){
		pr_err("%s fail\n",__func__);
		return ret;
	}
	
	return 0;
}


static int sgm41600_set_vdrop_ovp_enable(struct sgm41600_device *sgm,bool en)
{	
	return sgm41600_field_write(sgm, F_BIT_VDRP_OVP_EN,en); 
}

static int sgm41600_set_vdrop_ovp_th(struct sgm41600_device *sgm,u32 th)
{
	int ret;	
	u8 val;
	if (th > SGM41600_VDRP_OVP_MAX_uV)
		th = SGM41600_VDRP_OVP_MAX_uV;
	if (th < SGM41600_VDRP_OVP_MIN_uV)
		th = SGM41600_VDRP_OVP_MIN_uV;
	
	val = (th - SGM41600_VDRP_OVP_MIN_uV)/SGM41600_VDRP_OVP_STEP_uV;
	ret = sgm41600_field_write(sgm, F_BIT_VDRP_OVP,val);
	
	if (ret){
		pr_err("%s fail\n",__func__);
		return ret;
	}
	
	return 0;
}

static int sgm41600_set_ibus_ucp_enable(struct sgm41600_device *sgm,bool en)
{	
	return sgm41600_field_write(sgm, F_BIT_IBUS_UCP_EN,en); 
}

static int sgm41600_set_ibus_ucp_th(struct sgm41600_device *sgm,bool th)
{
	return sgm41600_field_write(sgm, F_BIT_IBUS_UCP,th); 
}


static int sgm41600_set_wdt_dis(struct sgm41600_device *sgm,bool en)
{	
	return sgm41600_field_write(sgm, F_BIT_WDT_DIS,en); 
}

#if 0
static int sgm41600_set_watchdog_timer(struct sgm41600_device *sgm,u32 ms)
{		
	u8 idx;	
	
	for (idx = 1; idx < ARRAY_SIZE(s_sgm41600_wdt_mS) && s_sgm41600_wdt_mS[idx] <= ms; idx++)
			;
	idx--; 	
	
	return sgm41600_field_write(sgm, F_BIT_WDT_TIMER,idx); 
}
#endif

static int sgm41600_set_vbat_regulation_enable(struct sgm41600_device *sgm,bool en)
{
	return sgm41600_field_write(sgm, F_BIT_VBAT_REG_EN,en);
}

static int sgm41600_set_vbat_regulation(struct sgm41600_device *sgm,int mV)
{
	u8 idx;

	for (idx = 1; idx < ARRAY_SIZE(s_sgm41600_vbatreg_mV) && s_sgm41600_vbatreg_mV[idx] <= mV; idx++)
			;
	idx--;

	return sgm41600_field_write(sgm, F_BIT_VBAT_REG,idx);
}

static int sgm41600_set_ibat_regulation_enable(struct sgm41600_device *sgm,bool en)
{
	return sgm41600_field_write(sgm, F_BIT_IBAT_REG_EN,en);
}

static int sgm41600_set_ibat_regulation(struct sgm41600_device *sgm,int mA)
{
	u8 idx;

	for (idx = 1; idx < ARRAY_SIZE(s_sgm41600_ibatreg_mA) && s_sgm41600_ibatreg_mA[idx] <= mA; idx++)
			;
	idx--;

	return sgm41600_field_write(sgm, F_BIT_IBAT_REG,idx);
}

static int sgm41600_init_protection(struct sgm41600_device *sgm)
{
	int ret;

	ret = sgm41600_set_bat_ovp_enable(sgm, sgm->cfg->bat_ovp_disable);
	pr_err("%s bat ovp %s\n",sgm->cfg->bat_ovp_disable ? "disable ":"enable",
			!ret ? "successfully" : "failed");

	ret = sgm41600_set_ibat_ocp_enable(sgm, sgm->cfg->bat_ocp_disable);
	pr_err("%s bat ocp %s\n",sgm->cfg->bat_ocp_disable ? "disable ":"enable",
			!ret ? "successfully" : "failed");

	ret = sgm41600_set_vdrop_ovp_enable(sgm, sgm->cfg->vdrop_ovp_disable);
	pr_err("%s vdrop ovp %s\n",sgm->cfg->vdrop_ovp_disable ? "disable ":"enable",
			!ret ? "successfully" : "failed");

	ret = sgm41600_set_bus_ovp_enable(sgm, sgm->cfg->bus_ovp_disable);
	pr_err("%s bus ovp %s\n",sgm->cfg->bus_ovp_disable ? "disable ":"enable",
			!ret ? "successfully" : "failed");

	ret = sgm41600_set_ibus_ucp_enable(sgm, sgm->cfg->bus_ucp_disable);
	pr_err("%s bus ucp %s\n",sgm->cfg->bus_ucp_disable ? "disable ":"enable",
			!ret ? "successfully" : "failed");

	ret = sgm41600_set_ibus_ocp_enable(sgm, sgm->cfg->bus_ocp_disable);
	pr_err("%s bus ocp %s\n",sgm->cfg->bus_ocp_disable ? "disable ":"enable",
			!ret ? "successfully" : "failed");

	ret = sgm41600_set_bat_ovp_th(sgm, sgm->cfg->bat_ovp_th);
	pr_err("set bat ovp th %d %s\n",sgm->cfg->bat_ovp_th,!ret ? "successfully" : "failed");

	ret = sgm41600_set_ibat_ocp_th(sgm, sgm->cfg->bat_ocp_th);
	pr_err("set bat ocp th %d %s\n",sgm->cfg->bat_ocp_th,!ret ? "successfully" : "failed");

	ret = sgm41600_set_ac_ovp_th(sgm, sgm->cfg->ac_ovp_th);
	pr_err("set ac ovp th %d %s\n",sgm->cfg->ac_ovp_th,!ret ? "successfully" : "failed");

	ret = sgm41600_set_bus_ovp_th(sgm, sgm->cfg->bus_ovp_th);
	pr_err("set bus ovp th %d %s\n",sgm->cfg->bus_ovp_th,!ret ? "successfully" : "failed");

	ret = sgm41600_set_ibus_ocp_th(sgm, sgm->cfg->bus_ocp_th);
	pr_err("set bus ocp th %d %s\n",sgm->cfg->bus_ocp_th,!ret ? "successfully" : "failed");

	return 0;

}

static int sgm41600_init_adc(struct sgm41600_device *sgm)
{
	sgm41600_field_write(sgm, F_BIT_ADC_RATE,0);
	sgm41600_field_write(sgm, F_BIT_VBUS_ADC_DIS,0);
	sgm41600_field_write(sgm, F_BIT_IBUS_ADC_DIS,0);
	sgm41600_field_write(sgm, F_BIT_VBAT_ADC_DIS,0);
	sgm41600_field_write(sgm, F_BIT_IBAT_ADC_DIS,0);
	sgm41600_field_write(sgm, F_BIT_TDIE_ADC_DIS,0);
	
	sgm41600_field_write(sgm, F_BIT_ADC_EN,1);
	
	return 0;
}

static int sgm41600_init_regulation(struct sgm41600_device *sgm)
{
	sgm41600_set_ibat_regulation(sgm,300);
	sgm41600_set_vbat_regulation(sgm,50);
	
	sgm41600_field_write(sgm, F_BIT_REG_TIMEOUT_DIS,1);
	
	sgm41600_field_write(sgm, F_BIT_VDRP_OVP_DEG,1);  //0 :10US 1:5MS
	sgm41600_set_vdrop_ovp_th(sgm,400);
	
	sgm41600_set_ibat_regulation_enable(sgm, 0);
	sgm41600_set_vbat_regulation_enable(sgm, 0);
	
	return 0;
	
}

static void sgm41600_check_fault_status(struct sgm41600_device *sgm)
{
	u8 flag_1,flag_2,flag_3;
	int ret;
	
	ret = sgm41600_read_reg(sgm,sgm41600_regs[F_BIT_AC_OVP_FLAG].reg, &flag_1);
	if (!ret && flag_1){
		if (flag_1 & sgm41600_regs[F_BIT_AC_OVP_FLAG].mask)
			pr_err("irq F_BIT_AC_OVP_FLAG\n");
		if (flag_1 & sgm41600_regs[F_BIT_AC_PDN_FLAG].mask)
			pr_err("irq F_BIT_AC_PDN_FLAG\n");
		if (flag_1 & sgm41600_regs[F_BIT_BUS_PDN_FLAG].mask)
			pr_err("irq F_BIT_BUS_PDN_FLAG\n");
		if (flag_1 & sgm41600_regs[F_BIT_VDRP_OVP_FLAG].mask)
			pr_err("irq F_BIT_VDRP_OVP_FLAG\n");
		if (flag_1 & sgm41600_regs[F_BIT_BUS_OVP_FLAG].mask)
			pr_err("irq F_BIT_BUS_OVP_FLAG\n");
		if (flag_1 & sgm41600_regs[F_BIT_IBUS_OCP_FLAG].mask)
			pr_err("irq F_BIT_IBUS_OCP_FLAG\n");
		if (flag_1 & sgm41600_regs[F_BIT_IBUS_UCP_RISE_FLAG].mask)
			pr_err("irq F_BIT_IBUS_UCP_RISE_FLAG\n");
		if (flag_1 & sgm41600_regs[F_BIT_IBUS_UCP_FALL_FLAG].mask)
			pr_err("irq F_BIT_IBUS_UCP_FALL_FLAG\n");
		
	}	
	
	ret = sgm41600_read_reg(sgm,sgm41600_regs[F_BIT_BAT_OVP_FLAG].reg, &flag_2);
	if (!ret && flag_2){
		if (flag_2 & sgm41600_regs[F_BIT_BAT_OVP_FLAG].mask)
			pr_err("irq F_BIT_BAT_OVP_FLAG\n");
		if (flag_2 & sgm41600_regs[F_BIT_IBAT_OCP_FLAG].mask)
			pr_err("irq F_BIT_IBAT_OCP_FLAG\n");
		if (flag_2 & sgm41600_regs[F_BIT_VBAT_REG_FLAG].mask)
			pr_err("irq F_BIT_VBAT_REG_FLAG\n");
		if (flag_2 & sgm41600_regs[F_BIT_IBAT_REG_FLAG].mask)
			pr_err("irq F_BIT_IBAT_REG_FLAG\n");
		if (flag_2 & sgm41600_regs[F_BIT_TDIE_OTP_FLAG].mask)
			pr_err("irq F_BIT_TDIE_OTP_FLAG\n");
		if (flag_2 & sgm41600_regs[F_BIT_VBUS_LO_FLAG].mask)
			pr_err("irq F_BIT_VBUS_LO_FLAG\n");
		if (flag_2 & sgm41600_regs[F_BIT_VBUS_HI_FLAG].mask)
			pr_err("irq F_BIT_VBUS_HI_FLAG\n");
		if (flag_2 & sgm41600_regs[F_BIT_CONV_OCP_FLAG].mask)
			pr_err("irq F_BIT_CONV_OCP_FLAG\n");
		
	}
	
	ret = sgm41600_read_reg(sgm,sgm41600_regs[F_BIT_BUS_INSERT_FLAG].reg, &flag_3);
	if (!ret && flag_2){
		if (flag_2 & sgm41600_regs[F_BIT_BUS_INSERT_FLAG].mask)
			pr_err("irq F_BIT_BUS_INSERT_FLAG\n");
		if (flag_2 & sgm41600_regs[F_BIT_BAT_INSERT_FLAG].mask)
			pr_err("irq F_BIT_BAT_INSERT_FLAG\n");
		if (flag_2 & sgm41600_regs[F_BIT_WD_TIMEOUT_FLAG].mask)
			pr_err("irq F_BIT_WD_TIMEOUT_FLAG\n");
		if (flag_2 & sgm41600_regs[F_BIT_AC_ABSENT_FLAG].mask)
			pr_err("irq F_BIT_AC_ABSENT_FLAG\n");
		if (flag_2 & sgm41600_regs[F_BIT_BUS_ABSENT_FLAG].mask)
			pr_err("irq F_BIT_BUS_ABSENT_FLAG\n");
		if (flag_2 & sgm41600_regs[F_BIT_IBUS_UCP_TIMEOUT_FLAG].mask)
			pr_err("irq F_BIT_IBUS_UCP_TIMEOUT_FLAG\n");
		if (flag_2 & sgm41600_regs[F_BIT_ADC_DONE_FLAG].mask)
			pr_err("irq F_BIT_ADC_DONE_FLAG\n");
		if (flag_2 & sgm41600_regs[F_BIT_PIN_DIAG_FLAG].mask)
			pr_err("irq F_BIT_PIN_DIAG_FLAG\n");
		
	}
		
	pr_err("%s flag_1=%d,flag_2=%d,flag_3=%d\n",__func__,flag_1,flag_2,flag_3);
	
}

static int sgm41600_get_state(struct sgm41600_device *sgm,
			     struct sgm41600_state *state)
{
	// state->vbus_adc = sgm41600_get_vbus_adc(sgm);
	// state->ibus_adc = sgm41600_get_ibus_adc(sgm);
	// state->vbat_adc = sgm41600_get_vbat_adc(sgm);
	// state->ibat_adc = sgm41600_get_ibat_adc(sgm);	
	// state->tdie_adc = sgm41600_get_tdie_adc(sgm);
	return 0;
} 
static int sgm41600_hw_init(struct sgm41600_device *sgm);

static int sgm41600_set_present(struct sgm41600_device *sgm, bool present)
{
	sgm->usb_present = present;

	if (present)
		sgm41600_hw_init(sgm);
	return 0;
}

#if 0
static int sgm41600_set_bypass_mode_en(struct sgm41600_device *sgm, int enable)
{
	sgm->bypass_mode_enable = !!enable;
	return 0;
}

static int sgm41600_get_bypass_mode_en(struct sgm41600_device *sgm)
{

	return sgm->bypass_mode_enable;
}

static int sgm41600_set_charge_mode(struct sgm41600_device *sgm,int mode)
{
	int ret;
	
	if (mode != SGM41600_CHARGE_MODE_BYPASS&& mode != SGM41600_CHARGE_MODE_DIV2)
		return -EINVAL;

	if (mode == SGM41600_CHARGE_MODE_BYPASS
			&& sgm->bypass_mode_enable != 1)
		return -EPERM;
	
	ret = sgm41600_field_write(sgm, F_BIT_CHG_MODE,mode);

	if (mode == SGM41600_CHARGE_MODE_DIV2) {
		ret = sgm41600_set_ac_ovp_th(sgm, sgm->cfg->ac_ovp_th);
		ret =sgm41600_set_bus_ovp_th(sgm, sgm->cfg->bus_ovp_th);
	}
	return ret; 
}
#endif

static int sgm41600_check_vbus_error_status(struct sgm41600_device *sgm, int *result)
{
    u8 ret;
    u8 reg;
    ret = sgm41600_read_reg(sgm, SGM41600_FAULT_REG, &reg);
    if(!ret) {
	    return ret;
    }

   *result = (int)reg;
    return ret;
}

static int sgm41600_get_charge_mode(struct sgm41600_device *sgm)
{
	return sgm41600_field_read(sgm, F_BIT_CHG_MODE);
}

static int sgm41600_property_is_writeable(struct power_supply *psy,
					 enum power_supply_property prop)
{
	switch (prop) {	
	case POWER_SUPPLY_PROP_ONLINE:
	case POWER_SUPPLY_PROP_PRESENT:
		return true;
	default:
		return false;
	}
}

static int sgm41600_charger_set_property(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *val)
{
	struct sgm41600_device *sgm = power_supply_get_drvdata(psy);
	

	switch (prop) {
	case POWER_SUPPLY_PROP_PRESENT:	
		sgm41600_set_present(sgm, !!val->intval);
		break;	
	case POWER_SUPPLY_PROP_ONLINE:
		if (val->intval)
			sgm41600_field_write(sgm, F_BIT_CHG_MODE, 2);
		else
			sgm41600_field_write(sgm, F_BIT_CHG_MODE, 0);
		break;
	/* Spruce code for OSPURCET-1748 by zhongxc7 at 2023.03.16 start */
	case POWER_SUPPLY_PROP_ADC_DIS:
		if (val->intval){
			sgm41600_field_write(sgm, F_BIT_ADC_EN,0);
			pr_info("%s plug out sgm disable adc\n",__func__);
		}else{
			sgm41600_field_write(sgm, F_BIT_ADC_EN,1);
			pr_info("%s plug in sgm enable adc\n",__func__);
		}
		break;
	/* Spruce code for OSPURCET-1748 by zhongxc7 at 2023.03.16 end */
	default:
		return -EINVAL;
	}

	return 0;
}

static int sgm41600_charger_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct sgm41600_device *sgm = power_supply_get_drvdata(psy);
	int ret = 0;
	int result;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:		
		val->intval = !!sgm41600_get_charge_mode(sgm);
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = SGM41600_MANUFACTURER;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = SGM41600_NAME;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = sgm->usb_present;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = sgm41600_get_ibus_adc(sgm, &result);
		if (!ret)
			sgm->ibus_curr = result;
		val->intval = sgm->ibus_curr;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = sgm41600_get_vbus_adc(sgm, &result);
		if (!ret)
			sgm->vbus_volt = result;
		val->intval = sgm->vbus_volt;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		ret = sgm41600_get_vbat_adc(sgm, &result);
		if (!ret)
			sgm->vbat_volt = result;
		val->intval = sgm->vbat_volt;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = sgm41600_get_ibat_adc(sgm, &result);
		if (!ret)
			sgm->ibat_curr = result;
		val->intval = sgm->ibat_curr;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		ret = sgm41600_get_tdie_adc(sgm, &result);
		if (!ret)
			sgm->die_temp = result;
		val->intval = sgm->die_temp;
		break;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		ret = sgm41600_check_vbus_error_status(sgm, &result);
		if (!ret)
			sgm->vbus_error = result;
		val->intval = sgm->vbus_error;
		break;
	default:
		return -EINVAL;
	}

	return ret;
}


static void sgm41600_dump_register(struct sgm41600_device * sgm)
{
	int i = 0;
	u8 reg = 0;
	int ret = 0;
	char buffer[1024] = {0};
	char *ptr = buffer;

	for(i=0; i <= 0x1A; i++) {
		ret = sgm41600_read_reg(sgm, i, &reg);
		if (ret == 0)
			ptr += sprintf(ptr, "[0x%0x=0x%0x]",
			       i, reg);
	}

	pr_err("[%s]%s\n",__func__, buffer);

}

static void charger_monitor_work_func(struct work_struct *work)
{
	int ret = 0;
	struct sgm41600_device * sgm = NULL;
	struct delayed_work *charge_monitor_work = NULL;
	//static u8 last_chg_method = 0;
	struct sgm41600_state state;

	pr_info("%s start\n",__func__);

	charge_monitor_work = container_of(work, struct delayed_work, work);
	if(charge_monitor_work == NULL) {
		pr_err("Cann't get charge_monitor_work\n");
		goto OUT;
	}
	sgm = container_of(charge_monitor_work, struct sgm41600_device, charge_monitor_work);
	if(sgm == NULL) {
		pr_err("Cann't get sgm \n");
		goto OUT;
	}

	ret = sgm41600_get_state(sgm, &state);
	mutex_lock(&sgm->lock);
	sgm->state = state;
	mutex_unlock(&sgm->lock);	

	sgm41600_dump_register(sgm);
	pr_info("%s end\n",__func__);
OUT:	
	schedule_delayed_work(&sgm->charge_monitor_work, 10*HZ);
}

static irqreturn_t sgm41600_irq_handler_thread(int irq, void *private)
{
	struct sgm41600_device *sgm = private;	
	struct sgm41600_state state;
	//lock wakelock
	pr_err("%s entry\n",__func__);
	
	mutex_lock(&sgm->irq_complete);
	sgm->irq_waiting = true;
	
	if (!sgm->resume_completed){
		if (!sgm->irq_disabled){
			disable_irq_nosync(irq);
			sgm->irq_disabled =true;
		}
		mutex_unlock(&sgm->irq_complete);
		return IRQ_HANDLED;
	}
	sgm->irq_waiting = false;	

	sgm41600_check_fault_status(sgm);
	sgm41600_get_state(sgm,&state);
	sgm41600_dump_register(sgm);
	mutex_unlock(&sgm->irq_complete);
	power_supply_changed(sgm->charger); 	

	return IRQ_HANDLED;
}

static enum power_supply_property sgm41600_power_supply_props[] = {
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE, //charge enable
	POWER_SUPPLY_PROP_VOLTAGE_NOW, // input voltage 
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
};

static char *sgm41600_charger_supplied_to[] = {
	"battery",	
};

static struct power_supply_desc sgm41600_power_supply_desc = {
	.name = "sgm41600-charger",
	.type = POWER_SUPPLY_TYPE_MAINS,
	//.usb_types = sgm41600_usb_type,
	//.num_usb_types = ARRAY_SIZE(sgm41600_usb_type),
	.properties = sgm41600_power_supply_props,
	.num_properties = ARRAY_SIZE(sgm41600_power_supply_props),
	.get_property = sgm41600_charger_get_property,
	.set_property = sgm41600_charger_set_property,
	.property_is_writeable = sgm41600_property_is_writeable,
};

static int sgm41600_power_supply_init(struct sgm41600_device *sgm,
							struct device *dev)
{
	struct power_supply_config psy_cfg = { .drv_data = sgm,
						.of_node = dev->of_node, };

	psy_cfg.supplied_to = sgm41600_charger_supplied_to;
	psy_cfg.num_supplicants = ARRAY_SIZE(sgm41600_charger_supplied_to);
	
	if (sgm->mode == SGM41600_ROLE_MASTER)
		sgm41600_power_supply_desc.name = "sgm41600-master";
	else if (sgm->mode == SGM41600_ROLE_SLAVE)
		sgm41600_power_supply_desc.name = "sgm41600-slave";
	else
		sgm41600_power_supply_desc.name = "sgm41600-standalone";
	
	sgm->charger = devm_power_supply_register(sgm->dev,
						 &sgm41600_power_supply_desc,
						 &psy_cfg);
	if (IS_ERR(sgm->charger))
		return -EINVAL;
	
	return 0;
}

static int sgm41600_hw_init(struct sgm41600_device *sgm)
{
	
	sgm41600_field_write(sgm, F_BIT_REG_RST,1);

	sgm41600_field_write(sgm, F_BIT_VBUS_HI,3);

	sgm41600_set_ibus_ucp_th(sgm,300);
	
	sgm41600_init_protection(sgm);	
	
	sgm41600_init_adc(sgm);
	
	sgm41600_set_wdt_dis(sgm,1);
	
	sgm41600_field_write(sgm, F_BIT_BUS_PDN_EN,0);
	
	sgm41600_init_regulation(sgm);
	return 0;

}

static int sgm41600_parse_dt(struct sgm41600_device *sgm)
{
	int ret;
	
	int irq_gpio = 0, irqn = 0;
	struct device_node *np = sgm->dev->of_node;
	
	sgm->cfg = devm_kzalloc(sgm->dev,sizeof(struct sgm41600_cfg),GFP_KERNEL);
	
	if (!sgm->cfg)
		return -ENOMEM;
	
	sgm->cfg->bat_ovp_disable = of_property_read_bool(np,"sgm,sgm41600,bat-ovp-disable");
	sgm->cfg->bat_ocp_disable = of_property_read_bool(np,"sgm,sgm41600,bat-ocp-disable");
	sgm->cfg->vdrop_ovp_disable = of_property_read_bool(np,"sgm,sgm41600,vdrop-ovp-disable");
	sgm->cfg->bus_ovp_disable = of_property_read_bool(np,"sgm,sgm41600,bus-ovp-disable");
	sgm->cfg->bus_ucp_disable = of_property_read_bool(np,"sgm,sgm41600,bus-ucp-disable");
	sgm->cfg->bus_ocp_disable = of_property_read_bool(np,"sgm,sgm41600,bus-ocp-disable");

	ret = of_property_read_u32(np,"sgm,mode", &sgm->mode);
	if(ret)
	{
		dev_err(sgm->dev, "failed to read sgm,mode\n");
		return ret;		
	}
	
	ret = of_property_read_u32(np,"sgm,sgm41600,bat-ovp-threshold",&sgm->cfg->bat_ovp_th);
	if (ret)
	{
		dev_err(sgm->dev, "failed to read bat-ovp-threshold\n");
		return ret;
	}
	
	ret = of_property_read_u32(np,"sgm,sgm41600,bat-ocp-threshold",&sgm->cfg->bat_ocp_th);
	if (ret)
	{
		dev_err(sgm->dev, "failed to read bat-ocp-threshold\n");
		return ret;
	}
	
	ret = of_property_read_u32(np,"sgm,sgm41600,ac-ovp-threshold",&sgm->cfg->ac_ovp_th);
	if (ret)
	{
		dev_err(sgm->dev, "failed to read ac-ovp-threshold\n");
		return ret;
	}
	
	ret = of_property_read_u32(np,"sgm,sgm41600,bus-ovp-threshold",&sgm->cfg->bus_ovp_th);
	if (ret)
	{
		dev_err(sgm->dev, "failed to read bus-ovp-threshold\n");
		return ret;
	}
	
	ret = of_property_read_u32(np,"sgm,sgm41600,bus-ocp-threshold",&sgm->cfg->bus_ocp_th);
	if (ret)
	{
		dev_err(sgm->dev, "failed to read bus-ocp-threshold\n");
		return ret;
	}

	irq_gpio = of_get_named_gpio(np, "sgm,irq-gpio", 0);
	if (!gpio_is_valid(irq_gpio))
	{
		dev_err(sgm->dev, "%s: %d gpio get failed\n", __func__, irq_gpio);
		return -EINVAL;
	}
	ret = gpio_request(irq_gpio, "sgm41600 irq pin");
	if (ret) {
		dev_err(sgm->dev, "%s: %d gpio request failed\n", __func__, irq_gpio);
		return ret;
	}
	gpio_direction_input(irq_gpio);
	irqn = gpio_to_irq(irq_gpio);
	if (irqn < 0) {
		dev_err(sgm->dev, "%s:%d gpio_to_irq failed\n", __func__, irqn);
		return irqn;
	}
	sgm->client->irq = irqn;
	return 0;
}


int sgm41600_hw_chipid_detect(struct i2c_client *client)
{
	int ret = 0;

    ret = i2c_smbus_read_byte_data(client, 0x03);
	if(ret < 0) {
		pr_err("i2c read fail:%s\n", __func__);
		return ret;
	} else if (SGM41600_DEV_ID == (ret & sgm41600_regs[F_BIT_DEV_ID].mask)) {
        pr_info("SGM41600 Device part ID reg 0x%02X\n", ret);
        return 0;
    } else {
		pr_err("not is  SGM41600 Device part ID reg 0x%02X\n", ret);
		return -ENODATA;
	}

	return ret;
}



int sgm41600_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct sgm41600_device *sgm;
	int ret;
	char *name = NULL;

	pr_info("%s enter\n",__func__);
	sgm = devm_kzalloc(dev, sizeof(*sgm), GFP_KERNEL);
	if (!sgm)
		return -ENOMEM;

	sgm->client = client;
	sgm->dev = dev;

	mutex_init(&sgm->lock);
	mutex_init(&sgm->i2c_rw_lock);
	mutex_init(&sgm->irq_complete);
	
	sgm->resume_completed = true;
	sgm->irq_waiting = false;
	
	
	// strncpy(sgm->model_name, id->name, I2C_NAME_SIZE);	

	i2c_set_clientdata(client, sgm);

	sgm->bypass_mode_enable = 0;

	name = devm_kasprintf(sgm->dev, GFP_KERNEL, "%s",
		"sgm41600 suspend wakelock");
	sgm->charger_wakelock =	wakeup_source_register(NULL,name); //kernel 4.19
	
	// Customer customization
	ret = sgm41600_parse_dt(sgm);
	if (ret) {
		dev_err(dev, "Failed to read device tree properties%d\n", ret);
		goto error_out;
	}

	ret = sgm41600_hw_init(sgm);
	if (ret) {
		dev_err(dev, "Cannot initialize the chip.\n");
		goto error_out2;
	}

	ret = sgm41600_power_supply_init(sgm, dev);
	if (ret) {
		dev_err(dev, "Failed to register power supply\n");
		goto error_out2;
	}
	
	INIT_DELAYED_WORK(&sgm->charge_monitor_work, charger_monitor_work_func);
	if (client->irq) {
		ret = devm_request_threaded_irq(dev, client->irq, NULL,
						sgm41600_irq_handler_thread,
						IRQF_TRIGGER_FALLING |
						IRQF_ONESHOT,
						dev_name(&client->dev), sgm);
		if (ret)
			goto error_out;
		enable_irq_wake(client->irq);
	}
	/* Spruce code for OSPURCET-1748 by zhongxc7 at 2023.03.16 start */
	sgm41600_field_write(sgm, F_BIT_ADC_EN,0);
	/* Spruce code for OSPURCET-1748 by zhongxc7 at 2023.03.16 end */
	schedule_delayed_work(&sgm->charge_monitor_work, 100);
	pr_info("%s end \n",__func__);

	return 0;
error_out2:
	power_supply_unregister(sgm->charger);  	
error_out:
	mutex_destroy(&sgm->lock);
	mutex_destroy(&sgm->i2c_rw_lock);
	mutex_destroy(&sgm->irq_complete);
	return ret;
}

int sgm41600_charger_remove(struct i2c_client *client)
{
	struct sgm41600_device *sgm= i2c_get_clientdata(client);

	sgm41600_field_write(sgm, F_BIT_ADC_EN,0);

	cancel_delayed_work_sync(&sgm->charge_monitor_work);

	power_supply_unregister(sgm->charger);

	mutex_destroy(&sgm->lock);
	mutex_destroy(&sgm->i2c_rw_lock);
	mutex_destroy(&sgm->irq_complete);

	return 0;
}


void sgm41600_charger_shutdown(struct i2c_client *client)
{
	int ret = 0;

	struct sgm41600_device *sgm = i2c_get_clientdata(client);
	if(IS_ERR_OR_NULL(sgm))
		return;
	ret = sgm41600_field_write(sgm, F_BIT_CHG_MODE,0x0);//Off charge
	if (ret) {
		pr_err("Failed to disable charger, ret = %d\n", ret);
	}
/* Spruce code for OSPURCET-599 by zhangjb18 at 2022.12.06 start */
	ret = sgm41600_field_write(sgm, F_BIT_ADC_EN,0);
	if (ret) {
		pr_err("Failed to disable ADC, ret = %d\n", ret);
	}
/* Spruce code for OSPURCET-599 by zhangjb18 at 2022.12.06 end */
	pr_info("sgm41600_charger_shutdown\n");
}



static inline bool is_device_suspended(struct sgm41600_device *sgm)
{
	return !sgm->resume_completed;
}

int sgm41600_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sgm41600_device *sgm = i2c_get_clientdata(client);

	mutex_lock(&sgm->irq_complete);
	sgm->resume_completed = false;
	mutex_unlock(&sgm->irq_complete);
	sgm41600_field_write(sgm, F_BIT_ADC_EN,0);
	cancel_delayed_work_sync(&sgm->charge_monitor_work);
	pr_err("sgm416000 Suspend successfully!");

	return 0;
}


int sgm41600_suspend_noirq(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sgm41600_device *sgm = i2c_get_clientdata(client);
	
	if (sgm->irq_waiting){
		pr_err("Aborting suspend ,an interrupt was detected while suspending\n");
		return -EBUSY;
	}
	return 0;
}

int sgm41600_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sgm41600_device *sgm = i2c_get_clientdata(client);
	
	mutex_lock(&sgm->irq_complete);
	sgm->resume_completed = true;
	if (sgm->irq_waiting){
		sgm->irq_disabled = false;
		enable_irq(client->irq);
		mutex_unlock(&sgm->irq_complete);
		sgm41600_irq_handler_thread(client->irq,sgm);
	}
	else{
		mutex_unlock(&sgm->irq_complete);
	}
	power_supply_changed(sgm->charger);
	pr_err("Resume successfully!");
	return 0;
}


