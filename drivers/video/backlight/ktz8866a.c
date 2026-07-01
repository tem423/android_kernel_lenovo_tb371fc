#include <linux/platform_data/ktz8866.h>

/* ===== 全局变量定义 ===== */
struct ktz8866 *bd_a = NULL;
struct ktz8866 *bd_b = NULL;
struct ktz8866_status ktz8866_status;
struct ktz8866_led g_ktz8866_led;  /* 现在结构体已完整定义 */

static struct backlight_ops *g_orig_ops = NULL;
static struct backlight_device *g_panel_bd = NULL;
static bool g_hooked = false;

/* ===== 亮度映射表 ===== */
static int bl_level_remap[KTZ8866_BL_MAX + 1];

static void init_bl_level_remap(void)
{
    int i;
    for (i = 0; i <= KTZ8866_BL_MAX; i++) {
        bl_level_remap[i] = i;
    }
    bl_level_remap[0] = 0;
    pr_info("ktz8866a: bl_level_remap initialized (0-2047)\n");
}

/* ===== I2C操作 ===== */
int ktz8866_reads(struct ktz8866 *bd, u8 reg, u8 *data)
{
    int ret = i2c_smbus_read_byte_data(bd->client, reg);
    if (ret < 0) {
        dev_err(&bd->client->dev, "read 0x%02x failed: %d\n", reg, ret);
        return ret;
    }
    *data = (uint8_t)ret;
    return 0;
}
EXPORT_SYMBOL(ktz8866_reads);

int ktz8866_writes(struct ktz8866 *bd, u8 reg, u8 data)
{
    int ret = i2c_smbus_write_byte_data(bd->client, reg, data);
    if (ret < 0)
        dev_err(&bd->client->dev, "write 0x%02x=0x%02x failed: %d\n", reg, data, ret);
    return ret;
}
EXPORT_SYMBOL(ktz8866_writes);

/* ===== B芯片同步 ===== */
void ktz8866b_sync_brightness(int brightness)
{
    u8 v[2];

    if (!bd_b) {
        pr_warn("ktz8866b: not available\n");
        return;
    }

    v[0] = brightness & 0x7;
    v[1] = (brightness >> 3) & 0xff;

    mutex_lock(&g_ktz8866_led.lock);

    dev_info(&bd_b->client->dev, "sync brightness=%d (0x%02x, 0x%02x)\n",
             brightness, v[0], v[1]);

    if (brightness > 0) {
        ktz8866_writes(bd_b, KTZ8866_REG_ENABLE, 0x4f);
    } else {
        ktz8866_writes(bd_b, KTZ8866_REG_ENABLE, 0x0f);
    }

    ktz8866_writes(bd_b, KTZ8866_REG_LSB, v[0]);
    ktz8866_writes(bd_b, KTZ8866_REG_MSB, v[1]);

    mutex_unlock(&g_ktz8866_led.lock);
}
EXPORT_SYMBOL(ktz8866b_sync_brightness);

/* ===== 控制A芯片亮度 ===== */
static void ktz8866a_set_brightness(struct ktz8866 *bd, int brightness)
{
    u8 v[2];
    int mapped;

    if (!bd)
        return;

    mapped = bl_level_remap[brightness];
    v[0] = mapped & 0x7;
    v[1] = (mapped >> 3) & 0xff;

    dev_info(&bd->client->dev, "brightness=%d mapped=%d (0x%02x, 0x%02x)\n",
             brightness, mapped, v[0], v[1]);

    if (mapped > 0) {
        ktz8866_writes(bd, KTZ8866_REG_ENABLE, 0x4f);
    } else {
        ktz8866_writes(bd, KTZ8866_REG_ENABLE, 0x0f);
    }

    ktz8866_writes(bd, KTZ8866_REG_LSB, v[0]);
    ktz8866_writes(bd, KTZ8866_REG_MSB, v[1]);

    bd->level = mapped;
}

/* ===== Hook的update_status ===== */
static int ktz8866_hooked_update_status(struct backlight_device *bd)
{
    int brightness = bd->props.brightness;
    int ret = 0;

    if (!bd_a) {
        pr_err("ktz8866a: bd_a is NULL\n");
        return -EINVAL;
    }

    dev_info(&bd_a->client->dev, "hooked: brightness=%d\n", brightness);

    mutex_lock(&g_ktz8866_led.lock);

    /* 控制A芯片 */
    ktz8866a_set_brightness(bd_a, brightness);

    /* 同步到B芯片 */
    if (bd_b) {
        dev_info(&bd_a->client->dev, "syncing to B chip\n");
        ktz8866b_sync_brightness(brightness);
    }

    mutex_unlock(&g_ktz8866_led.lock);

    /* 调用原始update_status（保持WLED/DCS功能） */
    if (g_orig_ops && g_orig_ops->update_status) {
        ret = g_orig_ops->update_status(bd);
    }

    return ret;
}

/* ===== 查找backlight设备的回调 ===== */
static int backlight_dev_match_name(struct device *dev, const void *data)
{
    return !strcmp(dev_name(dev), (const char *)data);
}

/* ===== Hook panel0-backlight ===== */
static int ktz8866_hook_panel_backlight(void)
{
    struct device *panel_dev;
    struct backlight_device *bd;
    struct backlight_ops *new_ops;
    int ret = 0;

    extern struct class backlight_class;

    if (!bd_a) {
        pr_err("ktz8866a: bd_a not initialized\n");
        return -EINVAL;
    }

    dev_info(&bd_a->client->dev, "hooking panel0-backlight\n");

    /* 查找 panel0-backlight */
    panel_dev = class_find_device(&backlight_class, NULL, "panel0-backlight",
                                  backlight_dev_match_name);
    if (!panel_dev) {
        dev_err(&bd_a->client->dev, "panel0-backlight not found\n");
        return -ENODEV;
    }

    bd = to_backlight_device(panel_dev);
    g_panel_bd = bd;

    /* 保存原始ops */
    g_orig_ops = kmalloc(sizeof(*g_orig_ops), GFP_KERNEL);
    if (!g_orig_ops) {
        ret = -ENOMEM;
        goto out_put;
    }
    memcpy(g_orig_ops, bd->ops, sizeof(*g_orig_ops));

    /* 创建新ops */
    new_ops = kmalloc(sizeof(*new_ops), GFP_KERNEL);
    if (!new_ops) {
        ret = -ENOMEM;
        goto out_free_orig;
    }
    memcpy(new_ops, bd->ops, sizeof(*new_ops));

    /* 替换update_status */
    new_ops->update_status = ktz8866_hooked_update_status;
    bd->ops = new_ops;
    g_hooked = true;

    dev_info(&bd_a->client->dev, "panel0-backlight hooked successfully\n");

    put_device(panel_dev);
    return 0;

out_free_orig:
    kfree(g_orig_ops);
    g_orig_ops = NULL;
out_put:
    put_device(panel_dev);
    return ret;
}

/* ===== 解析设备树 ===== */
static int parse_dt(struct device *dev, struct ktz8866_platform_data *pdata)
{
    struct device_node *np = dev->of_node;
    pdata->hw_en_gpio = of_get_named_gpio_flags(np, "ktz8866,hwen-gpio", 0, NULL);

    if (pdata->hw_en_gpio < 0) {
        dev_err(dev, "failed to parse hwen-gpio\n");
        return -EINVAL;
    }

    dev_info(dev, "GPIO: hwen=%d\n", pdata->hw_en_gpio);
    return 0;
}

/* ===== A芯片Probe ===== */
static int ktz8866a_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct ktz8866 *bd;
    int ret;
    static bool remap_initialized = false;

    dev_info(&client->dev, "KTZ8866A probing on %d-0x%02x\n",
             client->adapter->nr, client->addr);

    bd = devm_kzalloc(&client->dev, sizeof(*bd), GFP_KERNEL);
    if (!bd)
        return -ENOMEM;

    bd->pdata = devm_kzalloc(&client->dev, sizeof(*bd->pdata), GFP_KERNEL);
    if (!bd->pdata)
        return -ENOMEM;

    bd->client = client;
    bd->chip = KTZ8866_A;
    mutex_init(&bd->lock);

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
        dev_err(&client->dev, "I2C doesn't support SMBUS_BYTE_DATA\n");
        return -EIO;
    }

    if (!remap_initialized) {
        init_bl_level_remap();
        remap_initialized = true;
    }

    ret = parse_dt(&client->dev, bd->pdata);
    if (ret < 0)
        return ret;

    /* A芯片申请GPIO */
    ret = devm_gpio_request_one(&client->dev, bd->pdata->hw_en_gpio,
                                GPIOF_DIR_OUT | GPIOF_INIT_HIGH, "KTZ8866_HW_EN");
    if (ret < 0) {
        dev_err(&client->dev, "GPIO request failed: %d\n", bd->pdata->hw_en_gpio);
        return ret;
    }

    bd_a = bd;
    i2c_set_clientdata(client, bd);
    ktz8866_status.ktz8866a_init = true;

    /* 检查B芯片是否已初始化 */
    if (ktz8866_status.ktz8866a_init && ktz8866_status.ktz8866b_init) {
        dev_info(&client->dev, "Both chips initialized\n");
    }

    dev_info(&client->dev, "KTZ8866A probed successfully\n");
    return 0;
}

/* ===== A芯片Remove ===== */
static int ktz8866a_remove(struct i2c_client *client)
{
    /* 恢复原始ops */
    if (g_hooked && g_panel_bd && g_orig_ops) {
        g_panel_bd->ops = g_orig_ops;
        g_hooked = false;
    }

    kfree(g_orig_ops);
    g_orig_ops = NULL;
    g_panel_bd = NULL;

    bd_a = NULL;
    ktz8866_status.ktz8866a_init = false;
    return 0;
}

/* ===== I2C ID表 ===== */
static const struct i2c_device_id ktz8866a_ids[] = {
    { "ktz8866a", KTZ8866_A },
    { },
};
MODULE_DEVICE_TABLE(i2c, ktz8866a_ids);

/* ===== 设备树匹配表 ===== */
static const struct of_device_id ktz8866a_match_table[] = {
    { .compatible = "ktz,ktz8866a" },
    { },
};
MODULE_DEVICE_TABLE(of, ktz8866a_match_table);

/* ===== I2C驱动结构 ===== */
static struct i2c_driver ktz8866a_driver = {
    .driver = {
        .name = "ktz8866a",
        .owner = THIS_MODULE,
        .of_match_table = ktz8866a_match_table,
    },
    .probe = ktz8866a_probe,
    .remove = ktz8866a_remove,
    .id_table = ktz8866a_ids,
};

/* ===== 延迟Hook（等待B芯片加载） ===== */
static int __init ktz8866a_init(void)
{
    int ret;

    ret = i2c_add_driver(&ktz8866a_driver);
    if (ret)
        return ret;

    /* 延迟2秒后尝试hook，等待B芯片加载 */
    msleep(2000);
    ktz8866_hook_panel_backlight();

    return 0;
}

static void __exit ktz8866a_exit(void)
{
    i2c_del_driver(&ktz8866a_driver);
}

module_init(ktz8866a_init);
module_exit(ktz8866a_exit);

MODULE_DESCRIPTION("KTZ8866A Backlight Driver (Master + Hook)");
MODULE_AUTHOR("Your Name");
MODULE_LICENSE("GPL");