#ifndef _KTZ8866_H
#define _KTZ8866_H

#include <linux/backlight.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/notifier.h>

/* ===== 寄存器定义（基于实际I2C扫描） ===== */
#define KTZ8866_DISP_BL_ENABLE  0x08    /* 使能寄存器: 0x4f=使能, 0x0f=禁用 */
#define KTZ8866_DISP_BB_LSB     0x04    /* 亮度低字节 (bit 0-2) */
#define KTZ8866_DISP_BB_MSB     0x05    /* 亮度高字节 (bit 3-10) */
#define KTZ8866_DISP_FLAGS      0x06    /* 状态寄存器 */

#define BL_LEVEL_MAX            2047    /* 最大亮度 (12位) */

/* ===== 芯片枚举 ===== */
enum {
    KTZ8866_A = 0,
    KTZ8866_B,
};

/* ===== 数据结构 ===== */
struct ktz8866_platform_data {
    int hw_en_gpio;
};

struct ktz8866 {
    u8 chip;
    struct i2c_client *client;
    struct notifier_block nb;
    struct ktz8866_platform_data *pdata;
    struct mutex lock;
    int current_brightness;
};

/* ===== 全局变量 ===== */
extern struct ktz8866 *g_ktz_a;
extern struct ktz8866 *g_ktz_b;

/* ===== 函数声明 ===== */
int ktz8866_reads(struct ktz8866 *bd, u8 reg, u8 *data);
int ktz8866_writes(struct ktz8866 *bd, u8 reg, u8 data);

#endif