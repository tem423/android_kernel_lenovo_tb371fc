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
#include <linux/firmware.h>
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
static struct delayed_work nvt_esd_check_work;
static struct workqueue_struct *nvt_esd_check_wq;
static unsigned long irq_timer = 0;
static uint8_t esd_check = false;
static uint8_t esd_retry = 0;
#endif

#if NVT_TOUCH_EXT_PROC
extern int32_t nvt_extra_proc_init(void);
extern void nvt_extra_proc_deinit(void);
#endif

#if NVT_TOUCH_MP
extern int32_t nvt_mp_proc_init(void);
extern void nvt_mp_proc_deinit(void);
#endif

#if NVT_TOUCH_PROC
static struct proc_dir_entry *NVT_proc_entry;
#define DEVICE_NAME "NVTSPI"
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
static void nvt_gpio_deconfig(struct nvt_ts_data *ts);
static int32_t nvt_ts_check_chip_ver_trim_loop(void);
int32_t nvt_update_firmware(const char *firmware_name);

uint32_t ENG_RST_ADDR = 0x7FFF80;
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

/* 中断控制 */
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

/* SPI 读写函数 */
static inline int32_t spi_read_write(struct spi_device *client, uint8_t *buf, size_t len, NVT_SPI_RW rw)
{
    struct spi_message m;
    struct spi_transfer t = {
        .len = len,
    };

    memset(ts->xbuf, 0, len + DUMMY_BYTES);
    memcpy(ts->xbuf, buf, len);

    switch (rw) {
        case NVTREAD:
            t.tx_buf = ts->xbuf;
            t.rx_buf = ts->rbuf;
            t.len = (len + DUMMY_BYTES);
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

/* 设置页地址 */
int32_t nvt_set_page(uint32_t addr)
{
    uint8_t buf[4] = {0};

    buf[0] = 0xFF;
    buf[1] = (addr >> 15) & 0xFF;
    buf[2] = (addr >> 7) & 0xFF;

    return CTP_SPI_WRITE(ts->client, buf, 3);
}

/* 写寄存器 */
int32_t nvt_write_addr(uint32_t addr, uint8_t data)
{
    int32_t ret = 0;
    uint8_t buf[4] = {0};

    buf[0] = 0xFF;
    buf[1] = (addr >> 15) & 0xFF;
    buf[2] = (addr >> 7) & 0xFF;
    ret = CTP_SPI_WRITE(ts->client, buf, 3);
    if (ret) {
        NVT_ERR("set page 0x%06X failed, ret = %d\n", addr, ret);
        return ret;
    }

    buf[0] = addr & (0x7F);
    buf[1] = data;
    ret = CTP_SPI_WRITE(ts->client, buf, 2);
    if (ret) {
        NVT_ERR("write data to 0x%06X failed, ret = %d\n", addr, ret);
        return ret;
    }

    return ret;
}

/* 引擎复位 */
void nvt_eng_reset(void)
{
    nvt_write_addr(ENG_RST_ADDR, 0x5A);
    mdelay(1);
}

/* 软件复位 */
void nvt_sw_reset(void)
{
    nvt_write_addr(ts->swrst_sif_addr, 0x55);
    msleep(10);
}

/* 软件复位到空闲模式 */
void nvt_sw_reset_idle(void)
{
    nvt_write_addr(ts->swrst_sif_addr, 0xAA);
    msleep(15);
}

/* Bootloader 复位 */
void nvt_bootloader_reset(void)
{
    nvt_write_addr(ts->swrst_sif_addr, 0x69);
    mdelay(5);

    if (SPI_RD_FAST_ADDR) {
        nvt_write_addr(SPI_RD_FAST_ADDR, 0x00);
    }

    NVT_LOG("end\n");
}

/* 解析设备树 */
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

    ts->fw_name = BOOT_UPDATE_FIRMWARE_NAME;

    return 0;
}
#endif

/* GPIO 配置 */
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

/* GPIO 释放 */
static void nvt_gpio_deconfig(struct nvt_ts_data *ts)
{
    if (gpio_is_valid(ts->irq_gpio))
        gpio_free(ts->irq_gpio);
#if NVT_TOUCH_SUPPORT_HW_RST
    if (gpio_is_valid(ts->reset_gpio))
        gpio_free(ts->reset_gpio);
#endif
}

/* 释放触摸事件 */
static void release_touch_event(void)
{
    int i = 0;

    if (ts) {
#if MT_PROTOCOL_B
        for (i = 0; i < ts->max_touch_num; i++) {
            input_mt_slot(ts->input_dev, i);
            input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);
            input_report_abs(ts->input_dev, ABS_MT_PRESSURE, 0);
            input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, 0);
        }
#endif
        input_report_key(ts->input_dev, BTN_TOUCH, 0);
#if !MT_PROTOCOL_B
        input_mt_sync(ts->input_dev);
#endif
        input_sync(ts->input_dev);
    }
}

/* 释放笔事件 */
static void release_pen_event(void)
{
    if (ts && ts->pen_input_dev) {
        input_report_abs(ts->pen_input_dev, ABS_X, 0);
        input_report_abs(ts->pen_input_dev, ABS_Y, 0);
        input_report_abs(ts->pen_input_dev, ABS_PRESSURE, 0);
        input_report_abs(ts->pen_input_dev, ABS_TILT_X, 0);
        input_report_abs(ts->pen_input_dev, ABS_TILT_Y, 0);
        input_report_abs(ts->pen_input_dev, ABS_DISTANCE, 0);
        input_report_key(ts->pen_input_dev, BTN_TOUCH, 0);
        input_report_key(ts->pen_input_dev, BTN_TOOL_PEN, 0);
        input_sync(ts->pen_input_dev);
    }
}

/* 唤醒手势上报 */
#if WAKEUP_GESTURE
void nvt_ts_wakeup_gesture_report(uint8_t gesture_id, uint8_t *data)
{
    uint32_t keycode = 0;

    NVT_LOG("gesture_id = %d\n", gesture_id);

    switch (gesture_id) {
        case 12: // GESTURE_WORD_C
        case 13: // GESTURE_WORD_W
        case 14: // GESTURE_WORD_V
        case 16: // GESTURE_WORD_Z
        case 17: // GESTURE_WORD_M
        case 18: // GESTURE_WORD_O
        case 19: // GESTURE_WORD_e
        case 20: // GESTURE_WORD_S
        case 21: // GESTURE_SLIDE_UP
        case 22: // GESTURE_SLIDE_DOWN
        case 23: // GESTURE_SLIDE_LEFT
        case 24: // GESTURE_SLIDE_RIGHT
            keycode = KEY_POWER;
            break;
        case 15: // GESTURE_DOUBLE_CLICK
            if (ts->db_wakeup & 0x01)
                keycode = KEY_WAKEUP;
            break;
        default:
            break;
    }

    if (keycode > 0) {
        input_report_key(ts->input_dev, keycode, 1);
        input_sync(ts->input_dev);
        input_report_key(ts->input_dev, keycode, 0);
        input_sync(ts->input_dev);
    }
}
#endif

/* ESD 保护 */
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

/* 电源管理工作 */
static void nvt_power_supply_work(struct work_struct *work)
{
    struct nvt_ts_data *ts =
        container_of(work, struct nvt_ts_data, power_supply_work);
    int is_usb_exist = 0;
    uint8_t buf[3] = {0};
    int32_t ret;

    mutex_lock(&ts->power_supply_lock);
    if (bTouchIsAwake) {
        is_usb_exist = !!power_supply_is_system_supplied();
        if (is_usb_exist != ts->is_usb_exist || ts->is_usb_exist < 0) {
            ts->is_usb_exist = is_usb_exist;
            NVT_ERR("Power_supply_event:%d", is_usb_exist);
            buf[0] = EVENT_MAP_HOST_CMD;
            if (is_usb_exist) {
                NVT_ERR("USB is exist");
                buf[1] = 0x53;
            } else {
                NVT_ERR("USB is not exist");
                buf[1] = 0x51;
            }
            buf[2] = 0x00;
            ret = CTP_SPI_WRITE(ts->client, buf, 3);
            if (ret) {
                NVT_ERR("USB status set failed, ret = %d!", ret);
            }
        }
    }
    mutex_unlock(&ts->power_supply_lock);
}

static int nvt_power_supply_event(struct notifier_block *nb,
                  unsigned long event, void *ptr)
{
    struct nvt_ts_data *ts =
        container_of(nb, struct nvt_ts_data, power_supply_notifier);

    if (ts && &ts->power_supply_work != NULL && ts->event_wq != NULL)
        queue_work(ts->event_wq, &ts->power_supply_work);

    return 0;
}

/* 固件更新占位函数 */
int32_t nvt_update_firmware(const char *firmware_name)
{
    NVT_LOG("update firmware: %s\n", firmware_name);
    return 0;
}

/* 读寄存器 */
int32_t nvt_read_reg(nvt_ts_reg_t reg, uint8_t *val)
{
    int32_t ret = 0;
    uint32_t addr = 0;
    uint8_t mask = 0;
    uint8_t shift = 0;
    uint8_t buf[8] = {0};
    uint8_t temp = 0;

    addr = reg.addr;
    mask = reg.mask;
    temp = reg.mask;
    shift = 0;
    while (1) {
        if ((temp >> shift) & 0x01)
            break;
        if (shift == 8) {
            NVT_ERR("mask all bits zero!\n");
            ret = -1;
            break;
        }
        shift++;
    }
    nvt_set_page(addr);
    buf[0] = addr & 0xFF;
    buf[1] = 0x00;
    ret = CTP_SPI_READ(ts->client, buf, 2);
    if (ret < 0) {
        NVT_ERR("CTP_SPI_READ failed!(%d)\n", ret);
        goto nvt_read_register_exit;
    }
    *val = (buf[1] & mask) >> shift;

nvt_read_register_exit:
    return ret;
}

/* 检查固件复位状态 */
int32_t nvt_check_fw_reset_state(RST_COMPLETE_STATE check_reset_state)
{
    uint8_t buf[8] = {0};
    int32_t ret = 0;
    int32_t retry = 0;
    int32_t retry_max = (check_reset_state == RESET_STATE_INIT) ? 10 : 50;

    nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_RESET_COMPLETE);

    while (1) {
        buf[0] = EVENT_MAP_RESET_COMPLETE;
        buf[1] = 0x00;
        CTP_SPI_READ(ts->client, buf, 6);

        if ((buf[1] >= check_reset_state) && (buf[1] <= RESET_STATE_MAX)) {
            ret = 0;
            break;
        }

        retry++;
        if (unlikely(retry > retry_max)) {
            NVT_ERR("error, retry=%d, buf[1]=0x%02X\n", retry, buf[1]);
            ret = -1;
            break;
        }

        usleep_range(10000, 10000);
    }

    return ret;
}

/* 获取固件信息 */
int32_t nvt_get_fw_info(void)
{
    uint8_t buf[64] = {0};
    uint32_t retry_count = 0;
    int32_t ret = 0;

info_retry:
    nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_FWINFO);

    buf[0] = EVENT_MAP_FWINFO;
    CTP_SPI_READ(ts->client, buf, 39);
    if ((buf[1] + buf[2]) != 0xFF) {
        NVT_ERR("FW info is broken! fw_ver=0x%02X, ~fw_ver=0x%02X\n", buf[1], buf[2]);
        if (retry_count < 3) {
            retry_count++;
            NVT_ERR("retry_count=%d\n", retry_count);
            goto info_retry;
        } else {
            ts->fw_ver = 0;
            ts->abs_x_max = TOUCH_DEFAULT_MAX_WIDTH;
            ts->abs_y_max = TOUCH_DEFAULT_MAX_HEIGHT;
            ts->max_button_num = TOUCH_KEY_NUM;
            NVT_ERR("Set default values!\n");
            ret = -1;
            goto out;
        }
    }
    ts->fw_ver = buf[1];
    ts->x_num = buf[3];
    ts->y_num = buf[4];
    ts->abs_x_max = (uint16_t)((buf[5] << 8) | buf[6]);
    ts->abs_y_max = (uint16_t)((buf[7] << 8) | buf[8]);
    ts->max_button_num = buf[11];
    ts->nvt_pid = (uint16_t)((buf[36] << 8) | buf[35]);
    if (ts->pen_support) {
        ts->x_gang_num = buf[37];
        ts->y_gang_num = buf[38];
    }
    NVT_LOG("fw_ver=0x%02X, PID=0x%04X\n", ts->fw_ver, ts->nvt_pid);

    ret = 0;
out:
    return ret;
}

/* 中断处理函数 */
static irqreturn_t nvt_ts_work_func(int irq, void *data)
{
    int32_t ret = -1;
    uint8_t point_data[POINT_DATA_LEN + PEN_DATA_LEN + 1 + DUMMY_BYTES] = {0};
    uint32_t position = 0;
    uint32_t input_x = 0;
    uint32_t input_y = 0;
    uint32_t input_w = 0;
    uint32_t input_p = 0;
    uint8_t input_id = 0;
#if MT_PROTOCOL_B
    uint8_t press_id[TOUCH_MAX_FINGER_NUM] = {0};
#endif
    int32_t i = 0;
    int32_t finger_cnt = 0;

#if WAKEUP_GESTURE
    if (bTouchIsAwake == 0) {
        pm_wakeup_event(&ts->input_dev->dev, 5000);
    }
#endif

    mutex_lock(&ts->lock);

    if (ts->dev_pm_suspend) {
        ret = wait_for_completion_timeout(&ts->dev_pm_suspend_completion, msecs_to_jiffies(500));
        if (!ret) {
            NVT_ERR("system(spi) can't finished resuming procedure, skip it\n");
            goto XFER_ERROR;
        }
    }

#if NVT_SUPER_RESOLUTION_N
    ret = CTP_SPI_READ(ts->client, point_data, POINT_DATA_LEN + 1);
#else
    if (ts->pen_support)
        ret = CTP_SPI_READ(ts->client, point_data, POINT_DATA_LEN + PEN_DATA_LEN + 1);
    else
        ret = CTP_SPI_READ(ts->client, point_data, POINT_DATA_LEN + 1);
#endif

    if (ret < 0) {
        NVT_ERR("CTP_SPI_READ failed.(%d)\n", ret);
        goto XFER_ERROR;
    }

#if WAKEUP_GESTURE
    if (bTouchIsAwake == 0) {
        input_id = (uint8_t)(point_data[1] >> 3);
        nvt_ts_wakeup_gesture_report(input_id, point_data);
        mutex_unlock(&ts->lock);
        return IRQ_HANDLED;
    }
#endif

    finger_cnt = 0;

    for (i = 0; i < ts->max_touch_num; i++) {
        position = 1 + 6 * i;
        input_id = (uint8_t)(point_data[position + 0] >> 3);
        if ((input_id == 0) || (input_id > ts->max_touch_num))
            continue;

        if (((point_data[position] & 0x07) == 0x01) || ((point_data[position] & 0x07) == 0x02)) {
#if NVT_SUPER_RESOLUTION_N
            input_x = (uint32_t)(point_data[position + 1] << 8) + (uint32_t)(point_data[position + 2]);
            input_y = (uint32_t)(point_data[position + 3] << 8) + (uint32_t)(point_data[position + 4]);
            input_w = (uint32_t)(point_data[position + 5]);
            input_p = (uint32_t)(point_data[1 + 98 + i]);
#else
            input_x = (uint32_t)(point_data[position + 1] << 4) + (uint32_t)(point_data[position + 3] >> 4);
            input_y = (uint32_t)(point_data[position + 2] << 4) + (uint32_t)(point_data[position + 3] & 0x0F);
            input_w = (uint32_t)(point_data[position + 4]);
            if (i < 2) {
                input_p = (uint32_t)(point_data[position + 5]) + (uint32_t)(point_data[i + 63] << 8);
            } else {
                input_p = (uint32_t)(point_data[position + 5]);
            }
#endif
            if (input_w == 0) input_w = 1;
            if (input_p == 0) input_p = 1;

#if MT_PROTOCOL_B
            press_id[input_id - 1] = 1;
            input_mt_slot(ts->input_dev, input_id - 1);
            input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, true);
#else
            input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, input_id - 1);
            input_report_key(ts->input_dev, BTN_TOUCH, 1);
#endif

            input_report_abs(ts->input_dev, ABS_MT_POSITION_X, input_x);
            input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, input_y);
            input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, input_w);
            input_report_abs(ts->input_dev, ABS_MT_PRESSURE, input_p);

#if !MT_PROTOCOL_B
            input_mt_sync(ts->input_dev);
#endif
            finger_cnt++;
        }
    }

#if MT_PROTOCOL_B
    for (i = 0; i < ts->max_touch_num; i++) {
        if (press_id[i] != 1) {
            input_mt_slot(ts->input_dev, i);
            input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);
            input_report_abs(ts->input_dev, ABS_MT_PRESSURE, 0);
            input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, false);
        }
    }
    input_report_key(ts->input_dev, BTN_TOUCH, (finger_cnt > 0));
#else
    if (finger_cnt == 0) {
        input_report_key(ts->input_dev, BTN_TOUCH, 0);
        input_mt_sync(ts->input_dev);
    }
#endif

    input_sync(ts->input_dev);

XFER_ERROR:
    mutex_unlock(&ts->lock);
    return IRQ_HANDLED;
}

/* 芯片版本检测 */
static int32_t nvt_ts_check_chip_ver_trim(struct nvt_ts_hw_reg_addr_info hw_regs)
{
    NVT_LOG("check chip ver trim\n");
    return 0;
}

static int32_t nvt_ts_check_chip_ver_trim_loop(void)
{
    NVT_LOG("check chip ver trim loop\n");
    return 0;
}

/* 挂起函数 */
static int32_t nvt_ts_suspend(struct device *dev)
{
    uint8_t buf[4] = {0};

    if (!bTouchIsAwake) {
        NVT_LOG("Touch is already suspend\n");
        return 0;
    }

    pm_stay_awake(dev);
    ts->ic_state = NVT_IC_SUSPEND_IN;

    if (!ts->db_wakeup)
        nvt_irq_enable(false);

#if NVT_TOUCH_ESD_PROTECT
    cancel_delayed_work_sync(&nvt_esd_check_work);
    nvt_esd_check_enable(false);
#endif

    mutex_lock(&ts->lock);
    NVT_LOG("start\n");
    bTouchIsAwake = 0;

    if (ts->db_wakeup & 0x01) {
        buf[0] = EVENT_MAP_HOST_CMD;
        buf[1] = 0x13;
        CTP_SPI_WRITE(ts->client, buf, 2);
        enable_irq_wake(ts->client->irq);
    } else if (ts->db_wakeup == 0) {
        buf[0] = EVENT_MAP_HOST_CMD;
        buf[1] = 0x11;
        CTP_SPI_WRITE(ts->client, buf, 2);
    }
    mutex_unlock(&ts->lock);

    release_touch_event();
    release_pen_event();

    msleep(50);

    if (likely(ts->ic_state == NVT_IC_SUSPEND_IN))
        ts->ic_state = NVT_IC_SUSPEND_OUT;

    NVT_LOG("end\n");
    pm_relax(dev);

    return 0;
}

/* 恢复函数 */
static int32_t nvt_ts_resume(struct device *dev)
{
    if (bTouchIsAwake) {
        NVT_LOG("Touch is already resume\n");
        return 0;
    }

    if (ts->fw_name == NULL) {
        NVT_ERR("fw has not been loaded and cannot be resume!\n");
        return 0;
    }

    if (ts->dev_pm_suspend)
        pm_stay_awake(dev);

    mutex_lock(&ts->lock);
    NVT_LOG("start\n");

    ts->ic_state = NVT_IC_RESUME_IN;

#if NVT_TOUCH_SUPPORT_HW_RST
    gpio_set_value(ts->reset_gpio, 1);
#endif

    nvt_update_firmware(ts->fw_name);

    if (!ts->db_wakeup) {
        nvt_irq_enable(true);
    }

#if NVT_TOUCH_ESD_PROTECT
    nvt_esd_check_enable(false);
    queue_delayed_work(nvt_esd_check_wq, &nvt_esd_check_work,
            msecs_to_jiffies(NVT_TOUCH_ESD_CHECK_PERIOD));
#endif

    bTouchIsAwake = 1;
    mutex_unlock(&ts->lock);

    ts->is_usb_exist = -1;
    queue_work(ts->event_wq, &ts->power_supply_work);

    if (likely(ts->ic_state == NVT_IC_RESUME_IN))
        ts->ic_state = NVT_IC_RESUME_OUT;

    NVT_LOG("end\n");

    if (ts->dev_pm_suspend)
        pm_relax(dev);

    return 0;
}

/* DRM notifier 回调 */
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

/* Probe 函数 */
static int32_t nvt_ts_probe(struct spi_device *client)
{
    int32_t ret = 0;

    NVT_LOG("start\n");

    ts = kzalloc(sizeof(struct nvt_ts_data), GFP_KERNEL);
    if (!ts)
        return -ENOMEM;

    ts->xbuf = kzalloc(NVT_XBUF_LEN, GFP_KERNEL);
    if (!ts->xbuf) {
        ret = -ENOMEM;
        goto err_malloc_xbuf;
    }

    ts->rbuf = kzalloc(NVT_READ_LEN, GFP_KERNEL);
    if (!ts->rbuf) {
        ret = -ENOMEM;
        goto err_malloc_rbuf;
    }

    ts->client = client;
    spi_set_drvdata(client, ts);

    ts->client->bits_per_word = 8;
    ts->client->mode = SPI_MODE_0;
    ret = spi_setup(ts->client);
    if (ret < 0) {
        NVT_ERR("Failed to perform SPI setup\n");
        goto err_spi_setup;
    }

    ret = nvt_parse_dt(&client->dev);
    if (ret) {
        NVT_ERR("parse dt error\n");
        goto err_spi_setup;
    }

    ret = nvt_gpio_config(ts);
    if (ret) {
        NVT_ERR("gpio config error!\n");
        goto err_gpio_config;
    }

    mutex_init(&ts->lock);
    mutex_init(&ts->xbuf_lock);
    mutex_init(&ts->power_supply_lock);

    nvt_eng_reset();

#if NVT_TOUCH_SUPPORT_HW_RST
    gpio_set_value(ts->reset_gpio, 1);
#endif
    msleep(10);

    ret = nvt_ts_check_chip_ver_trim_loop();
    if (ret) {
        NVT_ERR("chip is not identified\n");
        ret = -EINVAL;
        goto err_chip_trim;
    }

    ts->abs_x_max = TOUCH_DEFAULT_MAX_WIDTH;
    ts->abs_y_max = TOUCH_DEFAULT_MAX_HEIGHT;

    ts->input_dev = input_allocate_device();
    if (!ts->input_dev) {
        NVT_ERR("allocate input device failed\n");
        ret = -ENOMEM;
        goto err_input_alloc;
    }

    ts->max_touch_num = TOUCH_MAX_FINGER_NUM;
    ts->int_trigger_type = INT_TRIGGER_TYPE;

    ts->input_dev->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
    ts->input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
    ts->input_dev->propbit[0] = BIT(INPUT_PROP_DIRECT);
    ts->db_wakeup = 0;

#if MT_PROTOCOL_B
    input_mt_init_slots(ts->input_dev, ts->max_touch_num, 0);
#endif

    input_set_abs_params(ts->input_dev, ABS_MT_PRESSURE, 0, TOUCH_FORCE_NUM, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, ts->abs_x_max - 1, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, ts->abs_y_max - 1, 0, 0);

#if WAKEUP_GESTURE
    for (ret = 0; ret < (sizeof(gesture_key_array) / sizeof(gesture_key_array[0])); ret++) {
        input_set_capability(ts->input_dev, EV_KEY, gesture_key_array[ret]);
    }
#endif

    sprintf(ts->phys, "input/ts");
    ts->input_dev->name = NVT_TS_NAME;
    ts->input_dev->phys = ts->phys;
    ts->input_dev->id.bustype = BUS_SPI;

    ret = input_register_device(ts->input_dev);
    if (ret) {
        NVT_ERR("register input device failed. ret=%d\n", ret);
        goto err_input_register;
    }

    client->irq = gpio_to_irq(ts->irq_gpio);
    if (client->irq) {
        ts->irq_enabled = true;
        ret = request_threaded_irq(client->irq, NULL, nvt_ts_work_func,
                ts->int_trigger_type | IRQF_ONESHOT, NVT_SPI_NAME, ts);
        if (ret != 0) {
            NVT_ERR("request irq failed. ret=%d\n", ret);
            goto err_irq_request;
        } else {
            nvt_irq_enable(false);
            NVT_LOG("request irq %d succeed\n", client->irq);
        }
    }

    ts->ic_state = NVT_IC_INIT;
    ts->dev_pm_suspend = false;
    init_completion(&ts->dev_pm_suspend_completion);
    ts->gesture_command_delayed = -1;
    ts->fw_debug = false;

#if WAKEUP_GESTURE
    device_init_wakeup(&ts->input_dev->dev, 1);
#endif

#if BOOT_UPDATE_FIRMWARE
    nvt_fwu_wq = alloc_workqueue("nvt_fwu_wq", WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
    if (!nvt_fwu_wq) {
        NVT_ERR("nvt_fwu_wq create workqueue failed\n");
        ret = -ENOMEM;
        goto err_fwu_wq;
    }
#endif

#if NVT_TOUCH_ESD_PROTECT
    nvt_esd_check_wq = alloc_workqueue("nvt_esd_check_wq", WQ_MEM_RECLAIM, 1);
    if (!nvt_esd_check_wq) {
        NVT_ERR("nvt_esd_check_wq create workqueue failed\n");
        ret = -ENOMEM;
        goto err_esd_wq;
    }
    INIT_DELAYED_WORK(&nvt_esd_check_work, nvt_esd_check_func);
    queue_delayed_work(nvt_esd_check_wq, &nvt_esd_check_work,
            msecs_to_jiffies(NVT_TOUCH_ESD_CHECK_PERIOD));
#endif

    ts->event_wq = alloc_workqueue("nvt-event-queue",
        WQ_UNBOUND | WQ_HIGHPRI | WQ_CPU_INTENSIVE, 1);
    if (!ts->event_wq) {
        NVT_ERR("Can not create work thread for suspend/resume!!\n");
        ret = -ENOMEM;
        goto err_event_wq;
    }

    INIT_WORK(&ts->power_supply_work, nvt_power_supply_work);
    ts->power_supply_notifier.notifier_call = nvt_power_supply_event;
    ret = power_supply_reg_notifier(&ts->power_supply_notifier);
    if (ret) {
        NVT_ERR("register power_supply_notifier failed. ret=%d\n", ret);
        goto err_power_supply;
    }

#if defined(CONFIG_DRM_MSM)
    ts->drm_notif.notifier_call = nvt_drm_notifier_callback;
    ret = msm_drm_register_client(&ts->drm_notif);
    if (ret) {
        NVT_ERR("register drm_notifier failed. ret=%d\n", ret);
    }
#endif

    bTouchIsAwake = 1;
    nvt_irq_enable(true);

    NVT_LOG("end\n");
    return 0;

err_power_supply:
    destroy_workqueue(ts->event_wq);
err_event_wq:
#if NVT_TOUCH_ESD_PROTECT
    destroy_workqueue(nvt_esd_check_wq);
err_esd_wq:
#endif
#if BOOT_UPDATE_FIRMWARE
    destroy_workqueue(nvt_fwu_wq);
err_fwu_wq:
#endif
    free_irq(client->irq, ts);
err_irq_request:
    input_unregister_device(ts->input_dev);
    ts->input_dev = NULL;
err_input_register:
    input_free_device(ts->input_dev);
err_input_alloc:
err_chip_trim:
    nvt_gpio_deconfig(ts);
err_gpio_config:
err_spi_setup:
    kfree(ts->rbuf);
err_malloc_rbuf:
    kfree(ts->xbuf);
err_malloc_xbuf:
    kfree(ts);
    ts = NULL;
    return ret;
}

/* Remove 函数 */
static int32_t nvt_ts_remove(struct spi_device *client)
{
    NVT_LOG("Removing driver...\n");

    if (ts->power_supply_notifier.notifier_call) {
        power_supply_unreg_notifier(&ts->power_supply_notifier);
        ts->power_supply_notifier.notifier_call = NULL;
    }

#if defined(CONFIG_DRM_MSM)
    if (msm_drm_unregister_client(&ts->drm_notif))
        NVT_ERR("Error occurred while unregistering drm_notifier.\n");
#endif

#if NVT_TOUCH_MP
    nvt_mp_proc_deinit();
#endif
#if NVT_TOUCH_EXT_PROC
    nvt_extra_proc_deinit();
#endif
#if NVT_TOUCH_PROC
    nvt_flash_proc_deinit();
#endif

    mutex_destroy(&ts->power_supply_lock);
    if (ts->event_wq)
        destroy_workqueue(ts->event_wq);

#if NVT_TOUCH_ESD_PROTECT
    if (nvt_esd_check_wq) {
        cancel_delayed_work_sync(&nvt_esd_check_work);
        nvt_esd_check_enable(false);
        destroy_workqueue(nvt_esd_check_wq);
        nvt_esd_check_wq = NULL;
    }
#endif

#if BOOT_UPDATE_FIRMWARE
    if (nvt_fwu_wq) {
        destroy_workqueue(nvt_fwu_wq);
        nvt_fwu_wq = NULL;
    }
#endif

#if WAKEUP_GESTURE
    device_init_wakeup(&ts->input_dev->dev, 0);
#endif

    nvt_irq_enable(false);
    free_irq(client->irq, ts);

    mutex_destroy(&ts->xbuf_lock);
    mutex_destroy(&ts->lock);

    nvt_gpio_deconfig(ts);

    if (ts->pen_support && ts->pen_input_dev) {
        input_unregister_device(ts->pen_input_dev);
        ts->pen_input_dev = NULL;
    }

    if (ts->input_dev) {
        input_unregister_device(ts->input_dev);
        ts->input_dev = NULL;
    }

    spi_set_drvdata(client, NULL);

    kfree(ts->xbuf);
    kfree(ts->rbuf);
    kfree(ts);
    ts = NULL;

    return 0;
}

/* Shutdown 函数 */
static void nvt_ts_shutdown(struct spi_device *client)
{
    NVT_LOG("Shutdown driver...\n");
    nvt_irq_enable(false);

    if (ts->power_supply_notifier.notifier_call) {
        power_supply_unreg_notifier(&ts->power_supply_notifier);
        ts->power_supply_notifier.notifier_call = NULL;
    }

#if defined(CONFIG_DRM_MSM)
    if (msm_drm_unregister_client(&ts->drm_notif))
        NVT_ERR("Error occurred while unregistering drm_notifier.\n");
#endif

#if NVT_TOUCH_MP
    nvt_mp_proc_deinit();
#endif
#if NVT_TOUCH_EXT_PROC
    nvt_extra_proc_deinit();
#endif
#if NVT_TOUCH_PROC
    nvt_flash_proc_deinit();
#endif

    mutex_destroy(&ts->power_supply_lock);
    if (ts->event_wq)
        destroy_workqueue(ts->event_wq);

#if NVT_TOUCH_ESD_PROTECT
    if (nvt_esd_check_wq) {
        cancel_delayed_work_sync(&nvt_esd_check_work);
        nvt_esd_check_enable(false);
        destroy_workqueue(nvt_esd_check_wq);
        nvt_esd_check_wq = NULL;
    }
#endif

#if BOOT_UPDATE_FIRMWARE
    if (nvt_fwu_wq) {
        destroy_workqueue(nvt_fwu_wq);
        nvt_fwu_wq = NULL;
    }
#endif
}

/* SPI 驱动结构体 */
static const struct spi_device_id nvt_ts_id[] = {
    { NVT_SPI_NAME, 0 },
    { }
};

#ifdef CONFIG_OF
static struct of_device_id nvt_match_table[] = {
    { .compatible = "novatek,NVT-ts-spi",},
    { },
};
#endif

#ifdef CONFIG_PM
static int nvt_pm_suspend(struct device *dev)
{
    NVT_LOG("system enters into pm_suspend");
    if (device_may_wakeup(dev) && ts->db_wakeup) {
        NVT_LOG("enable touch irq wake\n");
        enable_irq_wake(ts->client->irq);
    }
    ts->dev_pm_suspend = true;
    reinit_completion(&ts->dev_pm_suspend_completion);
    return 0;
}

static int nvt_pm_resume(struct device *dev)
{
    NVT_LOG("system resume from pm_suspend");
    if (device_may_wakeup(dev) && ts->db_wakeup) {
        NVT_LOG("disable touch irq wake\n");
        disable_irq_wake(ts->client->irq);
    }
    ts->dev_pm_suspend = false;
    complete(&ts->dev_pm_suspend_completion);
    return 0;
}

static const struct dev_pm_ops nvt_dev_pm_ops = {
    .suspend = nvt_pm_suspend,
    .resume = nvt_pm_resume,
};
#endif

static struct spi_driver nvt_spi_driver = {
    .probe = nvt_ts_probe,
    .remove = nvt_ts_remove,
    .shutdown = nvt_ts_shutdown,
    .id_table = nvt_ts_id,
    .driver = {
        .name = NVT_SPI_NAME,
        .owner = THIS_MODULE,
#ifdef CONFIG_OF
        .of_match_table = nvt_match_table,
#endif
#ifdef CONFIG_PM
        .pm = &nvt_dev_pm_ops,
#endif
    },
};

/* 充电器模式检测 */
static bool nvt_off_charger_mode(void)
{
    bool charger_mode = false;
    char *chose = strnstr(saved_command_line, "androidboot.mode=", strlen(saved_command_line));
    if (chose && !strncmp(chose + strlen("androidboot.mode="), "charger", 7))
        charger_mode = true;
    return charger_mode;
}

/* 模块初始化 */
static int32_t __init nvt_driver_init(void)
{
    NVT_LOG("start\n");

    if (nvt_off_charger_mode()) {
        NVT_LOG("off_charger states, %s exit", __func__);
        return 0;
    }

    return spi_register_driver(&nvt_spi_driver);
}

/* 模块退出 */
static void __exit nvt_driver_exit(void)
{
    spi_unregister_driver(&nvt_spi_driver);
}

module_init(nvt_driver_init);
module_exit(nvt_driver_exit);

MODULE_DESCRIPTION("Novatek Touchscreen Driver");
MODULE_LICENSE("GPL");