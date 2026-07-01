#include <linux/platform_data/ktz8866.h>

/* ===== 全局变量 ===== */
struct ktz8866 *bd_a = NULL;
struct ktz8866 *bd_b = NULL;
struct ktz8866_status ktz8866_status;
struct ktz8866_led g_ktz8866_led;

/* ===== 亮度映射表（基于实际日志反推） ===== */
/*
 * 从日志分析：
 * sys=100  -> bl=12   (写入寄存器 0x0c)
 * sys=500  -> bl=62   (写入寄存器 0x3e)
 * sys=1000 -> bl=125  (写入寄存器 0x7d)
 * sys=1500 -> bl=187  (写入寄存器 0xbb)
 * sys=2047 -> bl=255  (写入寄存器 0xff)
 *
 * 映射公式: bl = sys / 8 (线性)
 * 寄存器值 = bl & 0x7 (LSB), (bl >> 3) & 0xff (MSB)
 */
static int bl_level_remap[BL_LEVEL_MAX + 1];

static void init_bl_level_remap(void)
{
    int i;
    
    for (i = 0; i <= BL_LEVEL_MAX; i++) {
        /* 线性映射: sys/8，范围0-255 */
        bl_level_remap[i] = i >> 3;  /* 相当于 i / 8 */
        
        /* 保证最低亮度不为0（防止背光完全关闭） */
        if (i > 0 && bl_level_remap[i] == 0)
            bl_level_remap[i] = 1;
    }
    bl_level_remap[0] = 0;
    
    pr_info("ktz8866a: bl_level_remap initialized (linear: val = sys/8)\n");
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
        ktz8866_writes(bd_b, KTZ8866_DISP_BL_ENABLE, 0x4f);
    } else {
        ktz8866_writes(bd_b, KTZ8866_DISP_BL_ENABLE, 0x0f);
    }

    ktz8866_writes(bd_b, KTZ8866_DISP_BB_LSB, v[0]);
    ktz8866_writes(bd_b, KTZ8866_DISP_BB_MSB, v[1]);

    mutex_unlock(&g_ktz8866_led.lock);
}
EXPORT_SYMBOL(ktz8866b_sync_brightness);

/* ===== 背光更新 ===== */
static int ktz8866a_backlight_update_status(struct backlight_device *backlight)
{
    struct ktz8866 *bd = bl_get_data(backlight);
    int brightness = backlight->props.brightness;
    int mapped;
    u8 v[2];

    if (brightness < 0 || brightness > BL_LEVEL_MAX)
        return 0;

    mutex_lock(&g_ktz8866_led.lock);

    /* 映射亮度值 */
    mapped = bl_level_remap[brightness];

    dev_info(&bd->client->dev, "brightness=%d mapped=%d\n", brightness, mapped);

    /* ===== 控制A芯片 ===== */
    if (mapped > 0) {
        ktz8866_writes(bd, KTZ8866_DISP_BL_ENABLE, 0x4f);
    } else {
        ktz8866_writes(bd, KTZ8866_DISP_BL_ENABLE, 0x0f);
    }

    v[0] = mapped & 0x7;
    v[1] = (mapped >> 3) & 0xff;

    ktz8866_writes(bd, KTZ8866_DISP_BB_LSB, v[0]);
    ktz8866_writes(bd, KTZ8866_DISP_BB_MSB, v[1]);

    g_ktz8866_led.level = mapped;

    /* ===== 同步到B芯片 ===== */
    if (bd_b) {
        dev_info(&bd->client->dev, "Syncing to B chip\n");
        ktz8866b_sync_brightness(mapped);
    }

    mutex_unlock(&g_ktz8866_led.lock);
    return 0;
}

static int ktz8866a_backlight_get_brightness(struct backlight_device *backlight)
{
    struct ktz8866 *bd = bl_get_data(backlight);
    u8 lsb, msb;

    mutex_lock(&g_ktz8866_led.lock);
    ktz8866_reads(bd, KTZ8866_DISP_BB_LSB, &lsb);
    ktz8866_reads(bd, KTZ8866_DISP_BB_MSB, &msb);
    mutex_unlock(&g_ktz8866_led.lock);

    return (msb << 8) | lsb;
}

static const struct backlight_ops ktz8866a_backlight_ops = {
    .options = BL_CORE_SUSPENDRESUME,
    .update_status = ktz8866a_backlight_update_status,
    .get_brightness = ktz8866a_backlight_get_brightness,
};

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

/* ===== Probe ===== */
static int ktz8866a_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct backlight_properties props;
    struct ktz8866 *bd;
    struct backlight_device *bl;
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

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
        dev_err(&client->dev, "I2C doesn't support SMBUS_BYTE_DATA\n");
        return -EIO;
    }

    mutex_init(&g_ktz8866_led.lock);

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

    /* 注册背光设备 - 使用唯一名称，不冲突 */
    memset(&props, 0, sizeof(props));
    props.type = BACKLIGHT_RAW;
    props.max_brightness = BL_LEVEL_MAX;
    props.brightness = 98;

    bl = devm_backlight_device_register(&client->dev, "ktz8866a",
                                        &client->dev, bd,
                                        &ktz8866a_backlight_ops, &props);
    if (IS_ERR(bl)) {
        dev_err(&client->dev, "failed to register backlight\n");
        return PTR_ERR(bl);
    }

    bd->backlight = bl;
    i2c_set_clientdata(client, bl);

    bd_a = bd;
    ktz8866_status.ktz8866a_init = true;

    /* 检查B芯片是否已初始化 */
    if (ktz8866_status.ktz8866a_init && ktz8866_status.ktz8866b_init) {
        dev_info(&client->dev, "Both chips initialized\n");
    }

    dev_info(&client->dev, "KTZ8866A probed successfully\n");
    return 0;
}

static int ktz8866a_remove(struct i2c_client *client)
{
    struct backlight_device *bl = i2c_get_clientdata(client);
    bl->props.brightness = 0;
    backlight_update_status(bl);
    bd_a = NULL;
    ktz8866_status.ktz8866a_init = false;
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

MODULE_DESCRIPTION("KTZ8866A Backlight Driver");
MODULE_AUTHOR("Your Name");
MODULE_LICENSE("GPL");