/*
 * KTZ8866B Driver - Backlight only
 * compatible: "ktz,ktz8866b"
 * I2C bus 4, address 0x11
 */

#include <linux/platform_data/ktz8866.h

/* ===== 全局变量 ===== */
extern struct ktz8866 *g_ktz_a;
static struct ktz8866 *g_ktz_b = NULL;

/* ===== Probe ===== */
static int ktz8866b_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct ktz8866 *ktz;
    int ret;

    dev_info(&client->dev, "KTZ8866B probing on bus %d, addr 0x%02x\n",
             client->adapter->nr, client->addr);

    /* ===== 删除cmdline判断 ===== */
    /* 原厂有: if(strstr(saved_command_line,"backlightktz=4")...) 已删除 */

    ktz = devm_kzalloc(&client->dev, sizeof(*ktz), GFP_KERNEL);
    if (!ktz)
        return -ENOMEM;

    ktz->client = client;
    ktz->pdata = NULL;  /* B芯片不申请GPIO */
    ktz->is_a = false;
    mutex_init(&ktz->lock);

    /* 初始化背光（不初始化BIAS） */
    ret = ktz8866_init_backlight(client);
    if (ret < 0)
        return ret;

    g_ktz_b = ktz;
    i2c_set_clientdata(client, ktz);

    dev_info(&client->dev, "KTZ8866B probed successfully (slave)\n");
    return 0;
}

static int ktz8866b_remove(struct i2c_client *client)
{
    struct ktz8866 *ktz = i2c_get_clientdata(client);

    if (ktz) {
        ktz8866_write_byte(client, KTZ8866_REG_ENABLE, 0x00);
        g_ktz_b = NULL;
    }
    return 0;
}

static const struct i2c_device_id ktz8866b_ids[] = {
    { "ktz8866b", 0 },
    { },
};
MODULE_DEVICE_TABLE(i2c, ktz8866b_ids);

static const struct of_device_id ktz8866b_match[] = {
    { .compatible = "ktz,ktz8866b" },
    { },
};
MODULE_DEVICE_TABLE(of, ktz8866b_match);

static struct i2c_driver ktz8866b_driver = {
    .driver = {
        .name = "ktz8866b",
        .owner = THIS_MODULE,
        .of_match_table = ktz8866b_match,
    },
    .probe = ktz8866b_probe,
    .remove = ktz8866b_remove,
    .id_table = ktz8866b_ids,
};

module_i2c_driver(ktz8866b_driver);

MODULE_DESCRIPTION("KTZ8866B Backlight Driver (Slave)");
MODULE_AUTHOR("Your Name");
MODULE_LICENSE("GPL");