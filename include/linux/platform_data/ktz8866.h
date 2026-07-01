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
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/ktime.h>

/* ===== 寄存器定义 ===== */
#define KTZ8866_DISP_BL_ENABLE  0x08
#define KTZ8866_DISP_BB_LSB     0x04
#define KTZ8866_DISP_BB_MSB     0x05
#define KTZ8866_DISP_FLAGS      0x06

#define BL_LEVEL_MAX            2047

/* ===== 芯片枚举 ===== */
enum {
    KTZ8866_A = 0,
    KTZ8866_B,
};

/* ===== 数据结构 ===== */
struct ktz8866_platform_data {
    int hw_en_gpio;
};

struct ktz8866_status {
    bool ktz8866a_init;
    bool ktz8866b_init;
};

struct ktz8866 {
    u8 chip;
    struct i2c_client *client;
    struct backlight_device *backlight;
    struct ktz8866_platform_data *pdata;
};

struct pwm_reg {
    u8 lbyte;
    u8 hbyte;
};

struct ktz8866_led {
    struct mutex lock;
    int level;
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