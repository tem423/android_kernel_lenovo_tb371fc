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
#include <linux/device.h>
#include <linux/platform_device.h>

/* ===== 寄存器定义 ===== */
#define KTZ8866_REG_ENABLE  0x08
#define KTZ8866_REG_LSB     0x04
#define KTZ8866_REG_MSB     0x05
#define KTZ8866_REG_FLAGS   0x06

#define KTZ8866_BL_MAX      2047
#define KTZ8866_I2C_ADDR    0x11

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
    struct backlight_device *backlight;
    struct ktz8866_platform_data *pdata;
    struct mutex lock;
    int level;
};

struct ktz8866_status {
    bool ktz8866a_init;
    bool ktz8866b_init;
};

/* ===== 全局变量 ===== */
extern struct ktz8866 *bd_a;
extern struct ktz8866 *bd_b;
extern struct ktz8866_status ktz8866_status;
extern struct ktz8866_led g_ktz8866_led;

/* ===== 函数声明 ===== */
int ktz8866_reads(struct ktz8866 *bd, u8 reg, u8 *data);
int ktz8866_writes(struct ktz8866 *bd, u8 reg, u8 data);
void ktz8866b_sync_brightness(int brightness);

#endif