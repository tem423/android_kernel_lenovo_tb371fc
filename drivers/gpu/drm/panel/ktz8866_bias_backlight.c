/*
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/backlight.h>

#define BL_I2C_ADDRESS			  0x11

#define LCD_BL_PRINT printk

#define LCD_BL_BIAS_I2C_ID_NAME "lcd_bias_bl"

/*****************************************************************************
 * GLobal Variable
 *****************************************************************************/
static struct i2c_client *lcd_bl_bias_i2c_client;
static DEFINE_MUTEX(read_lock);
/*****************************************************************************
 * Function Prototype
 *****************************************************************************/
static int lcd_bl_bias_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int lcd_bl_bias_i2c_remove(struct i2c_client *client);

/*****************************************************************************
 * Extern Area
 *****************************************************************************/

static int lcd_bl_bias_write_byte(unsigned char addr, unsigned char value)
{
	int ret = 0;
	unsigned char write_data[2] = {0};

	write_data[0] = addr;
	write_data[1] = value;

	if (NULL == lcd_bl_bias_i2c_client) {
		LCD_BL_PRINT("[LCD][BL] lcd_bl_bias_i2c_client is null!!\n");
		return -EINVAL;
	}
	ret = i2c_master_send(lcd_bl_bias_i2c_client, write_data, 2);

	if (ret < 0)
		LCD_BL_PRINT("[LCD][BL] i2c write data fail !!\n");

	return ret;
}
#if 0
static int lcd_bl_read_byte(u8 regnum)
{
	u8 buffer[1], reg_value[1];
	int res = 0;

	if (NULL == lcd_bl_bias_i2c_client) {
		LCD_BL_PRINT("[LCD][BL] lcd_bl_bias_i2c_client is null!!\n");
		return -EINVAL;
	}

	mutex_lock(&read_lock);

	buffer[0] = regnum;
	res = i2c_master_send(lcd_bl_bias_i2c_client, buffer, 0x1);
	if (res <= 0)	{
	  mutex_unlock(&read_lock);
	  LCD_BL_PRINT("read reg send res = %d\n", res);
	  return res;
	}
	res = i2c_master_recv(lcd_bl_bias_i2c_client, reg_value, 0x1);
	if (res <= 0) {
	  mutex_unlock(&read_lock);
	  LCD_BL_PRINT("read reg recv res = %d\n", res);
	  return res;
	}
	mutex_unlock(&read_lock);

	return reg_value[0];
}
#endif

int lcd_bias_set_led_brightness(int value)//for set bringhtness
{
	//if(printk_ratelimit())
	LCD_BL_PRINT("%s:8866 bl = %d\n", __func__, value);

	if (value < 0) {
		LCD_BL_PRINT("%d %s --wlc invalid value=%d\n", __LINE__, __func__, value);
		return 0;
	}

	if (value > 0) {
		//lcd_bl_bias_write_byte(0x08, 0x5F); /* BL enabled and Current sink 1/2/3/4 /5 enabled；*/
	/*Spruce code for OSPURCET-1551 by zhuhao6 at 2023/3/15 start*/
		lcd_bl_bias_write_byte(0x04, 0x07);// lsb
		lcd_bl_bias_write_byte(0x05, value & 0xFF);// msb
	/*Spruce code for OSPURCET-1551 by zhuhao6 at 2023/3/15 end*/
	} else {
		lcd_bl_bias_write_byte(0x04, 0x00);// lsb
		lcd_bl_bias_write_byte(0x05, 0x00);// msb
		//lcd_bl_bias_write_byte(0x08, 0x00); /* BL disabled and Current sink 1/2/3/4 /5 enabled；*/
	}

	return 0;
}
EXPORT_SYMBOL(lcd_bias_set_led_brightness);

extern void lcd_set_backlight(int enable);
void lcd_set_bias(int enable)
{
	/*Spruce code for OSPURCET-612 by zhangkx10 at 2023/1/19 start*/
	LCD_BL_PRINT("[%s], value = %d", __func__, enable);
	if (enable) {
		lcd_bl_bias_write_byte(0x0C, 0x2E); /* LCD_BOOST_CFG */
		lcd_bl_bias_write_byte(0x0D, 0x24); /* OUTP_CFG，OUTP = 5.8V */
		lcd_bl_bias_write_byte(0x0E, 0x24); /* OUTN_CFG，OUTN = -5.8V */
		lcd_bl_bias_write_byte(0x09, 0x99);
		lcd_bl_bias_write_byte(0x04, 0x00);// lsb
		lcd_bl_bias_write_byte(0x05, 0x00);// msb
	/*Spruce code for OSPURCET-1551 by zhuhao6 at 2023/3/15 start*/
		lcd_bl_bias_write_byte(0x03, 0xFD);
	/*Spruce code for OSPURCET-1551 by zhuhao6 at 2023/3/15 end*/

	/*Spruce code for OSPURCET-612 by zhangkx10 at 2023/1/19 end*/

		//lcd_bl_bias_write_byte(0x09, 0x9C);/* enable OUTP */
		//mdelay(5);
		//lcd_bl_bias_write_byte(0x09, 0x9E);/* enable OUTN */

		lcd_bl_bias_write_byte(0x02, 0XDA); /* BL_CFG1；OVP=34V，线性调光，PWM enabled */
		lcd_bl_bias_write_byte(0x11, 0x37); /* BL_OPTION2；电感4.7uH，BL_CURRENT_LIMIT 2.5A；*/
		lcd_bl_bias_write_byte(0x15, 0xA0); /* Backlight Full-scale LED Current 21.2mA/CH；*/
		lcd_bl_bias_write_byte(0x08, 0x4F); /* BL enabled and Current sink 1/2/3/4 enabled；*/
		lcd_set_backlight(enable);
	}
}
EXPORT_SYMBOL(lcd_set_bias);

#ifdef CONFIG_OF
static const struct of_device_id i2c_of_match[] = {
    { .compatible = "ktz,ktz8866,bias,backlight", },
    {},
};
#endif

static const struct i2c_device_id lcd_bl_bias_i2c_id[] = {
    {LCD_BL_BIAS_I2C_ID_NAME, 0},
    {},
};

static struct i2c_driver lcd_bl_bias_i2c_driver = {
/************************************************************
Attention:
Althouh i2c_bus do not use .id_table to match, but it must be defined,
otherwise the probe function will not be executed!
************************************************************/
	.id_table = lcd_bl_bias_i2c_id,
	.probe = lcd_bl_bias_i2c_probe,
	.remove = lcd_bl_bias_i2c_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = LCD_BL_BIAS_I2C_ID_NAME,
#ifdef CONFIG_OF
		.of_match_table = i2c_of_match,
#endif
	},
};

static int lcd_bl_bias_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret;
	/*Spruce code for OSPURCET-263 by zhuhao at 2022/12/07 start*/
	if(strstr(saved_command_line,"backlightktz=4") || strstr(saved_command_line,"backlightktz=3"))
	{
			pr_err("ktz8866_bias match\n");
	}else{
			return 0;
	}
	/*Spruce code for OSPURCET-263 by zhuhao at 2022/12/07 end*/
	if (NULL == client) {
		LCD_BL_PRINT("[LCD][BL] i2c_client is NULL\n");
		return -EINVAL;
	}

	lcd_bl_bias_i2c_client = client;

	LCD_BL_PRINT("backlight, i2c address: %0x\n", client->addr);

	//write vsp/vsn reg
	ret = lcd_bl_bias_write_byte(0x0C, 0x2E); /* LCD_BOOST_CFG */
	/*Spruce code for OSPURCET-611 by zhuhao at 2022/12/12 start*/
	ret = lcd_bl_bias_write_byte(0x0D, 0x24); /* OUTP_CFG，OUTP = 5.8V */
	ret = lcd_bl_bias_write_byte(0x0E, 0x24); /* OUTN_CFG，OUTN = -5.8V */
	ret = lcd_bl_bias_write_byte(0x09, 0x99);
	/*Spruce code for OSPURCET-611 by zhuhao at 2022/12/12 end*/

	// ret = lcd_bl_bias_write_byte(0x09, 0x9C); /* enable OUTP */
	// mdelay(5); /* delay 5ms */
	// ret = lcd_bl_bias_write_byte(0x09, 0x9E); /* enable OUTN */

	//write backlight reg
	ret = lcd_bl_bias_write_byte(0x02, 0XDA); /* BL_CFG1；OVP=34V，线性调光，PWM enabled */
	ret = lcd_bl_bias_write_byte(0x11, 0x37); /* BL_OPTION2；电感4.7uH，BL_CURRENT_LIMIT 2.5A；*/
	ret = lcd_bl_bias_write_byte(0x15, 0xA0); /* Backlight Full-scale LED Current 21.2mA/CH；*/
	ret = lcd_bl_bias_write_byte(0x08, 0x4F); /* BL enabled and Current sink 1/2/3/4 enabled；*/
	/*Spruce code for OSPURCET-1551 by zhuhao6 at 2023/3/15 start*/
	ret = lcd_bl_bias_write_byte(0x03, 0xFD);
	/*Spruce code for OSPURCET-1551 by zhuhao6 at 2023/3/15 end*/

	if (ret < 0) {
		LCD_BL_PRINT("[%s]:I2C write reg is fail!", __func__);
		return -EINVAL;
	} else {
		LCD_BL_PRINT("[%s]:I2C write reg is success!", __func__);
	}
	return 0;
}

static int lcd_bl_bias_i2c_remove(struct i2c_client *client)
{
	lcd_bl_bias_i2c_client = NULL;
	i2c_unregister_device(client);
	return 0;
}

static int __init lcd_bl_bias_init(void)
{
	LCD_BL_PRINT("%s start!\n", __func__);

	if (i2c_add_driver(&lcd_bl_bias_i2c_driver)) {
		LCD_BL_PRINT("[LCD][BL] Failed to register lcd_bl_bias_i2c_driver!\n");
		return -EINVAL;
	}
	return 0;
}

static void __exit lcd_bl_bias_exit(void)
{
	i2c_del_driver(&lcd_bl_bias_i2c_driver);
}

module_init(lcd_bl_bias_init);
module_exit(lcd_bl_bias_exit);

MODULE_AUTHOR("wulongchao <wulongchao@huanqin.com>");
MODULE_DESCRIPTION("MTK LCD BL I2C Driver");
MODULE_LICENSE("GPL");




