#include "ktz8866.h"

/* ===== 全局变量 ===== */
struct ktz8866 *g_ktz_a = NULL;
struct ktz8866 *g_ktz_b = NULL;

/* ===== 亮度映射表（直接映射 0-2047） ===== */
static int bl_level_remap[BL_LEVEL_MAX + 1];

static void init_bl_level_remap(void)
{
    int i;
    for (i = 0; i <= BL_LEVEL_MAX; i++) {
        bl_level_remap[i] = i;  /* 直接映射，不右移 */
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

/* ===== 控制A和B芯片亮度 ===== */
static void ktz8866_set_brightness_locked(struct ktz8866 *bd, int brightness, bool sync_b)
{
    u8 v[2];
    int mapped;

    if (!bd)
        return;

    mapped = bl_level_remap[brightness];
    v[0] = mapped & 0x7;          /* LSB: bit 0-2 */
    v[1] = (mapped >> 3) & 0xff;  /* MSB: bit 3-10 */

    dev_info(&bd->client->dev, "brightness=%d mapped=%d (0x%02x, 0x%02x)\n",
             brightness, mapped, v[0], v[1]);

    /* 控制当前芯片 */
    if (mapped > 0) {
        ktz8866_writes(bd, KTZ8866_DISP_BL_ENABLE, 0x4f);
    } else {
        ktz8866_writes(bd, KTZ8866_DISP_BL_ENABLE, 0x0f);
    }
    ktz8866_writes(bd, KTZ8866_DISP_BB_LSB, v[0]);
    ktz8866_writes(bd, KTZ8866_DISP_BB_MSB, v[1]);

    bd->current_brightness = mapped;

    /* 同步到B芯片 */
    if (sync_b && g_ktz_b && bd != g_ktz_b) {
        struct ktz8866 *b_chip = g_ktz_b;
        mutex_lock(&b_chip->lock);
        dev_info(&b_chip->client->dev, "syncing brightness=%d\n", brightness);
        ktz8866_set_brightness_locked(b_chip, brightness, false);
        mutex_unlock(&b_chip->lock);
    }
}

/* ===== panel0-backlight 回调拦截 ===== */
static int ktz8866_backlight_notifier(struct notifier_block *nb,
                                      unsigned long action,
                                      void *data)
{
    struct backlight_device *bd = data;
    struct ktz8866 *ktz = container_of(nb, struct ktz8866, nb);
    int brightness;

    if (!bd || action != BACKLIGHT_UPDATE_STATUS)
        return NOTIFY_DONE;

    /* 只拦截 panel0-backlight */
    if (strcmp(bd->name, "panel0-backlight") != 0)
        return NOTIFY_DONE;

    brightness = bd->props.brightness;

    mutex_lock(&ktz->lock);

    dev_info(&ktz->client->dev, "intercept panel0-backlight: brightness=%d\n", brightness);

    /* 控制A芯片，同时同步B芯片 */
    ktz8866_set_brightness_locked(ktz, brightness, true);

    mutex_unlock(&ktz->lock);

    return NOTIFY_DONE;
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
    struct ktz8866 *ktz;
    int ret;
    static bool remap_initialized = false;

    dev_info(&client->dev, "KTZ8866A probing on %d-0x%02x\n",
             client->adapter->nr, client->addr);

    ktz = devm_kzalloc(&client->dev, sizeof(*ktz), GFP_KERNEL);
    if (!ktz)
        return -ENOMEM;

    ktz->pdata = devm_kzalloc(&client->dev, sizeof(*ktz->pdata), GFP_KERNEL);
    if (!ktz->pdata)
        return -ENOMEM;

    ktz->client = client;
    ktz->chip = KTZ8866_A;
    mutex_init(&ktz->lock);

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
        dev_err(&client->dev, "I2C doesn't support SMBUS_BYTE_DATA\n");
        return -EIO;
    }

    if (!remap_initialized) {
        init_bl_level_remap();
        remap_initialized = true;
    }

    ret = parse_dt(&client->dev, ktz->pdata);
    if (ret < 0)
        return ret;

    /* A芯片申请GPIO */
    ret = devm_gpio_request_one(&client->dev, ktz->pdata->hw_en_gpio,
                                GPIOF_DIR_OUT | GPIOF_INIT_HIGH, "KTZ8866_HW_EN");
    if (ret < 0) {
        dev_err(&client->dev, "GPIO request failed: %d\n", ktz->pdata->hw_en_gpio);
        return ret;
    }

    /* 注册notifier拦截panel0-backlight */
    ktz->nb.notifier_call = ktz8866_backlight_notifier;
    ret = backlight_register_notifier(&ktz->nb);
    if (ret < 0) {
        dev_err(&client->dev, "failed to register backlight notifier: %d\n", ret);
        return ret;
    }

    g_ktz_a = ktz;
    i2c_set_clientdata(client, ktz);

    dev_info(&client->dev, "KTZ8866A probed successfully (notifier mode)\n");
    return 0;
}

static int ktz8866a_remove(struct i2c_client *client)
{
    struct ktz8866 *ktz = i2c_get_clientdata(client);

    if (ktz) {
        backlight_unregister_notifier(&ktz->nb);
        g_ktz_a = NULL;
    }
    return 0;
}

static const struct i2c_device_id ktz8866a_ids[] = {
    { "ktz8866a", KTZ8866_A },
    { },
};
MODULE_DEVICE_TABLE(i2c, ktz8866a_ids);

static const struct of_device_id ktz8866a_match_table[] = {
    { .compatible = "ktz,ktz8866a" },
    { },
};
MODULE_DEVICE_TABLE(of, ktz8866a_match_table);

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

module_i2c_driver(ktz8866a_driver);

MODULE_DESCRIPTION("KTZ8866A Backlight Driver (Notifier Mode)");
MODULE_AUTHOR("Your Name");
MODULE_LICENSE("GPL");