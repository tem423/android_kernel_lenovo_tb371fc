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
#include <linux/platform_data/dualktz8866.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>

/* ===== 寄存器定义 ===== */
#define KTZ8866_DISP_BL_ENABLE  0x08    /* 使能寄存器 (0x4f=使能, 0x0f=禁用) */
#define KTZ8866_DISP_BB_LSB     0x04    /* 亮度低字节 */
#define KTZ8866_DISP_BB_MSB     0x05    /* 亮度高字节 */
#define KTZ8866_DISP_FLAGS      0x06    /* 状态寄存器 */
#define BL_LEVEL_MAX            2047    /* 最大亮度值 (12位) */

/* ===== 芯片枚举 ===== */
enum {
    KTZ8866_A = 0,
    KTZ8866_B,
};

/* ===== 平台数据结构 ===== */
struct ktz8866_platform_data {
    int hw_en_gpio;
    int enp_gpio;
    int enn_gpio;
};

/* ===== 芯片状态结构 ===== */
struct ktz8866_status {
    bool ktz8866a_init;
    bool ktz8866b_init;
};

/* ===== 芯片私有数据 ===== */
struct ktz8866 {
    u8 chip;
    struct i2c_client *client;
    struct backlight_device *backlight;
    struct ktz8866_platform_data *pdata;
};

/* ===== 全局变量声明 ===== */
extern struct ktz8866 *bd_a;
extern struct ktz8866 *bd_b;
extern struct ktz8866_status ktz8866_status;
extern struct ktz8866_led g_ktz8866_led;

/* ===== 公共I2C操作函数 ===== */
int ktz8866_reads(struct ktz8866 *bd, u8 reg, u8 *data);
int ktz8866_writes(struct ktz8866 *bd, u8 reg, u8 data);

/* ===== B芯片同步写入函数（由A芯片调用） ===== */
void ktz8866b_sync_write(u8 chip, u8 reg, u8 data);
void ktz8866b_sync_brightness(int brightness);

#endif /* _KTZ8866_H */