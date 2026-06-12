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
static char gresult[30];
static int caseid = 0;

static struct ktz8866_led g_ktz8866_led;

+/* 辅助函数：同时写入 A 和 B 芯片 */
+static void ktz8866_write_both(u8 reg, u8 data)
+{
+       if (bd_a)
+               i2c_smbus_write_byte_data(bd_a->client, reg, data);
+       if (bd_b)
+               i2c_smbus_write_byte_data(bd_b->client, reg, data);
+}
+
 static int ktz8866_read(u8 reg, u8 *data)
 {
         int ret;
@@ -94,33 +107,24 @@ static int ktz8866_backlight_update_status(struct backlight_device *backlight)
 
         mutex_lock(&g_ktz8866_led.lock);
 
         if (brightness > 0) {
-               ktz8866_writes(bd, KTZ8866_DISP_BL_ENABLE, 0x4f);
-               dev_warn(&bd->client->dev,
-                        "ktz8866 backlight enable,dimming close");
+               ktz8866_write_both(KTZ8866_DISP_BL_ENABLE, 0x4f);
         } else if (brightness == 0) {
-               ktz8866_writes(bd, KTZ8866_DISP_BL_ENABLE, 0x0f);
-               dev_warn(&bd->client->dev,
-                        "ktz8866 backlight disable,dimming close");
+               ktz8866_write_both(KTZ8866_DISP_BL_ENABLE, 0x0f);
         }
 
         v[0] = brightness & 0x7;
         v[1] = (brightness >> 3) & 0xff;
 
-        ktz8866_writes(bd, KTZ8866_DISP_BB_LSB, v[0]);
-        ktz8866_writes(bd, KTZ8866_DISP_BB_MSB, v[1]);
+        ktz8866_write_both(KTZ8866_DISP_BB_LSB, v[0]);
+        ktz8866_write_both(KTZ8866_DISP_BB_MSB, v[1]);
 
         g_ktz8866_led.level = brightness;
 
         mutex_unlock(&g_ktz8866_led.lock);
         return 0;
 }
 
 static int ktz8866_backlight_get_brightness(struct backlight_device *backlight)
 {
@@ -178,30 +182,46 @@ static int ktz8866_probe(struct i2c_client *client,
 
         mutex_init(&g_ktz8866_led.lock);
 
         memset(&props, 0, sizeof(props));
         if (bd->chip == KTZ8866_A) {
-               bd->client->dev.init_name = "KTZ8866A";
+               /* A 芯片：注册为 panel0-backlight，让 DRM 能找到 */
+               bd->client->dev.init_name = "panel0-backlight";
         } else {
+               /* B 芯片：不注册背光设备，只保存指针用于同步 */
                bd->client->dev.init_name = "KTZ8866B";
         }
         props.type = BACKLIGHT_RAW;
         props.max_brightness = 2047;
         props.brightness = clamp_t(unsigned int, 98, 16, props.max_brightness);
-        dev_warn(&client->dev, "ktz8866 devm_backlight_device_register \n");
-        backlight = devm_backlight_device_register(
-               &client->dev, dev_name(&client->dev), &bd->client->dev, bd,
-               &ktz8866_backlight_ops, &props);
-        if (IS_ERR(backlight)) {
-               dev_err(&client->dev, "ktz8866 failed to register backlight\n");
-               return PTR_ERR(backlight);
+
+       /* 只有 A 芯片注册 backlight 设备 */
+       if (bd->chip == KTZ8866_A) {
+               const char *bl_name = "panel0-backlight";
+               dev_warn(&client->dev, "ktz8866 registering backlight as: %s\n", bl_name);
+               backlight = devm_backlight_device_register(
+                       &client->dev, bl_name, &bd->client->dev, bd,
+                       &ktz8866_backlight_ops, &props);
+               if (IS_ERR(backlight)) {
+                       dev_err(&client->dev, "ktz8866 failed to register backlight\n");
+                       return PTR_ERR(backlight);
+               }
+               i2c_set_clientdata(client, backlight);
+               bd->backlight = backlight;
+       } else {
+               /* B 芯片：不注册 backlight 设备，只打印信息 */
+               dev_info(&client->dev, "KTZ8866B registered as slave, no backlight device\n");
+               /* B 芯片继续执行，用于保存指针和 GPIO 初始化 */
+               goto skip_backlight_reg;
         }
-        dev_warn(&client->dev, "ktz8866 i2c_set_clientdata \n");
-        i2c_set_clientdata(client, backlight);
+
+skip_backlight_reg:
+       dev_warn(&client->dev, "ktz8866 parse_dt \n");
 
         parse_dt(&client->dev, bd->pdata);
-        dev_warn(&client->dev, "ktz8866 parse_dt \n");
 
+       /* 只有 A 芯片需要处理 HW_EN GPIO */
        if (bd->chip == KTZ8866_A) {
                dev_warn(&client->dev,
                         "ktz8866 ktz8866_probe KTZ8866_LCD_DRV_HW_EN\n");
                ret = devm_gpio_request_one(&client->dev, bd->pdata->hw_en_gpio,
@@ -220,16 +240,29 @@ static int ktz8866_probe(struct i2c_client *client,
         if (bd->chip == KTZ8866_A) {
                ktz8866_status.ktz8866a_init = true;
                bd_a = bd;
        } else if (bd->chip == KTZ8866_B) {
                ktz8866_status.ktz8866b_init = true;
                bd_b = bd;
        }
 
+       /* 两个芯片都初始化完成后，检查背光设备是否正确注册 */
        if (ktz8866_status.ktz8866a_init == true &&
            ktz8866_status.ktz8866b_init == true) {
                dev_info(
                        &client->dev,
                        "ktz8866a and ktz8866b init success\n");
+               /* 如果 A 芯片的 backlight 没有正确注册，这里重试一次 */
+               if (bd_a && !bd_a->backlight && bd_a->chip == KTZ8866_A) {
+                       dev_warn(&client->dev, "retry registering backlight for A chip\n");
+                       backlight = devm_backlight_device_register(
+                               &bd_a->client->dev, "panel0-backlight",
+                               &bd_a->client->dev, bd_a,
+                               &ktz8866_backlight_ops, &props);
+                       if (!IS_ERR(backlight)) {
+                               bd_a->backlight = backlight;
+                               i2c_set_clientdata(bd_a->client, backlight);
+                       }
+               }
                // proc_create("bl_selftest", 0644, NULL, &bl_selftest_fops);  // 调试功能已移除
        }
 