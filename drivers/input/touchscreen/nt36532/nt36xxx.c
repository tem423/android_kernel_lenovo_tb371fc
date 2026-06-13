/*
 * Copyright (C) 2010 - 2022 Novatek, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/proc_fs.h>
#include <linux/input/mt.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/irqreturn.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/spi/spi.h>
#include <linux/pm_wakeup.h>
#include <linux/power_supply.h>
#include <uapi/linux/sched/types.h>
#include "nt36xxx.h"

#if defined(CONFIG_DRM_MSM)
#include <linux/msm_drm_notify.h>
#elif defined(CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>
#endif

#if NVT_TOUCH_ESD_PROTECT
#include <linux/jiffies.h>
#endif

#if NVT_TOUCH_EXT_PROC
extern int32_t nvt_extra_proc_init(void);
extern void nvt_extra_proc_deinit(void);
#endif

#if NVT_TOUCH_MP
extern int32_t nvt_mp_proc_init(void);
extern void nvt_mp_proc_deinit(void);
#endif

static int32_t nvt_ts_suspend(struct device *dev);
static int32_t nvt_ts_resume(struct device *dev);
struct nvt_ts_data *ts;

#if BOOT_UPDATE_FIRMWARE
static struct workqueue_struct *nvt_fwu_wq;
extern void Boot_Update_Firmware(struct work_struct *work);
#endif

#if defined(CONFIG_DRM_MSM)
static int nvt_drm_notifier_callback(struct notifier_block *self, unsigned long event, void *data);
#elif defined(CONFIG_FB)
static int nvt_fb_notifier_callback(struct notifier_block *self, unsigned long event, void *data);
#endif

static void release_touch_event(void);
static void release_pen_event(void);

uint32_t ENG_RST_ADDR  = 0x7FFF80;
uint32_t SPI_RD_FAST_ADDR = 0;

#if TOUCH_KEY_NUM > 0
const uint16_t touch_key_array[TOUCH_KEY_NUM] = {
	KEY_BACK,
	KEY_HOME,
	KEY_MENU
};
#endif

#if WAKEUP_GESTURE
const uint16_t gesture_key_array[] = {
	KEY_POWER,  // GESTURE_WORD_C
	KEY_POWER,  // GESTURE_WORD_W
	KEY_POWER,  // GESTURE_WORD_V
	KEY_WAKEUP, // GESTURE_DOUBLE_CLICK
	KEY_POWER,  // GESTURE_WORD_Z
	KEY_POWER,  // GESTURE_WORD_M
	KEY_POWER,  // GESTURE_WORD_O
	KEY_POWER,  // GESTURE_WORD_e
	KEY_POWER,  // GESTURE_WORD_S
	KEY_POWER,  // GESTURE_SLIDE_UP
	KEY_POWER,  // GESTURE_SLIDE_DOWN
	KEY_POWER,  // GESTURE_SLIDE_LEFT
	KEY_POWER,  // GESTURE_SLIDE_RIGHT
};
#endif

static uint8_t bTouchIsAwake = 0;

/* 移除 MTK 相关配置，只保留高通平台的 SPI 配置 */
#ifdef CONFIG_SPI_MSM
/* 高通平台 SPI 配置通常通过 device tree 完成，不需要额外的 config 结构 */
#endif

/* 中断控制 - 简化版本 */
static void nvt_irq_enable(bool enable)
{
	if (enable) {
		if (!ts->irq_enabled) {
			enable_irq(ts->client->irq);
			ts->irq_enabled = true;
		}
	} else {
		if (ts->irq_enabled) {
			disable_irq(ts->client->irq);
			ts->irq_enabled = false;
		}
	}
}

/* SPI 读写函数保持不变 */
static inline int32_t spi_read_write(struct spi_device *client, uint8_t *buf, size_t len, NVT_SPI_RW rw)
{
	struct spi_message m;
	struct spi_transfer t = {
		.len    = len,
	};

	memset(ts->xbuf, 0, len + DUMMY_BYTES);
	memcpy(ts->xbuf, buf, len);

	switch (rw) {
		case NVTREAD:
			t.tx_buf = ts->xbuf;
			t.rx_buf = ts->rbuf;
			t.len    = (len + DUMMY_BYTES);
			break;
		case NVTWRITE:
			t.tx_buf = ts->xbuf;
			break;
	}

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	return spi_sync(client, &m);
}

int32_t CTP_SPI_READ(struct spi_device *client, uint8_t *buf, uint16_t len)
{
	int32_t ret = -1;
	int32_t retries = 0;

	mutex_lock(&ts->xbuf_lock);

	buf[0] = SPI_READ_MASK(buf[0]);

	while (retries < 5) {
		ret = spi_read_write(client, buf, len, NVTREAD);
		if (ret == 0) break;
		retries++;
	}

	if (unlikely(retries == 5)) {
		NVT_ERR("read error, ret=%d\n", ret);
		ret = -EIO;
	} else {
		memcpy((buf+1), (ts->rbuf+2), (len-1));
	}

	mutex_unlock(&ts->xbuf_lock);
	return ret;
}

int32_t CTP_SPI_WRITE(struct spi_device *client, uint8_t *buf, uint16_t len)
{
	int32_t ret = -1;
	int32_t retries = 0;

	mutex_lock(&ts->xbuf_lock);

	buf[0] = SPI_WRITE_MASK(buf[0]);

	while (retries < 5) {
		ret = spi_read_write(client, buf, len, NVTWRITE);
		if (ret == 0) break;
		retries++;
	}

	if (unlikely(retries == 5)) {
		NVT_ERR("error, ret=%d\n", ret);
		ret = -EIO;
	}

	mutex_unlock(&ts->xbuf_lock);
	return ret;
}

/* 其他核心函数保持原样... */
/* nvt_set_page, nvt_write_addr, nvt_read_reg, etc. */

/* 移除所有小米专有代码后的 parse_dt 简化版 */
#ifdef CONFIG_OF
static int32_t nvt_parse_dt(struct device *dev)
{
	struct device_node *np = dev->of_node;
	int32_t ret = 0;

#if NVT_TOUCH_SUPPORT_HW_RST
	ts->reset_gpio = of_get_named_gpio_flags(np, "novatek,reset-gpio", 0, &ts->reset_flags);
	NVT_LOG("novatek,reset-gpio=%d\n", ts->reset_gpio);
#endif
	ts->irq_gpio = of_get_named_gpio_flags(np, "novatek,irq-gpio", 0, &ts->irq_flags);
	NVT_LOG("novatek,irq-gpio=%d\n", ts->irq_gpio);

	ts->pen_support = of_property_read_bool(np, "novatek,pen-support");
	NVT_LOG("novatek,pen-support=%d\n", ts->pen_support);

	ts->stylus_resol_double = of_property_read_bool(np, "novatek,stylus-resol-double");
	NVT_LOG("novatek,stylus-resol-double=%d\n", ts->stylus_resol_double);

	ret = of_property_read_u32(np, "novatek,spi-rd-fast-addr", &SPI_RD_FAST_ADDR);
	if (ret) {
		NVT_LOG("not support novatek,spi-rd-fast-addr\n");
		SPI_RD_FAST_ADDR = 0;
		ret = 0;
	} else {
		NVT_LOG("SPI_RD_FAST_ADDR=0x%06X\n", SPI_RD_FAST_ADDR);
	}

	/* 移除 config-array-size 相关代码，简化处理 */
	ts->fw_name = BOOT_UPDATE_FIRMWARE_NAME;

	return 0;
}
#endif

/* 移除 nvt_get_panel_type, is_lockdown_empty, nvt_match_fw 等函数 */

/* 简化 GPIO 配置 */
static int nvt_gpio_config(struct nvt_ts_data *ts)
{
	int32_t ret = 0;

#if NVT_TOUCH_SUPPORT_HW_RST
	if (gpio_is_valid(ts->reset_gpio)) {
		ret = gpio_request_one(ts->reset_gpio, GPIOF_OUT_INIT_LOW, "NVT-tp-rst");
		if (ret) {
			NVT_ERR("Failed to request NVT-tp-rst GPIO\n");
			goto err_request_reset_gpio;
		}
	}
#endif

	if (gpio_is_valid(ts->irq_gpio)) {
		ret = gpio_request_one(ts->irq_gpio, GPIOF_IN, "NVT-int");
		if (ret) {
			NVT_ERR("Failed to request NVT-int GPIO\n");
			goto err_request_irq_gpio;
		}
	}

	return ret;

err_request_irq_gpio:
#if NVT_TOUCH_SUPPORT_HW_RST
	gpio_free(ts->reset_gpio);
err_request_reset_gpio:
#endif
	return ret;
}

/* 移除 nvt_set_dbgfw_status 相关函数 */

/* ESD 保护相关保持原样 */
#if NVT_TOUCH_ESD_PROTECT
void nvt_esd_check_enable(uint8_t enable)
{
	irq_timer = jiffies;
	esd_retry = enable ? 0 : esd_retry;
	esd_check = enable;
}

static void nvt_esd_check_func(struct work_struct *work)
{
	unsigned int timer = jiffies_to_msecs(jiffies - irq_timer);

	if ((timer > NVT_TOUCH_ESD_CHECK_PERIOD) && esd_check) {
		mutex_lock(&ts->lock);
		NVT_ERR("do ESD recovery, timer = %d, retry = %d\n", timer, esd_retry);
		nvt_update_firmware(ts->fw_name);
		mutex_unlock(&ts->lock);
		irq_timer = jiffies;
		esd_retry++;
	}
	queue_delayed_work(nvt_esd_check_wq, &nvt_esd_check_work,
			msecs_to_jiffies(NVT_TOUCH_ESD_CHECK_PERIOD));
}
#endif

/* 简化的中断处理函数 - 移除小米专有调用 */
static irqreturn_t nvt_ts_work_func(int irq, void *data)
{
	int32_t ret = -1;
	uint8_t point_data[POINT_DATA_LEN + PEN_DATA_LEN + 1 + DUMMY_BYTES] = {0};
	/* ... 变量声明 ... */

#if WAKEUP_GESTURE
	if (bTouchIsAwake == 0) {
		pm_wakeup_event(&ts->input_dev->dev, 5000);
	}
#endif

	/* 移除 touch_irq_boost() 和 lpm_disable_for_dev() 调用 */

	mutex_lock(&ts->lock);

	if (ts->dev_pm_suspend) {
		ret = wait_for_completion_timeout(&ts->dev_pm_suspend_completion, msecs_to_jiffies(500));
		if (!ret) {
			NVT_ERR("system(spi) can't finished resuming procedure, skip it\n");
			goto XFER_ERROR;
		}
	}

	/* SPI 读取和触摸处理逻辑保持不变 */
	/* ... */

XFER_ERROR:
	mutex_unlock(&ts->lock);
	return IRQ_HANDLED;
}

/* 简化的 suspend/resume 函数 - 移除小米专有调用 */
static int32_t nvt_ts_suspend(struct device *dev)
{
	/* ... 保持原有逻辑，但移除以下调用： */
	/* - dsi_panel_doubleclick_enable */
	/* - nvt_game_mode_recovery */
	/* - switch_pen_input_device */
	/* ... */
}

static int32_t nvt_ts_resume(struct device *dev)
{
	/* ... 保持原有逻辑，但移除以下调用： */
	/* - dsi_panel_doubleclick_enable */
	/* - nvt_game_mode_recovery */
	/* - switch_pen_input_device */
	/* ... */
}

/* 高通标准 DRM notifier */
#if defined(CONFIG_DRM_MSM)
static int nvt_drm_notifier_callback(struct notifier_block *self, unsigned long event, void *data)
{
	struct msm_drm_notifier *evdata = data;
	int *blank;
	struct nvt_ts_data *ts = container_of(self, struct nvt_ts_data, drm_notif);

	if (!evdata || evdata->id != 0)
		return 0;

	if (evdata->data && ts) {
		blank = evdata->data;
		if (event == MSM_DRM_EARLY_EVENT_BLANK) {
			if (*blank == MSM_DRM_BLANK_POWERDOWN) {
				nvt_ts_suspend(&ts->client->dev);
			}
		} else if (event == MSM_DRM_EVENT_BLANK) {
			if (*blank == MSM_DRM_BLANK_UNBLANK) {
				nvt_ts_resume(&ts->client->dev);
			}
		}
	}
	return 0;
}
#endif

/* 移除所有小米专有的 proc 和 debugfs 代码 */

/* probe 函数简化版 */
static int32_t nvt_ts_probe(struct spi_device *client)
{
	int32_t ret = 0;

	/* 基本分配和初始化 */
	ts = kzalloc(sizeof(struct nvt_ts_data), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->xbuf = kzalloc(NVT_XBUF_LEN, GFP_KERNEL);
	ts->rbuf = kzalloc(NVT_READ_LEN, GFP_KERNEL);
	/* 错误处理... */

	ts->client = client;
	spi_set_drvdata(client, ts);

	/* SPI 配置 */
	ts->client->bits_per_word = 8;
	ts->client->mode = SPI_MODE_0;
	ret = spi_setup(ts->client);

	/* GPIO 和 pinctrl 配置 */
	ret = nvt_parse_dt(&client->dev);
	ret = nvt_gpio_config(ts);

	/* 互斥锁初始化 */
	mutex_init(&ts->lock);
	mutex_init(&ts->xbuf_lock);

	/* 硬件复位和芯片检测 */
	nvt_eng_reset();
#if NVT_TOUCH_SUPPORT_HW_RST
	gpio_set_value(ts->reset_gpio, 1);
#endif
	msleep(10);
	ret = nvt_ts_check_chip_ver_trim_loop();

	/* Input 设备注册 */
	ts->input_dev = input_allocate_device();
	/* 设置 input 设备参数... */

	/* 中断注册 */
	client->irq = gpio_to_irq(ts->irq_gpio);
	ret = request_threaded_irq(client->irq, NULL, nvt_ts_work_func,
			ts->int_trigger_type | IRQF_ONESHOT, NVT_SPI_NAME, ts);

	/* 电源管理初始化 */
	device_init_wakeup(&ts->input_dev->dev, 1);

	/* 固件更新工作队列 */
#if BOOT_UPDATE_FIRMWARE
	nvt_fwu_wq = alloc_workqueue("nvt_fwu_wq", WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	INIT_DELAYED_WORK(&ts->nvt_fwu_work, Boot_Update_Firmware);
	queue_delayed_work(nvt_fwu_wq, &ts->nvt_fwu_work, msecs_to_jiffies(14000));
#endif

	/* ESD 保护工作队列 */
#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_wq = alloc_workqueue("nvt_esd_check_wq", WQ_MEM_RECLAIM, 1);
	INIT_DELAYED_WORK(&nvt_esd_check_work, nvt_esd_check_func);
	queue_delayed_work(nvt_esd_check_wq, &nvt_esd_check_work,
			msecs_to_jiffies(NVT_TOUCH_ESD_CHECK_PERIOD));
#endif

	/* DRM notifier 注册 */
#if defined(CONFIG_DRM_MSM)
	ts->drm_notif.notifier_call = nvt_drm_notifier_callback;
	ret = msm_drm_register_client(&ts->drm_notif);
#endif

	/* 电源供应通知器 */
	INIT_WORK(&ts->power_supply_work, nvt_power_supply_work);
	ts->power_supply_notifier.notifier_call = nvt_power_supply_event;
	power_supply_reg_notifier(&ts->power_supply_notifier);

	bTouchIsAwake = 1;
	nvt_irq_enable(true);

	return 0;

	/* 错误处理... */
}

/* 模块入口保持原样 */
static bool nvt_off_charger_mode(void)
{
	/* 保留充电器模式检测 */
	bool charger_mode = false;
	char *chose = strnstr(saved_command_line, "androidboot.mode=", strlen(saved_command_line));
	if (chose && !strncmp(chose + strlen("androidboot.mode="), "charger", 7))
		charger_mode = true;
	return charger_mode;
}

static int32_t __init nvt_driver_init(void)
{
	if (nvt_off_charger_mode())
		return 0;
	return spi_register_driver(&nvt_spi_driver);
}

static void __exit nvt_driver_exit(void)
{
	spi_unregister_driver(&nvt_spi_driver);
}

module_init(nvt_driver_init);
module_exit(nvt_driver_exit);

MODULE_DESCRIPTION("Novatek Touchscreen Driver");
MODULE_LICENSE("GPL");