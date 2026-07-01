// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2018-2022, The Linux Foundation. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/delay.h>

#include <linux/pinctrl/consumer.h>
#include "was4780.h"
/*
 * use tcpc dev to detect audio plug in
 */
#include "mediatek/typec/tcpc/inc/tcpci_core.h"
#include "mediatek/typec/tcpc/inc/tcpm.h"

#define SWITCH_I2C_NAME	"typec-switch-driver"

enum was4780_register {
	WAS4780_SWITCH_SETTINGS	= 0x04,
	WAS4780_SWITCH_CONTROL	= 0x05,
	WAS4780_SWITCH_STATUS1	= 0x07,
	WAS4780_SLOW_L			= 0x08,
	WAS4780_SLOW_R			= 0x09,
	WAS4780_SLOW_MIC		= 0x0A,
	WAS4780_SLOW_SENSE		= 0x0B,
	WAS4780_SLOW_GND		= 0x0C,
	WAS4780_DELAY_L_R		= 0x0D,
	WAS4780_DELAY_L_MIC		= 0x0E,
	WAS4780_DELAY_L_SENSE	= 0x0F,
	WAS4780_DELAY_L_AGND	= 0x10,
	WAS4780_FUNC_EN			= 0x12,
	WAS4780_AJ_STS			= 0x17,
	WAS4780_DET_INT			= 0X18,
	WAS4780_RESET			= 0x1E,
};

enum DIO4480_RESGISTER {
	DIO4880_SWITCH_ENABLE	= 0x04,
	DIO4880_SWITCH_SELECT	= 0x05,
	DIO4480_SWITCH_RESET	= 0x1E,
};

enum SWITCH_VENDOR {
	INVALID_TYPE = 0,
	WAS4780,
	DIO4480,
	OCP96011,
	VENDOR_MAX
};

static const char * const usbc_switch_name[VENDOR_MAX] = {
	"invalid usbc type",
	"was4780",
	"DIO4480",
	"OCP96011",
};
static enum SWITCH_VENDOR usbc_supply;


struct was4780_priv {
	struct regmap *regmap;
	struct device *dev;
	struct tcpc_device *tcpc;
	struct notifier_block pd_nb;
	atomic_t usbc_mode;
	struct workqueue_struct *usbc_workqueue;
	struct work_struct usbc_analog_work;
	struct mutex usbc_lock;
	struct blocking_notifier_head was4780_notifier;
	unsigned int earpiece_type;
};

struct was4780_reg_val {
	u16 reg;
	u8 val;
};

static const struct regmap_config was4780_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = WAS4780_RESET,
};

static const struct was4780_reg_val was_reg_i2c_defaults[] = {
	{WAS4780_SWITCH_SETTINGS, 0x98},
	{WAS4780_SWITCH_CONTROL, 0x18},
	{WAS4780_SLOW_L, 0x00},
	{WAS4780_SLOW_R, 0x00},
	{WAS4780_SLOW_MIC, 0x00},
	{WAS4780_SLOW_SENSE, 0x00},
	{WAS4780_SLOW_GND, 0x00},
	{WAS4780_DELAY_L_R, 0x00},
	{WAS4780_DELAY_L_MIC, 0x00},
	{WAS4780_DELAY_L_SENSE, 0x00},
	{WAS4780_DELAY_L_AGND, 0x09},
	{WAS4780_FUNC_EN, 0x08},
};

static const struct was4780_reg_val dio_reg_i2c_defaults[] = {
	{DIO4880_SWITCH_ENABLE, 0x98},
	{DIO4880_SWITCH_SELECT, 0x18},
	{DIO4480_SWITCH_RESET, 0x01},
};


static void was4780_usbc_update_settings(struct was4780_priv *was_priv,
		u32 switch_control, u32 switch_enable)
{
	if (!was_priv) {
		pr_info("%s: invalid was_priv!!", __func__);
		return;
	}
	if (!was_priv->regmap) {
		dev_err(was_priv->dev, "%s: regmap invalid\n", __func__);
		return;
	}

	regmap_write(was_priv->regmap, WAS4780_SWITCH_SETTINGS, 0x80);

	regmap_write(was_priv->regmap, WAS4780_SWITCH_CONTROL, switch_control);
	/* was4780 chip hardware requirement */
	usleep_range(50, 55);
	regmap_write(was_priv->regmap, WAS4780_SWITCH_SETTINGS, switch_enable);
}

static int was4780_usbc_event_changed(struct notifier_block *nb,
				      unsigned long evt, void *ptr)
{
	struct tcp_notify *noti = ptr;
	struct was4780_priv *was_priv = container_of(nb, struct was4780_priv, pd_nb);

	if (!was_priv) {
		pr_info("%s: invalid was_priv!!", __func__);
		return NOTIFY_DONE;
	}

	if (noti == NULL) {
		dev_err(was_priv->dev, "%s: data is NULL. \n", __func__);
		return NOTIFY_DONE;
	}

	switch (evt) {
	case TCP_NOTIFY_TYPEC_STATE:
		if (noti->typec_state.old_state == TYPEC_UNATTACHED &&
			noti->typec_state.new_state == TYPEC_ATTACHED_AUDIO) {
			/* Audio Plug in */
			dev_err(was_priv->dev, "%s: Audio Plug In \n", __func__);
			atomic_set(&(was_priv->usbc_mode), 1);

			queue_work(was_priv->usbc_workqueue, &was_priv->usbc_analog_work);

		} else if (noti->typec_state.old_state == TYPEC_ATTACHED_AUDIO &&
			noti->typec_state.new_state == TYPEC_UNATTACHED) {
			/* Audio Plug out */
			dev_err(was_priv->dev, "%s: Audio Plug Out \n", __func__);
			atomic_set(&(was_priv->usbc_mode), 0);
			queue_work(was_priv->usbc_workqueue, &was_priv->usbc_analog_work);
		} else if (noti->typec_state.old_state == TYPEC_UNATTACHED &&
			noti->typec_state.new_state != TYPEC_ATTACHED_AUDIO) {
			if (atomic_read(&(was_priv->usbc_mode)) != 0)
				was4780_usbc_update_settings(was_priv, 0x18, 0x98);
		}
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static int was4780_usbc_status_check(struct was4780_priv *priv)
{
	uint8_t state;

	if (priv == NULL || priv->tcpc == NULL) {
		pr_info("[%s] Failed to get tcpc device!", __func__);
		return -EINVAL;
	}

	state = tcpm_inquire_typec_attach_state(priv->tcpc);
	if (state == TYPEC_ATTACHED_AUDIO &&
			atomic_read(&(priv->usbc_mode)) != 1) {
		/*change to audio mode while audio Plug in*/
		dev_err(priv->dev, "[%s] audio plug in", __func__);
		atomic_set(&(priv->usbc_mode), 1);

		queue_work(priv->usbc_workqueue, &priv->usbc_analog_work);
	}

	return 0;
}

/*
 * was4780_reg_notifier - register notifier block with was driver
 *
 * @nb - notifier block of was4780
 * @node - phandle node to was4780 device
 *
 * Returns 0 on success, or error code
 */
int was4780_reg_notifier(struct notifier_block *nb,
			 struct device_node *node)
{
	int rc = 0;
	struct i2c_client *client;
	struct was4780_priv *was_priv = NULL;

	client = of_find_i2c_device_by_node(node);
	if (!client) {
		pr_info("%s, client is error", __func__);
		return -EPROBE_DEFER;
	}

	was_priv = (struct was4780_priv *)i2c_get_clientdata(client);
	if (!was_priv) {
		pr_info("failed to get data", __func__);
		return -EINVAL;
	}

	rc = blocking_notifier_chain_register(&was_priv->was4780_notifier, nb);
	if (rc) {
		dev_err(was_priv->dev, "failed to blocking_notifier_chain_register");
		return rc;
	}

	/*as part of the init sequence check if there is a connected headset*/
	was4780_usbc_status_check(was_priv);

	return rc;
}
EXPORT_SYMBOL(was4780_reg_notifier);

/*
 * was4780_unreg_notifier - unregister notifier block with was driver
 *
 * @nb - notifier block of was4780
 * @node - phandle node to was4780 device
 *
 * Returns 0 on pass, or error code
 */
int was4780_unreg_notifier(struct notifier_block *nb,
			     struct device_node *node)
{
	int rc = 0;
	struct i2c_client *client = of_find_i2c_device_by_node(node);
	struct was4780_priv *was_priv;
	struct device *dev;

	if (!client)
		return -EINVAL;

	was_priv = (struct was4780_priv *)i2c_get_clientdata(client);
	if (!was_priv)
		return -EINVAL;
	dev = was_priv->dev;
	if (!dev)
		return -EINVAL;

	rc = blocking_notifier_chain_unregister(&was_priv->was4780_notifier, nb);

	return rc;
}
EXPORT_SYMBOL(was4780_unreg_notifier);

static int was4780_validate_display_port_settings(struct was4780_priv *was_priv)
{
	u32 switch_status = 0;

	if (!was_priv) {
		pr_info("%s, was_priv is null!", __func__);
		return -EINVAL;
	}

	regmap_read(was_priv->regmap, WAS4780_SWITCH_STATUS1, &switch_status);

	if ((switch_status != 0x23) && (switch_status != 0x1C)) {
		pr_info("AUX SBU1/2 switch status is invalid = %u\n",
				switch_status);
		return -EIO;
	}

	return 0;
}
/*
 * was4780_switch_event - configure was switch position based on event
 *
 * @node - phandle node to was4780 device
 * @event - was_function enum
 *
 * Returns int on whether the switch happened or not.
 * return 0 on pass, <0 on fail.
 */
int was4780_switch_event(struct device_node *node,
			 enum was_function event, unsigned int param)
{
	int switch_control = 0;
	struct i2c_client *client = of_find_i2c_device_by_node(node);
	struct was4780_priv *was_priv;

	if (!client)
		return -EINVAL;

	was_priv = (struct was4780_priv *)i2c_get_clientdata(client);
	if (!was_priv)
		return -EINVAL;
	if (!was_priv->regmap)
		return -EINVAL;

	switch (event) {
	case WAS_MIC_GND_SWAP:
		regmap_read(was_priv->regmap, WAS4780_SWITCH_CONTROL,
				&switch_control);

		if ((switch_control & 0x07) == 0x07)
			switch_control = 0x0;
		else
			switch_control = 0x7;
		regmap_write(was_priv->regmap, DIO4880_SWITCH_SELECT, switch_control);
		/*Spruce code for OSPURCET-1315 by wangzr10 at 20230222 start*/
		if (usbc_supply == DIO4480)
			regmap_write(was_priv->regmap, DIO4880_SWITCH_ENABLE, 0x87);
		else if (usbc_supply == WAS4780)
		/*Spruce code for OSPURCET-1315 by wangzr10 at 20230222 end*/
			regmap_write(was_priv->regmap, DIO4880_SWITCH_ENABLE, 0x9f);
		break;
	case WAS_USBC_ORIENTATION_CC1:
		was4780_usbc_update_settings(was_priv, 0x18, 0xF8);
		return was4780_validate_display_port_settings(was_priv);
	case WAS_USBC_ORIENTATION_CC2:
		was4780_usbc_update_settings(was_priv, 0x78, 0xF8);
		return was4780_validate_display_port_settings(was_priv);
	case WAS_USBC_DISPLAYPORT_DISCONNECTED:
		was4780_usbc_update_settings(was_priv, 0x18, 0x98);
		break;
	case WAS_EARPIECE_TYPE:
		if (param < TYPE_MAX)
			was_priv->earpiece_type = param;
		/*Spruce code for OSPURCET-1315 by wangzr10 at 20230222 start*/
		if (usbc_supply == DIO4480) {
			usleep_range(10000,12000);
			regmap_write(was_priv->regmap, DIO4880_SWITCH_ENABLE, 0x9F);
		}
		/*Spruce code for OSPURCET-1315 by wangzr10 at 20230222 end*/
		break;
	default:
		dev_dbg(was_priv->dev, "%s:invalid event", __func__);
		break;
	}

	return 0;
}
EXPORT_SYMBOL(was4780_switch_event);

ssize_t sw_state_show(struct device *was_dev, struct device_attribute *attr,
		char *buf)
{
	struct was4780_priv *was_priv = dev_get_drvdata(was_dev);
	unsigned int set_value = 0;
	unsigned int ctl_value = 0;
	int ret = 0;

	if (buf == NULL) {
		pr_notice("[%s] *buf is NULL!\n",  __func__);
		return -EINVAL;
	}
	regmap_read(was_priv->regmap, WAS4780_SWITCH_SETTINGS, &set_value);
	regmap_read(was_priv->regmap, WAS4780_SWITCH_CONTROL, &ctl_value);
	ret = sprintf(buf, "reg:SW_SET(0x04): %#x, SW_CTL(0x05): %#x\n",
			set_value, ctl_value);

	return ret;
}

ssize_t headset_state_show(struct device *was_dev, struct device_attribute *attr,
		char *buf)
{
	struct was4780_priv *was_priv = dev_get_drvdata(was_dev);
	unsigned int set_value = NO_EARPIECE;
	int ret = 0;

	if (buf == NULL) {
		pr_notice("[%s] *buf is NULL!\n",  __func__);
		return -EINVAL;
	}
	if (was_priv->earpiece_type > NO_EARPIECE &&
		was_priv->earpiece_type < TYPE_MAX)
		set_value = was_priv->earpiece_type;
	ret = sprintf(buf, "%#x", set_value);

	return ret;
}

static DEVICE_ATTR_RO(sw_state);
static DEVICE_ATTR_RO(headset_state);

static struct attribute *switch_attributes[] = {
	&dev_attr_sw_state.attr,
	&dev_attr_headset_state.attr,
	NULL
};

static struct attribute_group switch_attr_group = {
	.attrs = switch_attributes,
};

static void was4780_usbc_analog_work_fn(struct work_struct *work)
{
	struct was4780_priv *was_priv =
		container_of(work, struct was4780_priv, usbc_analog_work);
	int audio_mode;
	unsigned int audio_set;
	unsigned int status;
	if (!was_priv) {
		pr_info("%s: was container invalid", __func__);
		return;
	}

	mutex_lock(&was_priv->usbc_lock);

	audio_mode = atomic_read(&(was_priv->usbc_mode));
	if (audio_mode) {
		/*Spruce code for OSPURCET-1315 by wangzr10 at 20230222 start*/
		if (usbc_supply == WAS4780) {
			was4780_usbc_update_settings(was_priv, 0x00, 0x9F);
			regmap_write(was_priv->regmap, WAS4780_FUNC_EN, 0x0D);
			usleep_range(10000, 12000);
			regmap_read(was_priv->regmap, 0x18, &status);
			regmap_read(was_priv->regmap, 0x05, &status);
			regmap_write(was_priv->regmap, 0x05, status & 0xE7);
		} else if (usbc_supply == DIO4480) {
			was4780_usbc_update_settings(was_priv, 0x00, 0x87);
			regmap_write(was_priv->regmap, 0x08, 0x8F);
			regmap_write(was_priv->regmap, 0x09, 0x8F);
			regmap_write(was_priv->regmap, 0x0a, 0x8F);
			regmap_write(was_priv->regmap, 0x0b, 0x8F);
			regmap_write(was_priv->regmap, 0x0c, 0x8F);
			regmap_write(was_priv->regmap, WAS4780_FUNC_EN, 0x08);
		}
		/*Spruce code for OSPURCET-1315 by wangzr10 at 20230222 end*/
	} else {
		/* switch to usb */

		if (usbc_supply == DIO4480)
			regmap_write(was_priv->regmap, DIO4480_SWITCH_RESET, 0x01);

		was4780_usbc_update_settings(was_priv, 0x18, 0x98);
		was_priv->earpiece_type = NO_EARPIECE;

	}
	regmap_read(was_priv->regmap, WAS4780_SWITCH_CONTROL, &audio_set);
	regmap_read(was_priv->regmap, 0x17, &status);
	blocking_notifier_call_chain(&was_priv->was4780_notifier, audio_mode, NULL);

	mutex_unlock(&was_priv->usbc_lock);
}


static void was4780_update_reg_defaults(struct regmap *regmap)
{
	u8 i;

	if (usbc_supply == INVALID_TYPE) {
		pr_info("%s invalid usbc type, just return", __func__);
		return;
	}

	if (usbc_supply == DIO4480) {
		for (i = 0; i < ARRAY_SIZE(dio_reg_i2c_defaults); i++)
			regmap_write(regmap, dio_reg_i2c_defaults[i].reg,
					   dio_reg_i2c_defaults[i].val);
	} else {
		for (i = 0; i < ARRAY_SIZE(was_reg_i2c_defaults); i++)
			regmap_write(regmap, was_reg_i2c_defaults[i].reg,
					   was_reg_i2c_defaults[i].val);
	}
}

static void usbc_switch_type_check(struct regmap *regmap)
{
	unsigned int chip_id;

	/*Get chip id*/
	regmap_read(regmap, 0x00, &chip_id);

	switch (chip_id) {
	case 0x11:
		usbc_supply = WAS4780;
		break;
	case 0xF1:
		usbc_supply = DIO4480;
		break;
	case 0x00:
		usbc_supply = OCP96011;
		break;
	default:
		usbc_supply = INVALID_TYPE;
		break;
	}

	pr_info("%s usbc type: %s", __func__, usbc_switch_name[usbc_supply]);
}

static int was4780_probe(struct i2c_client *i2c,
			 const struct i2c_device_id *id)
{
	struct was4780_priv *was_priv;
	int rc = 0;

	was_priv = devm_kzalloc(&i2c->dev, sizeof(*was_priv),
				GFP_KERNEL);
	if (!was_priv)
		return -ENOMEM;

	was_priv->dev = &i2c->dev;
	was_priv->earpiece_type = NO_EARPIECE;

	was_priv->regmap = devm_regmap_init_i2c(i2c, &was4780_regmap_config);
	if (IS_ERR_OR_NULL(was_priv->regmap)) {
		dev_err(was_priv->dev, "%s: Failed to initialize regmap: %d\n",
			__func__, rc);
		if (!was_priv->regmap) {
			rc = -EINVAL;
			goto err_data;
		}
		rc = PTR_ERR(was_priv->regmap);
		goto err_data;
	}

	usbc_switch_type_check(was_priv->regmap);
	was4780_update_reg_defaults(was_priv->regmap);
	/*Spruce code for OSPURCET-1315 by wangzr10 at 20230222 start*/
	if (usbc_supply == DIO4480)
		regmap_write(was_priv->regmap, DIO4480_SWITCH_RESET, 0x01);
	/*Spruce code for OSPURCET-1315 by wangzr10 at 20230222 end*/

	was_priv->tcpc = tcpc_dev_get_by_name("type_c_port0");
	if (!was_priv->tcpc) {
		dev_notice(was_priv->dev, "%s get tcpc dev fail\n", __func__);
		return -EPROBE_DEFER;
            //goto err_data;
	}
	dev_notice(was_priv->dev, "%s qiantao get tcpc dev fail\n", __func__);
	mutex_init(&was_priv->usbc_lock);
	i2c_set_clientdata(i2c, was_priv);
	rc = sysfs_create_group(&i2c->dev.kobj, &switch_attr_group);
	if (rc) {
		dev_err(was_priv->dev, "%s: Failed to create attr", __func__);
		goto err_data;
	}

	was_priv->usbc_workqueue = create_singlethread_workqueue("usbc_switch");
	INIT_WORK(&was_priv->usbc_analog_work,
		  was4780_usbc_analog_work_fn);

	BLOCKING_INIT_NOTIFIER_HEAD(&was_priv->was4780_notifier);

	was_priv->pd_nb.notifier_call = was4780_usbc_event_changed;
	was_priv->pd_nb.priority = 0;
	rc = register_tcp_dev_notifier(was_priv->tcpc, &was_priv->pd_nb,
				TCP_NOTIFY_TYPE_USB);
	if (rc) {
		dev_err(was_priv->dev, "%s: tcpc notify reg failed: %d\n",
			__func__, rc);
		goto err_remove;
	}
	dev_err(was_priv->dev, "%s: was4780 driver registered successfully", __func__);

	return 0;

err_remove:
	sysfs_remove_group(&i2c->dev.kobj, &switch_attr_group);
err_data:
	devm_kfree(&i2c->dev, was_priv);
	dev_err(was_priv->dev, "%s: was4780 deriver failed", __func__);

	return rc;
}

static int was4780_remove(struct i2c_client *i2c)
{
	struct was4780_priv *was_priv =
			(struct was4780_priv *)i2c_get_clientdata(i2c);

	if (!was_priv)
		return -EINVAL;

	destroy_workqueue(was_priv->usbc_workqueue);
	was4780_usbc_update_settings(was_priv, 0x18, 0x98);
	mutex_destroy(&was_priv->usbc_lock);
	sysfs_remove_group(&i2c->dev.kobj, &switch_attr_group);
	unregister_tcp_dev_notifier(was_priv->tcpc, &was_priv->pd_nb,
				TCP_NOTIFY_TYPE_USB);
	dev_set_drvdata(&i2c->dev, NULL);

	return 0;
}

static const struct of_device_id was4780_i2c_dt_match[] = {
	{
		.compatible = "willsemi,was4780-i2c",
	},
	{}
};

static struct i2c_driver was4780_i2c_driver = {
	.driver = {
		.name = SWITCH_I2C_NAME,
		.of_match_table = was4780_i2c_dt_match,
	},
	.probe = was4780_probe,
	.remove = was4780_remove,
};

static int __init was4780_init(void)
{
	int rc;

	rc = i2c_add_driver(&was4780_i2c_driver);
	if (rc)
		pr_info("was4780: Failed to register I2C driver: %d\n", rc);

	return rc;
}
subsys_initcall(was4780_init);

static void __exit was4780_exit(void)
{
	i2c_del_driver(&was4780_i2c_driver);
}
module_exit(was4780_exit);

MODULE_DESCRIPTION("was4780 I2C driver");
MODULE_LICENSE("GPL v2");
