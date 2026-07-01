/*
 * KTZ8866A Driver - BIAS + Backlight
 * compatible: "ktz,ktz8866a"
 * I2C bus 2, address 0x11
 */

#include <linux/platform_data/ktz8866.h>

/* ===== 全局变量 ===== */
static struct ktz8866 *g_ktz_a = NULL;
static struct ktz8866 *g_ktz_b = NULL;  /* 引用B芯片，用于同步 */

/* ===== I2C写操作 ===== */
int ktz8866_write_byte(struct i2c_client *client, u8 reg, u8 value)
{
    u8 write_data[2] = {reg, value};
    int ret;

    if (!client)
        return -EINVAL;

    ret = i2c_master_send(client, write_data, 2);
    if (ret < 0)
        pr_err("[KTZ8866] write 0x%02x=0x%02x failed: %d\n", reg, value, ret);
    return ret;
}
EXPORT_SYMBOL(ktz8866_write_byte);

/* ===== I2C读操作 ===== */
int ktz8866_read_byte(struct i2c_client *client, u8 reg, u8 *value)
{
    u8 buffer = reg;
    int ret;

    if (!client || !value)
        return -EINVAL;

    ret = i2c_master_send(client, &buffer, 1);
    if (ret < 0)
        return ret;

    ret = i2c_master_recv(client, value, 1);
    if (ret < 0)
        return ret;

    return 0;
}
EXPORT_SYMBOL(ktz8866_read_byte);

/* ===== 设置亮度（同时控制A和B） ===== */
void ktz8866_set_brightness(struct ktz8866 *dev, int brightness)
{
    u8 lsb, msb;

    if (!dev)
        return;

    if (brightness < 0)
        brightness = 0;
    if (brightness > KTZ8866_BL_MAX)
        brightness = KTZ8866_BL_MAX;

    lsb = 0x07;
    msb = brightness & 0xFF;

    dev_info(&dev->client->dev, "brightness=%d (0x%02x, 0x%02x)\n",
             brightness, lsb, msb);

    mutex_lock(&dev->lock);

    /* 写自己 (A芯片) */
    ktz8866_write_byte(dev->client, KTZ8866_REG_LSB, lsb);
    ktz8866_write_byte(dev->client, KTZ8866_REG_MSB, msb);

    /* 同步写入B芯片 */
    if (g_ktz_b && g_ktz_b != dev) {
        mutex_lock(&g_ktz_b->lock);
        ktz8866_write_byte(g_ktz_b->client, KTZ8866_REG_LSB, lsb);
        ktz8866_write_byte(g_ktz_b->client, KTZ8866_REG_MSB, msb);
        mutex_unlock(&g_ktz_b->lock);
        dev_info(&dev->client->dev, "synced to B chip\n");
    }

    dev->brightness = brightness;
    mutex_unlock(&dev->lock);
}
EXPORT_SYMBOL(ktz8866_set_brightness);

/* ===== 初始化BIAS偏压 ===== */
int ktz8866_init_bias(struct i2c_client *client)
{
    int ret;

    if (!client)
        return -EINVAL;

    dev_info(&client->dev, "Initializing BIAS\n");

    ret = ktz8866_write_byte(client, KTZ8866_REG_BOOST_CFG, 0x2E);
    if (ret < 0) return ret;
    ret = ktz8866_write_byte(client, KTZ8866_REG_OUTP_CFG, 0x24);
    if (ret < 0) return ret;
    ret = ktz8866_write_byte(client, KTZ8866_REG_OUTN_CFG, 0x24);
    if (ret < 0) return ret;
    ret = ktz8866_write_byte(client, KTZ8866_REG_CTRL, 0x99);
    if (ret < 0) return ret;

    dev_info(&client->dev, "BIAS init success\n");
    return 0;
}
EXPORT_SYMBOL(ktz8866_init_bias);

/* ===== 初始化背光 ===== */
int ktz8866_init_backlight(struct i2c_client *client)
{
    int ret;

    if (!client)
        return -EINVAL;

    dev_info(&client->dev, "Initializing Backlight\n");

    ret = ktz8866_write_byte(client, KTZ8866_REG_CFG1, 0xDA);
    if (ret < 0) return ret;
    ret = ktz8866_write_byte(client, KTZ8866_REG_OPTION2, 0x37);
    if (ret < 0) return ret;
    ret = ktz8866_write_byte(client, KTZ8866_REG_CURRENT, 0xA0);
    if (ret < 0) return ret;
    ret = ktz8866_write_byte(client, KTZ8866_REG_ENABLE, 0x4F);
    if (ret < 0) return ret;
    ret = ktz8866_write_byte(client, KTZ8866_REG_CFG2, 0xFD);
    if (ret < 0) return ret;

    dev_info(&client->dev, "Backlight init success\n");
    return 0;
}
EXPORT_SYMBOL(ktz8866_init_backlight);

/* ===== 解析设备树GPIO ===== */
static int ktz8866_parse_dt(struct device *dev, struct ktz8866_platform_data *pdata)
{
    struct device_node *np = dev->of_node;

    pdata->hw_en_gpio = of_get_named_gpio_flags(np, "ktz8866,hwen-gpio", 0, NULL);
    pdata->enp_gpio = of_get_named_gpio_flags(np, "ktz8866,enp-gpio", 0, NULL);
    pdata->enn_gpio = of_get_named_gpio_flags(np, "ktz8866,enn-gpio", 0, NULL);

    if (pdata->hw_en_gpio < 0 || pdata->enp_gpio < 0 || pdata->enn_gpio < 0) {
        dev_err(dev, "GPIO parse failed\n");
        return -EINVAL;
    }

    dev_info(dev, "GPIO: hwen=%d, enp=%d, enn=%d\n",
             pdata->hw_en_gpio, pdata->enp_gpio, pdata->enn_gpio);
    return 0;
}

/* ===== 申请GPIO ===== */
static int ktz8866_request_gpio(struct device *dev, struct ktz8866_platform_data *pdata)
{
    int ret;

    ret = devm_gpio_request_one(dev, pdata->hw_en_gpio,
                                GPIOF_DIR_OUT | GPIOF_INIT_HIGH, "KTZ8866_HW_EN");
    if (ret < 0) {
        dev_err(dev, "HW_EN GPIO request failed\n");
        return ret;
    }

    ret = devm_gpio_request_one(dev, pdata->enp_gpio,
                                GPIOF_DIR_OUT | GPIOF_INIT_HIGH, "KTZ8866_ENP");
    if (ret < 0) {
        dev_err(dev, "ENP GPIO request failed\n");
        return ret;
    }

    ret = devm_gpio_request_one(dev, pdata->enn_gpio,
                                GPIOF_DIR_OUT | GPIOF_INIT_LOW, "KTZ8866_ENN");
    if (ret < 0) {
        dev_err(dev, "ENN GPIO request failed\n");
        return ret;
    }

    dev_info(dev, "GPIO requested successfully\n");
    return 0;
}

/* ===== 设置GPIO状态 ===== */
static void ktz8866_set_gpio(struct ktz8866_platform_data *pdata, bool enable)
{
    if (!pdata)
        return;

    gpio_set_value(pdata->hw_en_gpio, enable ? 1 : 0);
    gpio_set_value(pdata->enp_gpio, enable ? 1 : 0);
    gpio_set_value(pdata->enn_gpio, enable ? 0 : 1);

    pr_info("[KTZ8866] GPIO: enable=%d\n", enable);
}

/* ===== Probe ===== */
static int ktz8866a_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct ktz8866 *ktz;
    struct ktz8866_platform_data *pdata;
    int ret;

    dev_info(&client->dev, "KTZ8866A probing on bus %d, addr 0x%02x\n",
             client->adapter->nr, client->addr);

    /* ===== 删除cmdline判断 ===== */
    /* 原厂有: if(strstr(saved_command_line,"backlightktz=4")...) 已删除 */

    ktz = devm_kzalloc(&client->dev, sizeof(*ktz), GFP_KERNEL);
    if (!ktz)
        return -ENOMEM;

    pdata = devm_kzalloc(&client->dev, sizeof(*pdata), GFP_KERNEL);
    if (!pdata)
        return -ENOMEM;

    ktz->client = client;
    ktz->pdata = pdata;
    ktz->is_a = true;
    mutex_init(&ktz->lock);

    ret = ktz8866_parse_dt(&client->dev, pdata);
    if (ret < 0)
        return ret;

    ret = ktz8866_request_gpio(&client->dev, pdata);
    if (ret < 0)
        return ret;

    /* 使能GPIO */
    ktz8866_set_gpio(pdata, true);
    msleep(10);

    /* 初始化BIAS */
    ret = ktz8866_init_bias(client);
    if (ret < 0)
        return ret;

    /* 初始化背光 */
    ret = ktz8866_init_backlight(client);
    if (ret < 0)
        return ret;

    g_ktz_a = ktz;
    i2c_set_clientdata(client, ktz);

    /* 默认亮度100 */
    ktz8866_set_brightness(ktz, 100);

    dev_info(&client->dev, "KTZ8866A probed successfully\n");
    return 0;
}

static int ktz8866a_remove(struct i2c_client *client)
{
    struct ktz8866 *ktz = i2c_get_clientdata(client);

    if (ktz) {
        ktz8866_set_brightness(ktz, 0);
        g_ktz_a = NULL;
    }
    return 0;
}

static const struct i2c_device_id ktz8866a_ids[] = {
    { "ktz8866a", 0 },
    { },
};
MODULE_DEVICE_TABLE(i2c, ktz8866a_ids);

static const struct of_device_id ktz8866a_match[] = {
    { .compatible = "ktz,ktz8866a" },
    { },
};
MODULE_DEVICE_TABLE(of, ktz8866a_match);

static struct i2c_driver ktz8866a_driver = {
    .driver = {
        .name = "ktz8866a",
        .owner = THIS_MODULE,
        .of_match_table = ktz8866a_match,
    },
    .probe = ktz8866a_probe,
    .remove = ktz8866a_remove,
    .id_table = ktz8866a_ids,
};

module_i2c_driver(ktz8866a_driver);

MODULE_DESCRIPTION("KTZ8866A BIAS + Backlight Driver");
MODULE_AUTHOR("Your Name");
MODULE_LICENSE("GPL");