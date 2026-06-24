/*
 * KTZ Semiconductor KTZ8866 LED Driver
 *
 * Copyright (C) 2013 Ideas on board SPRL
 *
 * Contact: Chenzilong  <chenzilong1@xiaomi.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/fb.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/platform_data/dualktz8866.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>

#define u8 unsigned char

enum {
        KTZ8866_A = 0,
        KTZ8866_B,
};

struct ktz8866 {
        u8 chip;
        struct i2c_client *client;
        struct backlight_device *backlight;
        struct ktz8866_platform_data *pdata;
};

static struct ktz8866 *bd;
static struct ktz8866_status ktz8866_status;
static struct pwm_to_51 pwm_map[6] = {
        {10, 0x199},
        {20, 0x333},
        {40, 0x666},
        {60, 0x999},
        {80, 0xccc},
        {100, 0xFFF},
};

static struct ktz8866 *bd_a;
static struct ktz8866 *bd_b;

static struct ktz8866_led g_ktz8866_led;

/* 辅助函数：同时写入 A 和 B 芯片 */
static void ktz8866_write_both(u8 reg, u8 data)
{
        if (bd_a)
                i2c_smbus_write_byte_data(bd_a->client, reg, data);
        if (bd_b)
                i2c_smbus_write_byte_data(bd_b->client, reg, data);
}

static int ktz8866_read(u8 reg, u8 *data)
{
        int ret;

        ret = i2c_smbus_read_byte_data(bd->client, reg);
        if (ret < 0) {
                dev_err(&bd->client->dev, "failed reading at 0x%02x\n", reg);
                return ret;
        }

        *data = (uint8_t)ret;

        return 0;
}

static int ktz8866_reads(struct ktz8866 *bd, u8 reg, u8 *data)
{
        int ret;

        ret = i2c_smbus_read_byte_data(bd->client, reg);
        if (ret < 0) {
                dev_err(&bd->client->dev, "failed reading at 0x%02x\n", reg);
                return ret;
        }

        *data = (uint8_t)ret;

        return 0;
}

static int ktz8866_backlight_update_status(struct backlight_device *backlight)
{
        struct ktz8866 *bd = bl_get_data(backlight);
        int exponential_bl = backlight->props.brightness;
        int brightness = 0;
        u8 v[2];

        brightness = bl_level_remap[exponential_bl];

        if (brightness < 0 || brightness > BL_LEVEL_MAX)
                return 0;

        mutex_lock(&g_ktz8866_led.lock);

        if (brightness > 0) {
                ktz8866_write_both(KTZ8866_DISP_BL_ENABLE, 0x4f);
                dev_dbg(&bd->client->dev, "backlight enable, brightness=%d\n", brightness);
        } else if (brightness == 0) {
                ktz8866_write_both(KTZ8866_DISP_BL_ENABLE, 0x0f);
                dev_dbg(&bd->client->dev, "backlight disable\n");
        }

        v[0] = brightness & 0x7;
        v[1] = (brightness >> 3) & 0xff;

        ktz8866_write_both(KTZ8866_DISP_BB_LSB, v[0]);
        ktz8866_write_both(KTZ8866_DISP_BB_MSB, v[1]);

        g_ktz8866_led.level = brightness;

        mutex_unlock(&g_ktz8866_led.lock);
        return 0;
}

static int ktz8866_backlight_get_brightness(struct backlight_device *backlight)
{
        int brightness = backlight->props.brightness;
        u8 v[2];
        
        mutex_lock(&g_ktz8866_led.lock);

        if (bd_a) {
                ktz8866_reads(bd_a, 0x5, &v[0]);
                ktz8866_reads(bd_a, 0x4, &v[1]);
                brightness = (v[1] << 8) + v[0];
        }

        mutex_unlock(&g_ktz8866_led.lock);
        return brightness;
}

static const struct backlight_ops ktz8866_backlight_ops = {
        .options = BL_CORE_SUSPENDRESUME,
        .update_status = ktz8866_backlight_update_status,
        .get_brightness = ktz8866_backlight_get_brightness,
};

static int parse_dt(struct device *dev, struct ktz8866_platform_data *pdata)
{
        struct device_node *np = dev->of_node;

        pdata->hw_en_gpio =
                of_get_named_gpio_flags(np, "ktz8866,hwen-gpio", 0);

        return 0;
}

static int ktz8866_probe(struct i2c_client *client,
                         const struct i2c_device_id *id)
{
        struct backlight_device *backlight;
        struct backlight_properties props;
        int ret = 0;
        u8 read;

        bd = devm_kzalloc(&client->dev, sizeof(*bd), GFP_KERNEL);
        if (!bd)
                return -ENOMEM;
        dev_info(&client->dev, "ktz8866 bd = devm_kzalloc\n");

        bd->pdata = devm_kzalloc(
                &client->dev, sizeof(struct ktz8866_platform_data), GFP_KERNEL);
        if (!bd->pdata)
                return -ENOMEM;
        dev_info(&client->dev, "bd->pdata = devm_kzalloc\n");

        bd->client = client;
        bd->chip = id->driver_data;

        if (!i2c_check_functionality(client->adapter,
                                     I2C_FUNC_SMBUS_BYTE_DATA)) {
                dev_err(&client->dev,
                        "ktz8866 I2C adapter doesn't support I2C_FUNC_SMBUS_BYTE\n");
                return -EIO;
        }
        dev_info(&client->dev,
                "ktz8866 i2c_check_functionality OK\n");

        mutex_init(&g_ktz8866_led.lock);

        memset(&props, 0, sizeof(props));
        
        if (bd->chip == KTZ8866_A) {
                bd->client->dev.init_name = "panel1-backlight";
        } else {
                bd->client->dev.init_name = "KTZ8866B";
        }
        
        props.type = BACKLIGHT_RAW;
        props.max_brightness = 2047;
        props.brightness = clamp_t(unsigned int, 98, 16, props.max_brightness);

        /* 只有 A 芯片注册 backlight 设备，B 芯片不注册 */
        if (bd->chip == KTZ8866_A) {
                dev_info(&client->dev, "registering backlight as panel1-backlight\n");
                backlight = devm_backlight_device_register(
                        &client->dev, "panel1-backlight", &bd->client->dev, bd,
                        &ktz8866_backlight_ops, &props);
                if (IS_ERR(backlight)) {
                        dev_err(&client->dev, "failed to register backlight\n");
                        return PTR_ERR(backlight);
                }
                i2c_set_clientdata(client, backlight);
                bd->backlight = backlight;
        } else {
                dev_info(&client->dev, "KTZ8866B registered as slave, no backlight device\n");
                i2c_set_clientdata(client, NULL);
        }

        parse_dt(&client->dev, bd->pdata);
        dev_info(&client->dev, "ktz8866 parse_dt\n");

        /* 只有 A 芯片需要处理 HW_EN GPIO */
        if (bd->chip == KTZ8866_A) {
                ret = devm_gpio_request_one(&client->dev, bd->pdata->hw_en_gpio,
                                            GPIOF_DIR_OUT | GPIOF_INIT_HIGH,
                                            "HW_EN");
                if (ret < 0) {
                        dev_err(&client->dev,
                                "unable to request HW_EN GPIO\n");
                        return ret;
                }
        }

        ktz8866_read(KTZ8866_DISP_FLAGS, &read);
        dev_info(&bd->client->dev, "reading reg 0x%02x = 0x%02x\n",
                KTZ8866_DISP_FLAGS, read);

        /* 保存全局指针 */
        if (bd->chip == KTZ8866_A) {
                ktz8866_status.ktz8866a_init = true;
                bd_a = bd;
        } else if (bd->chip == KTZ8866_B) {
                ktz8866_status.ktz8866b_init = true;
                bd_b = bd;
        }

        if (ktz8866_status.ktz8866a_init == true &&
            ktz8866_status.ktz8866b_init == true) {
                dev_info(&client->dev,
                        "ktz8866a and ktz8866b init success\n");
        }

        return 0;
}

static int ktz8866_remove(struct i2c_client *client)
{
        struct backlight_device *backlight = i2c_get_clientdata(client);

        if (backlight) {
                backlight->props.brightness = 0;
                backlight_update_status(backlight);
        }

        return 0;
}

static const struct i2c_device_id ktz8866_ids[] = {
        { "ktz8866a", 0 },
        { "ktz8866b", 1 },
};
MODULE_DEVICE_TABLE(i2c, ktz8866_ids);

static struct of_device_id ktz8866_match_table[] = {
        {
                .compatible = "ktz,ktz8866a",
        },
        {
                .compatible = "ktz,ktz8866b",
        },
        {},
};

static struct i2c_driver ktz8866_driver = {
        .driver = {
                .name = "dualktz8866",
                .owner = THIS_MODULE,
                .of_match_table = ktz8866_match_table,
        },
        .probe = ktz8866_probe,
        .remove = ktz8866_remove,
        .id_table = ktz8866_ids,
};

module_i2c_driver(ktz8866_driver);

MODULE_DESCRIPTION("KTZ8866 Backlight Driver");
MODULE_AUTHOR("Modified for dual-chip sync");
MODULE_LICENSE("GPL");