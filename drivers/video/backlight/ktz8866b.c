#include "ktz8866.h"

/* ===== 全局变量 ===== */
extern struct ktz8866 *g_ktz_b;

/* ===== B芯片Probe ===== */
static int ktz8866b_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct ktz8866 *ktz;

    dev_info(&client->dev, "KTZ8866B probing on %d-0x%02x\n",
             client->adapter->nr, client->addr);

    ktz = devm_kzalloc(&client->dev, sizeof(*ktz), GFP_KERNEL);
    if (!ktz)
        return -ENOMEM;

    ktz->client = client;
    ktz->chip = KTZ8866_B;
    ktz->pdata = NULL;
    mutex_init(&ktz->lock);

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
        dev_err(&client->dev, "I2C doesn't support SMBUS_BYTE_DATA\n");
        return -EIO;
    }

    g_ktz_b = ktz;
    i2c_set_clientdata(client, ktz);

    dev_info(&client->dev, "KTZ8866B probed successfully (slave)\n");
    return 0;
}

static int ktz8866b_remove(struct i2c_client *client)
{
    g_ktz_b = NULL;
    return 0;
}

static const struct i2c_device_id ktz8866b_ids[] = {
    { "ktz8866b", KTZ8866_B },
    { },
};
MODULE_DEVICE_TABLE(i2c, ktz8866b_ids);

static const struct of_device_id ktz8866b_match_table[] = {
    { .compatible = "ktz,ktz8866b" },
    { },
};
MODULE_DEVICE_TABLE(of, ktz8866b_match_table);

static struct i2c_driver ktz8866b_driver = {
    .driver = {
        .name = "ktz8866b",
        .owner = THIS_MODULE,
        .of_match_table = ktz8866b_match_table,
    },
    .probe = ktz8866b_probe,
    .remove = ktz8866b_remove,
    .id_table = ktz8866b_ids,
};

module_i2c_driver(ktz8866b_driver);

MODULE_DESCRIPTION("KTZ8866B Backlight Driver (Slave)");
MODULE_AUTHOR("Your Name");
MODULE_LICENSE("GPL");