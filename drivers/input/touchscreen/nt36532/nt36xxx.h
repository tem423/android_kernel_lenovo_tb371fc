/*
 * Copyright (C) 2010 - 2022 Novatek, Inc.
 *
 * $Revision: 103375 $
 * $Date: 2022-07-29 10:34:16 +0800 (週五, 29 七月 2022) $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef         _LINUX_NVT_TOUCH_H
#define                _LINUX_NVT_TOUCH_H

#include <linux/delay.h>
#include <linux/input.h>
#include <linux/of.h>
#include <linux/spi/spi.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/power_supply.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include "nt36xxx_mem_map.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
#define HAVE_PROC_OPS
#endif

/* 移除 MTK 相关配置，只保留高通平台需要的 */
#ifdef CONFIG_SPI_MSM
/* 高通平台 SPI 配置 */
#endif

#define NVT_DEBUG 1

/* GPIO 定义 - 从 dts 读取 */
#define NVTTOUCH_RST_PIN 0
#define NVTTOUCH_INT_PIN 0

#define NVT_LOCKDOWN_SIZE 8
#define PINCTRL_STATE_ACTIVE                "pmx_ts_active"
#define PINCTRL_STATE_SUSPEND                "pmx_ts_suspend"

/* INT 触发模式 */
#define INT_TRIGGER_TYPE IRQ_TYPE_EDGE_RISING

/* 总线传输长度 */
#define BUS_TRANSFER_LENGTH  256

/* SPI 驱动信息 */
#define NVT_SPI_NAME "NVT-ts"

#if NVT_DEBUG
#define NVT_LOG(fmt, args...)    pr_err("[%s] %s %d: " fmt, NVT_SPI_NAME, __func__, __LINE__, ##args)
#else
#define NVT_LOG(fmt, args...)    pr_info("[%s] %s %d: " fmt, NVT_SPI_NAME, __func__, __LINE__, ##args)
#endif
#define NVT_ERR(fmt, args...)    pr_err("[%s] %s %d: " fmt, NVT_SPI_NAME, __func__, __LINE__, ##args)

/* 输入设备信息 */
#define NVT_TS_NAME "NVTCapacitiveTouchScreen"
#define NVT_PEN_NAME "NVTCapacitivePen"

/* 触摸信息 */
#define TOUCH_DEFAULT_MAX_WIDTH 1840
#define TOUCH_DEFAULT_MAX_HEIGHT 2944
#define TOUCH_MAX_FINGER_NUM 10
#define TOUCH_KEY_NUM 0
#if TOUCH_KEY_NUM > 0
extern const uint16_t touch_key_array[TOUCH_KEY_NUM];
#endif
#define TOUCH_FORCE_NUM 1000

/* 笔数据长度 */
#define PEN_DATA_LEN 14

/* 手写笔参数 */
#define PEN_PRESSURE_MAX (4095)
#define PEN_DISTANCE_MAX (1)
#define PEN_TILT_MIN (-60)
#define PEN_TILT_MAX (60)

/* 硬件复位支持 */
#define NVT_TOUCH_SUPPORT_HW_RST 1

/* 功能开关 */
#define NVT_TOUCH_PROC 1
#define NVT_TOUCH_EXT_PROC 1
#define NVT_TOUCH_MP 1
#define NVT_SAVE_TEST_DATA_IN_FILE 0
#define MT_PROTOCOL_B 1
#define WAKEUP_GESTURE 1

#if WAKEUP_GESTURE
extern const uint16_t gesture_key_array[];
#endif

#define BOOT_UPDATE_FIRMWARE 1
/* 固件名称 */
#define BOOT_UPDATE_FIRMWARE_NAME "novatek_ts_tm_fw_6.bin"
#define MP_UPDATE_FIRMWARE_NAME   "novatek_ts_tm_mp_6.bin"
#define DEFAULT_DEBUG_FW_NAME     "novatek_ts_tm_fw.bin"
#define DEFAULT_DEBUG_MP_NAME     "novatek_ts_tm_mp.bin"

#define NVT_SUPER_RESOLUTION_N 10
#if NVT_SUPER_RESOLUTION_N
#define POINT_DATA_LEN 108
#else
#define POINT_DATA_LEN 65
#endif
#define POINT_DATA_CHECKSUM 1
#define POINT_DATA_CHECKSUM_LEN 65

/* ESD 保护 */
#define NVT_TOUCH_ESD_PROTECT 1
#define NVT_TOUCH_ESD_CHECK_PERIOD 1500
#define NVT_TOUCH_WDT_RECOVERY 1

#define CHECK_PEN_DATA_CHECKSUM 0

#if BOOT_UPDATE_FIRMWARE
#define SIZE_4KB 4096
#define FLASH_SECTOR_SIZE SIZE_4KB
#define FW_BIN_VER_OFFSET (fw_need_write_size - SIZE_4KB)
#define FW_BIN_VER_BAR_OFFSET (FW_BIN_VER_OFFSET + 1)
#define NVT_FLASH_END_FLAG_LEN 3
#define NVT_FLASH_END_FLAG_ADDR (fw_need_write_size - NVT_FLASH_END_FLAG_LEN)
#endif

enum nvt_ic_state {
        NVT_IC_SUSPEND_IN,
        NVT_IC_SUSPEND_OUT,
        NVT_IC_RESUME_IN,
        NVT_IC_RESUME_OUT,
        NVT_IC_INIT,
};

/* 配置信息结构体 */
struct nvt_config_info {
        u8 display_maker;
        const char *nvt_fw_name;
        const char *nvt_mp_name;
};

/* 主数据结构体 */
struct nvt_ts_data {
        struct spi_device *client;
        struct input_dev *input_dev;
        struct delayed_work nvt_fwu_work;
        struct mutex power_supply_lock;
        struct work_struct power_supply_work;
        struct notifier_block power_supply_notifier;
        struct notifier_block charger_notifier;
        struct work_struct charger_notify_work;
        int is_usb_exist;
        int db_wakeup;

        int ic_state;
        int gesture_command_delayed;
        bool dev_pm_suspend;
        struct completion dev_pm_suspend_completion;
        uint16_t addr;
        int8_t phys[32];

/* DRM notifier - 高通标准接口 */
#if defined(CONFIG_DRM_MSM)
        struct notifier_block drm_notif;
#elif defined(CONFIG_FB)
        struct notifier_block fb_notif;
#elif defined(CONFIG_HAS_EARLYSUSPEND)
        struct early_suspend early_suspend;
#endif

        uint32_t config_array_size;
        struct nvt_config_info *config_array;
        const char *fw_name;
        const char *mp_name;

        uint8_t fw_ver;
        uint8_t x_num;
        uint8_t y_num;
        uint16_t abs_x_max;
        uint16_t abs_y_max;
        uint8_t max_touch_num;
        uint8_t max_button_num;
        uint32_t int_trigger_type;

        int32_t irq_gpio;
        uint32_t irq_flags;
        int32_t reset_gpio;
        uint32_t reset_flags;
        struct mutex lock;
        const struct nvt_ts_mem_map *mmap;
        uint8_t hw_crc;
        uint8_t auto_copy;
        uint16_t nvt_pid;
        uint8_t *rbuf;
        uint8_t *xbuf;
        struct mutex xbuf_lock;
        bool irq_enabled;
        bool pen_support;
        bool stylus_resol_double;
        bool fw_debug;
        uint8_t x_gang_num;
        uint8_t y_gang_num;
        uint8_t debug_flag;
        struct input_dev *pen_input_dev;
        bool pen_input_dev_enable;
        int8_t pen_phys[32];
        int result_type;
        int panel_index;

#ifdef CONFIG_TOUCHSCREEN_NVT_DEBUG_FS
        struct dentry *debugfs;
#endif

        uint32_t chip_ver_trim_addr;
        uint32_t swrst_sif_addr;
        uint32_t crc_err_flag_addr;

        struct pinctrl *ts_pinctrl;
        struct pinctrl_state *pinctrl_state_active;
        struct pinctrl_state *pinctrl_state_suspend;
        struct workqueue_struct *event_wq;
        struct work_struct resume_work;
};

#if NVT_TOUCH_PROC
struct nvt_flash_data{
        rwlock_t lock;
};
#endif

typedef enum {
        RESET_STATE_INIT = 0xA0,
        RESET_STATE_REK,
        RESET_STATE_REK_FINISH,
        RESET_STATE_NORMAL_RUN,
        RESET_STATE_MAX  = 0xAF
} RST_COMPLETE_STATE;

typedef enum {
    EVENT_MAP_HOST_CMD                      = 0x50,
    EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE   = 0x51,
    EVENT_MAP_RESET_COMPLETE                = 0x60,
    EVENT_MAP_FWINFO                        = 0x78,
    EVENT_MAP_PROJECTID                     = 0x9A,
} SPI_EVENT_MAP;

/* SPI 读写掩码 */
#define SPI_WRITE_MASK(a)        (a | 0x80)
#define SPI_READ_MASK(a)         (a & 0x7F)

#define DUMMY_BYTES (1)
#define NVT_TRANSFER_LEN        (63*1024)
#define NVT_READ_LEN            (2*1024)
#define NVT_XBUF_LEN            (NVT_TRANSFER_LEN + 1 + DUMMY_BYTES)

typedef enum {
        NVTWRITE = 0,
        NVTREAD  = 1
} NVT_SPI_RW;

/* 外部结构和函数声明 */
extern struct nvt_ts_data *ts;

int32_t CTP_SPI_READ(struct spi_device *client, uint8_t *buf, uint16_t len);
int32_t CTP_SPI_WRITE(struct spi_device *client, uint8_t *buf, uint16_t len);
void nvt_bootloader_reset(void);
void nvt_eng_reset(void);
void nvt_sw_reset(void);
void nvt_sw_reset_idle(void);
void nvt_boot_ready(void);
void nvt_fw_crc_enable(void);
void nvt_tx_auto_copy_mode(void);
void nvt_read_fw_history(uint32_t fw_history_addr);
int32_t nvt_update_firmware(const char *firmware_name);
int32_t nvt_check_fw_reset_state(RST_COMPLETE_STATE check_reset_state);
int32_t nvt_get_fw_info(void);
int32_t nvt_clear_fw_status(void);
int32_t nvt_check_fw_status(void);
int32_t nvt_set_page(uint32_t addr);
int32_t nvt_wait_auto_copy(void);
int32_t nvt_write_addr(uint32_t addr, uint8_t data);
bool nvt_get_dbgfw_status(void);
void nvt_set_dbgfw_status(bool enable);

#if NVT_TOUCH_ESD_PROTECT
extern void nvt_esd_check_enable(uint8_t enable);
#endif

#endif /* _LINUX_NVT_TOUCH_H */