#ifndef _KTZ8866_H
#define _KTZ8866_H

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/backlight.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>

/* ===== 寄存器定义 ===== */
#define KTZ8866_REG_CFG1        0x02
#define KTZ8866_REG_CFG2        0x03
#define KTZ8866_REG_LSB         0x04
#define KTZ8866_REG_MSB         0x05
#define KTZ8866_REG_ENABLE      0x08
#define KTZ8866_REG_BOOST_CFG   0x0C
#define KTZ8866_REG_OUTP_CFG    0x0D
#define KTZ8866_REG_OUTN_CFG    0x0E
#define KTZ8866_REG_CTRL        0x09
#define KTZ8866_REG_OPTION2     0x11
#define KTZ8866_REG_CURRENT     0x15

#define KTZ8866_BL_MAX          2047
#define KTZ8866_I2C_ADDR        0x11

/* ===== 数据结构 ===== */
struct ktz8866_platform_data {
    int hw_en_gpio;
    int enp_gpio;
    int enn_gpio;
};

struct ktz8866 {
    struct i2c_client *client;
    struct ktz8866_platform_data *pdata;
    struct mutex lock;
    int brightness;
    bool is_a;  /* true=A芯片, false=B芯片 */
};

/* ===== 函数声明 ===== */
int ktz8866_write_byte(struct i2c_client *client, u8 reg, u8 value);
int ktz8866_read_byte(struct i2c_client *client, u8 reg, u8 *value);
int ktz8866_init_backlight(struct i2c_client *client);
int ktz8866_init_bias(struct i2c_client *client);
void ktz8866_set_brightness(struct ktz8866 *dev, int brightness);

#endif