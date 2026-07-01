#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/err.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/debugfs.h>
#include <linux/bitops.h>
#include <linux/math64.h>
#include <v1/mtk_charger.h>
#include <v1/chagre_pump_manage.h>

static const struct of_device_id cust_charge_pump_of_match[];


static int cust_charge_pump_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	int ret = 0;

	ret = sgm41600_hw_chipid_detect(client);
	if(!ret) {
		pr_notice("loading sgm41600 driver...\n");
		ret = sgm41600_probe(client, id);
		if(!ret) {
			hardinfo_set_vendor_chginfo(HRADINFO_CHG_PUMP_INFO, SGM41600);
			return ret;
		}
	}

	ret = sc854x_hw_chipid_detect(client);
	if(!ret) {
		pr_notice("loading sc854x driver...\n");
		ret = sc854x_charger_probe(client, id);
		if(!ret) {
			hardinfo_set_vendor_chginfo(HRADINFO_CHG_PUMP_INFO, SC8549);
			return ret;
		}
	}

	pr_notice("not loading success charge pump\n");

	return ret;
}

static int cust_charge_pump_suspend_noirq(struct device *dev)
{
	int id = -1;

	id = hardinfo_get_vendor_chginfo(HRADINFO_CHG_PUMP_INFO);
	switch (id)
	{
	case SGM41600:
		sgm41600_suspend_noirq(dev);
		break;
	case SC8549:
		sc854x_suspend_noirq(dev);
		break;
	default:
		pr_notice("%s\n", __func__);
		break;
	}

	return 0;
}

static int cust_charge_pump_suspend(struct device *dev)
{

	int id = -1;

	id = hardinfo_get_vendor_chginfo(HRADINFO_CHG_PUMP_INFO);

	switch (id)
	{
	case SGM41600:
		sgm41600_suspend(dev);
		break;
	case SC8549:
		sc854x_suspend(dev);
		break;
	default:
		pr_notice("%s\n", __func__);
		break;
	}

	return 0;
}

static int cust_charge_pump_resume(struct device *dev)
{
	int id = -1;

	id = hardinfo_get_vendor_chginfo(HRADINFO_CHG_PUMP_INFO);
	switch (id)
	{
	case SGM41600:
		sgm41600_resume(dev);
		break;
	case SC8549:
		sc854x_resume(dev);
		break;
	default:
		pr_notice("%s\n", __func__);
		break;
	}

	return 0;
}

static int cust_charge_pump_remove(struct i2c_client *client)
{
	int id = -1;

	id = hardinfo_get_vendor_chginfo(HRADINFO_CHG_PUMP_INFO);
	switch (id)
	{
	case SGM41600:
		sgm41600_charger_remove(client);
		break;
	case SC8549:
		sc854x_charger_remove(client);
		break;
	default:
		pr_notice("%s\n", __func__);
		break;
	}

	return 0;
}

static void cust_charge_pump_shutdown(struct i2c_client *client)
{
	int id = -1;

	id = hardinfo_get_vendor_chginfo(HRADINFO_CHG_PUMP_INFO);
	switch (id)
	{
	case SGM41600:
		sgm41600_charger_shutdown(client);
		break;
	case SC8549:
		sc854x_charger_shutdown(client);
		break;
	default:
		pr_notice("%s\n", __func__);
		break;
	}

}

static const struct dev_pm_ops cust_charge_pump_ops = {
	.resume        = cust_charge_pump_resume,
	.suspend_noirq = cust_charge_pump_suspend_noirq,
	.suspend       = cust_charge_pump_suspend,
};

static const struct of_device_id cust_charge_pump_of_match[] = {
	{
		.compatible = "hq,chgpump-standalone",
	},
	{},
};

static struct i2c_driver chg_pump_driver = {
	.driver = {
		.name = "charge-pump-manage",
		.owner = THIS_MODULE,
		.of_match_table = cust_charge_pump_of_match,
		.pm = &cust_charge_pump_ops,
	},
	.probe = cust_charge_pump_probe,
	.remove = cust_charge_pump_remove,
	.shutdown = cust_charge_pump_shutdown,
};
module_i2c_driver(chg_pump_driver);

