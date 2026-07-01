/*
 * HID over I2C protocol implementation
 *
 * Copyright (c) 2012 Benjamin Tissoires <benjamin.tissoires@gmail.com>
 * Copyright (c) 2012 Ecole Nationale de l'Aviation Civile, France
 * Copyright (c) 2012 Red Hat, Inc
 *
 * This code is partly based on "USB HID support for Linux":
 *
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2005 Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2005 Michael Haboustak <mike-@cinci.rr.com> for Concept2, Inc
 *  Copyright (c) 2007-2008 Oliver Neukum
 *  Copyright (c) 2006-2010 Jiri Kosina
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/hid.h>
#include <linux/mutex.h>
#include <linux/acpi.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <linux/platform_data/i2c-hid.h>

#include <linux/gpio.h>
#include <linux/of_gpio.h>
//#include <mt-plat/mtk_boot.h>
#include "../hid-ids.h"
#include "i2c-hid.h"

/* quirks to control the device */
#define I2C_HID_QUIRK_SET_PWR_WAKEUP_DEV	BIT(0)
#define I2C_HID_QUIRK_NO_IRQ_AFTER_RESET	BIT(1)
#define I2C_HID_QUIRK_NO_RUNTIME_PM		BIT(2)

/* flags */
#define I2C_HID_STARTED		0
#define I2C_HID_RESET_PENDING	1
#define I2C_HID_READ_PENDING	2

#define I2C_HID_PWR_ON		0x00
#define I2C_HID_PWR_SLEEP	0x01
#define I2C_HID_IDLE_OFF	0x00
#define I2C_HID_IDLE_ON		0x01

#define SPECIAL_KEY_KB_ENABLE           0x28e
#define SPECIAL_KEY_KB_DISABLE          0x28f

#define SPECIAL_KEY_LOCKSCREEN          0x280
#define SPECIAL_KEY_SWITCHLANGUAGE      0x282
#define SPECIAL_KEY_MICDISABLE          0x283
#define SPECIAL_KEY_TOUCHPANELMUTE      0x284
#define SPECIAL_KEY_GLOBALSEARCH        0x285
#define SPECIAL_KEY_FULLSCREEN          0x286
#define SPECIAL_KEY_SPLITSCREEN         0x287
#define SPECIAL_KEY_PMODESWITCH         0x288
#define SPECIAL_KEY_SUPERINTCON         0x289
#define SPECIAL_KEY_CUSTOMERAPP1        0x28a
#define SPECIAL_KEY_CUSTOMERAPP2        0x28b
#define SPECIAL_KEY_FN                  0x28c
#define SPECIAL_KEY_SETTING             0x28d

int kb_hall_gpio;
bool spec_key_lockscreen = false;
bool spec_key_switchlanguage = false;
bool spec_key_micdisable = false;
bool spec_key_touchpanelmute = false;
bool spec_key_globalsearch = false;
bool spec_key_fullscreen = false;
bool spec_key_splitscreen = false;
bool spec_key_superintcon = false;
bool spec_key_customerapp1 = false;
bool spec_key_customerapp2 = false;

struct kobject *kobject_status;
struct input_dev *kb_input_dev=NULL;
struct input_dev *mouse_input_dev=NULL;
int register_kpd_wakeup_devices(void);
int register_mos_wakeup_devices(void);
/*Spruce code for OSPURCET-1235 by chenzm9 at 2023/2/16 start*/
static int kb_i2c_hid_suspend(void);
static int kb_i2c_hid_resume(void);
/*Spruce code for OSPURCET-1235 by chenzm9 at 2023/2/16 end*/
int screen_is_black = 1;

int kb_report_power_key(void);
int mouse_report_power_key(void);

int register_mouse_wakeup_devices(void);
struct hid_device_keyboard *device_keyboard;
void hidinput_connection_worker(struct work_struct *work);
/* Spruce code for OSPURCET-780 by sunft3 at 2023/01/18 start */
extern void hidinput_disconnect(struct hid_device *hid);
extern int hidinput_connect(struct hid_device *hid, unsigned int force);
static unsigned char g_screen_on = 0;
/* Spruce code for OSPURCET-780 by sunft3 at 2023/01/18 end */

int mcu_en_gpio = 0;
int mcu_rst_gpio = 0;
/*Spruce code for OSPURCET-1235 by chenzm9 at 2023/2/16 start*/
int mcu_resume_gpio = 0;
bool g_already_sleep = false;
/*Spruce code for OSPURCET-1235 by chenzm9 at 2023/2/16 end*/
//EXPORT_SYMBOL_GPL(device_keyboard->kb_connect_status);
EXPORT_SYMBOL_GPL(device_keyboard);
/* debug option */
static bool debug = 1;
module_param(debug, bool, 0444);
MODULE_PARM_DESC(debug, "print a lot of debug information");
struct hid_device *g_phid = NULL;
/* Spruce code for OSPURCET-780 by sunft3 at 2023/01/18 start */
struct i2c_hid *g_ihid=NULL;
struct i2c_client *g_client=NULL;
/* Spruce code for OSPURCET-780 by sunft3 at 2023/01/18 end */

#define i2c_hid_dbg(ihid, fmt, arg...)					  \
do {									  \
	if (debug)							  \
		dev_printk(KERN_ERR, &(ihid)->client->dev, fmt, ##arg); \
} while (0)

struct i2c_hid_desc {
	__le16 wHIDDescLength;
	__le16 bcdVersion;
	__le16 wReportDescLength;
	__le16 wReportDescRegister;
	__le16 wInputRegister;
	__le16 wMaxInputLength;
	__le16 wOutputRegister;
	__le16 wMaxOutputLength;
	__le16 wCommandRegister;
	__le16 wDataRegister;
	__le16 wVendorID;
	__le16 wProductID;
	__le16 wVersionID;
	__le32 reserved;
} __packed;

struct i2c_hid_cmd {
	unsigned int registerIndex;
	__u8 opcode;
	unsigned int length;
	bool wait;
};

union command {
	u8 data[0];
	struct cmd {
		__le16 reg;
		__u8 reportTypeID;
		__u8 opcode;
	} __packed c;
};

#define I2C_HID_CMD(opcode_) \
	.opcode = opcode_, .length = 4, \
	.registerIndex = offsetof(struct i2c_hid_desc, wCommandRegister)

/* fetch HID descriptor */
static const struct i2c_hid_cmd hid_descr_cmd = { .length = 2 };
/* fetch report descriptors */
static const struct i2c_hid_cmd hid_report_descr_cmd = {
		.registerIndex = offsetof(struct i2c_hid_desc,
			wReportDescRegister),
		.opcode = 0x00,
		.length = 2 };
/* commands */
static const struct i2c_hid_cmd hid_reset_cmd =		{ I2C_HID_CMD(0x01),
							  .wait = true };
static const struct i2c_hid_cmd hid_get_report_cmd =	{ I2C_HID_CMD(0x02) };
static const struct i2c_hid_cmd hid_set_report_cmd =	{ I2C_HID_CMD(0x03) };
static const struct i2c_hid_cmd hid_set_power_cmd =	{ I2C_HID_CMD(0x08) };
static const struct i2c_hid_cmd hid_no_cmd =		{ .length = 0 };
static const struct i2c_hid_cmd hid_set_idle_cmd = 	{ I2C_HID_CMD(0x05) };
/*
 * These definitions are not used here, but are defined by the spec.
 * Keeping them here for documentation purposes.
 *
 * static const struct i2c_hid_cmd hid_get_idle_cmd = { I2C_HID_CMD(0x04) };
 * static const struct i2c_hid_cmd hid_set_idle_cmd = { I2C_HID_CMD(0x05) };
 * static const struct i2c_hid_cmd hid_get_protocol_cmd = { I2C_HID_CMD(0x06) };
 * static const struct i2c_hid_cmd hid_set_protocol_cmd = { I2C_HID_CMD(0x07) };
 */

static DEFINE_MUTEX(i2c_hid_open_mut);

/* The main device structure */
struct i2c_hid {
	struct i2c_client	*client;	/* i2c client */
	struct hid_device	*hid;	/* pointer to corresponding HID dev */
	union {
		__u8 hdesc_buffer[sizeof(struct i2c_hid_desc)];
		struct i2c_hid_desc hdesc;	/* the HID Descriptor */
	};
	__le16			wHIDDescRegister; /* location of the i2c
						   * register of the HID
						   * descriptor. */
	unsigned int		bufsize;	/* i2c buffer size */
	u8			*inbuf;		/* Input buffer */
	u8			*rawbuf;	/* Raw Input buffer */
	u8			*cmdbuf;	/* Command buffer */
	u8			*argsbuf;	/* Command arguments buffer */

	unsigned long		flags;		/* device flags */
	unsigned long		quirks;		/* Various quirks */

	wait_queue_head_t	wait;		/* For waiting the interrupt */

	struct i2c_hid_platform_data pdata;

	bool			irq_wake_enabled;
	struct mutex		reset_lock;
};

/* Spruce code for OSPURCET-780 by chenzm9 at 2023/1/13 start */
static const struct i2c_hid_quirks {
	__u16 idVendor;
	__u16 idProduct;
	__u32 quirks;
} i2c_hid_quirks[] = {
	{ USB_VENDOR_ID_WEIDA, USB_DEVICE_ID_WEIDA_8752,
		I2C_HID_QUIRK_SET_PWR_WAKEUP_DEV },
	{ USB_VENDOR_ID_WEIDA, USB_DEVICE_ID_WEIDA_8755,
		I2C_HID_QUIRK_SET_PWR_WAKEUP_DEV },
	{ I2C_VENDOR_ID_HT32F5, I2C_PRODUCT_ID_HT32F5,
		I2C_HID_QUIRK_NO_IRQ_AFTER_RESET |
		I2C_HID_QUIRK_NO_RUNTIME_PM |
		I2C_HID_QUIRK_SET_PWR_WAKEUP_DEV },
	{ I2C_VENDOR_ID_HT32F5_TMP, I2C_PRODUCT_ID_HT32F5_TMP,
		I2C_HID_QUIRK_NO_IRQ_AFTER_RESET |
		I2C_HID_QUIRK_NO_RUNTIME_PM |
		I2C_HID_QUIRK_SET_PWR_WAKEUP_DEV },
	{ I2C_VENDOR_ID_HT32F5_SPR, I2C_PRODUCT_ID_HT32F5_SPR,
		I2C_HID_QUIRK_NO_IRQ_AFTER_RESET |
		I2C_HID_QUIRK_NO_RUNTIME_PM |
		I2C_HID_QUIRK_SET_PWR_WAKEUP_DEV },
	{ 0, 0 }

};
/* Spruce code for OSPURCET-780 by chenzm9 at 2023/1/13 end */

int kb_report_power_key()
{
	printk(KERN_ERR "===%s===\n",__func__);
	if(kb_input_dev==NULL){
		printk(KERN_ERR "kb_input_dev is NULL");
		return -1;
	}
	input_report_key(kb_input_dev, KEY_POWER, 1);
	input_sync(kb_input_dev);
	input_report_key(kb_input_dev, KEY_POWER, 0);
	input_sync(kb_input_dev);
	return 0;
}
EXPORT_SYMBOL_GPL(kb_report_power_key);
/*
 * Register the input device; print a message.
 * Configure the input layer interface
 * Read all reports and initialize the absolute field values.
 */
int register_kb_wakeup_devices(void)
{
	printk(KERN_ERR "==keyboard register_report_power_key==");
	kb_input_dev = input_allocate_device();
	if(kb_input_dev==NULL){
		printk(KERN_ERR "failed to allocate keyboard report_power_key device");
		return -1;
	}
	input_set_capability(kb_input_dev, EV_KEY, KEY_POWER);
	input_set_capability(kb_input_dev, EV_KEY, SPECIAL_KEY_KB_ENABLE);
	input_set_capability(kb_input_dev, EV_KEY, SPECIAL_KEY_KB_DISABLE);
	input_set_capability(kb_input_dev, EV_KEY, SPECIAL_KEY_LOCKSCREEN);
	input_set_capability(kb_input_dev, EV_KEY, SPECIAL_KEY_SWITCHLANGUAGE);
	input_set_capability(kb_input_dev, EV_KEY, SPECIAL_KEY_MICDISABLE);
	input_set_capability(kb_input_dev, EV_KEY, SPECIAL_KEY_TOUCHPANELMUTE);
	input_set_capability(kb_input_dev, EV_KEY, SPECIAL_KEY_GLOBALSEARCH);
	input_set_capability(kb_input_dev, EV_KEY, SPECIAL_KEY_FULLSCREEN);
	input_set_capability(kb_input_dev, EV_KEY, SPECIAL_KEY_SPLITSCREEN);
	input_set_capability(kb_input_dev, EV_KEY, SPECIAL_KEY_SUPERINTCON);
	input_set_capability(kb_input_dev, EV_KEY, SPECIAL_KEY_CUSTOMERAPP1);
	input_set_capability(kb_input_dev, EV_KEY, SPECIAL_KEY_CUSTOMERAPP2);
	kb_input_dev->name="keyboard_wakeup_devices";
	if (input_register_device(kb_input_dev)){
		printk(KERN_ERR "failed to register keyboard report_power_key device");
		return -1;
	}
	return 0;

}
EXPORT_SYMBOL_GPL(register_kb_wakeup_devices);

/*
void hidinput_connection_worker(struct work_struct *work)
{
	struct hid_device *hid = g_phid;
        printk(KERN_DEBUG "hidinput connection work start,vendor:0x%x,input_register=%d,device_keyboard->kb_connect_status=%d\n",
		hid->vendor,device_keyboard->input_registered,device_keyboard->kb_connect_status);
        if (!device_keyboard->input_registered && device_keyboard->kb_connect_status) {
                if(hidinput_connect(hid,0) == 0)
			device_keyboard->input_registered = true;
        } else if (device_keyboard->input_registered && !device_keyboard->kb_connect_status) {
                hidinput_disconnect(hid);
		device_keyboard->input_registered = false;
        }
}
*/
/*
 * i2c_hid_lookup_quirk: return any quirks associated with a I2C HID device
 * @idVendor: the 16-bit vendor ID
 * @idProduct: the 16-bit product ID
 *
 * Returns: a u32 quirks value.
 */
static u32 i2c_hid_lookup_quirk(const u16 idVendor, const u16 idProduct)
{
	u32 quirks = 0;
	int n;

	for (n = 0; i2c_hid_quirks[n].idVendor; n++)
		if (i2c_hid_quirks[n].idVendor == idVendor &&
		    (i2c_hid_quirks[n].idProduct == (__u16)HID_ANY_ID ||
		     i2c_hid_quirks[n].idProduct == idProduct))
			quirks = i2c_hid_quirks[n].quirks;

	return quirks;
}

static int __i2c_hid_command(struct i2c_client *client,
		const struct i2c_hid_cmd *command, u8 reportID,
		u8 reportType, u8 *args, int args_len,
		unsigned char *buf_recv, int data_len)
{
	struct i2c_hid *ihid = i2c_get_clientdata(client);
	union command *cmd = (union command *)ihid->cmdbuf;
	int ret;
	struct i2c_msg msg[2];
	int msg_num = 1;

	int length = command->length;
	bool wait = command->wait;
	unsigned int registerIndex = command->registerIndex;

	/* special case for hid_descr_cmd */
	if (command == &hid_descr_cmd) {
		cmd->c.reg = ihid->wHIDDescRegister;
	} else {
		cmd->data[0] = ihid->hdesc_buffer[registerIndex];
		cmd->data[1] = ihid->hdesc_buffer[registerIndex + 1];
	}

	if (length > 2) {
		cmd->c.opcode = command->opcode;
		cmd->c.reportTypeID = reportID | reportType << 4;
	}

	memcpy(cmd->data + length, args, args_len);
	length += args_len;

	i2c_hid_dbg(ihid, "%s: cmd=%*ph\n", __func__, length, cmd->data);

	msg[0].addr = client->addr;
	msg[0].flags = client->flags & I2C_M_TEN;
	msg[0].len = length;
	msg[0].buf = cmd->data;
	if (data_len > 0) {
		msg[1].addr = client->addr;
		msg[1].flags = client->flags & I2C_M_TEN;
		msg[1].flags |= I2C_M_RD;
		msg[1].len = data_len;
		msg[1].buf = buf_recv;
		msg_num = 2;
		set_bit(I2C_HID_READ_PENDING, &ihid->flags);
	}

	if (wait)
		set_bit(I2C_HID_RESET_PENDING, &ihid->flags);

	ret = i2c_transfer(client->adapter, msg, msg_num);

	if (data_len > 0)
		clear_bit(I2C_HID_READ_PENDING, &ihid->flags);

	if (ret != msg_num)
		return ret < 0 ? ret : -EIO;

	ret = 0;

	if (wait && (ihid->quirks & I2C_HID_QUIRK_NO_IRQ_AFTER_RESET)) {
		msleep(100);
	} else if (wait) {
		i2c_hid_dbg(ihid, "%s: waiting...\n", __func__);
		if (!wait_event_timeout(ihid->wait,
				!test_bit(I2C_HID_RESET_PENDING, &ihid->flags),
				msecs_to_jiffies(5000)))
			ret = -ENODATA;
		i2c_hid_dbg(ihid, "%s: finished.\n", __func__);
	}

	return ret;
}

static int i2c_hid_command(struct i2c_client *client,
		const struct i2c_hid_cmd *command,
		unsigned char *buf_recv, int data_len)
{
	return __i2c_hid_command(client, command, 0, 0, NULL, 0,
				buf_recv, data_len);
}

static int i2c_hid_get_report(struct i2c_client *client, u8 reportType,
		u8 reportID, unsigned char *buf_recv, int data_len)
{
	struct i2c_hid *ihid = i2c_get_clientdata(client);
	u8 args[3];
	int ret;
	int args_len = 0;
	u16 readRegister = le16_to_cpu(ihid->hdesc.wDataRegister);

	i2c_hid_dbg(ihid, "%s\n", __func__);

	if (reportID >= 0x0F) {
		args[args_len++] = reportID;
		reportID = 0x0F;
	}

	args[args_len++] = readRegister & 0xFF;
	args[args_len++] = readRegister >> 8;

	ret = __i2c_hid_command(client, &hid_get_report_cmd, reportID,
		reportType, args, args_len, buf_recv, data_len);
	if (ret) {
		dev_err(&client->dev,
			"failed to retrieve report from device.\n");
		return ret;
	}

	return 0;
}

/**
 * i2c_hid_set_or_send_report: forward an incoming report to the device
 * @client: the i2c_client of the device
 * @reportType: 0x03 for HID_FEATURE_REPORT ; 0x02 for HID_OUTPUT_REPORT
 * @reportID: the report ID
 * @buf: the actual data to transfer, without the report ID
 * @len: size of buf
 * @use_data: true: use SET_REPORT HID command, false: send plain OUTPUT report
 */
static int i2c_hid_set_or_send_report(struct i2c_client *client, u8 reportType,
		u8 reportID, unsigned char *buf, size_t data_len, bool use_data)
{
	struct i2c_hid *ihid = i2c_get_clientdata(client);
	u8 *args = ihid->argsbuf;
	const struct i2c_hid_cmd *hidcmd;
	int ret;
	u16 dataRegister = le16_to_cpu(ihid->hdesc.wDataRegister);
	u16 outputRegister = le16_to_cpu(ihid->hdesc.wOutputRegister);
	u16 maxOutputLength = le16_to_cpu(ihid->hdesc.wMaxOutputLength);
	u16 size;
	int args_len;
	int index = 0;

	i2c_hid_dbg(ihid, "%s\n", __func__);

	if (data_len > ihid->bufsize)
		return -EINVAL;

	size =		2			/* size */ +
			(reportID ? 1 : 0)	/* reportID */ +
			data_len		/* buf */;
	args_len =	(reportID >= 0x0F ? 1 : 0) /* optional third byte */ +
			2			/* dataRegister */ +
			size			/* args */;

	if (!use_data && maxOutputLength == 0)
		return -ENOSYS;

	if (reportID >= 0x0F) {
		args[index++] = reportID;
		reportID = 0x0F;
	}

	/*
	 * use the data register for feature reports or if the device does not
	 * support the output register
	 */
	if (use_data) {
		args[index++] = dataRegister & 0xFF;
		args[index++] = dataRegister >> 8;
		hidcmd = &hid_set_report_cmd;
	} else {
		args[index++] = outputRegister & 0xFF;
		args[index++] = outputRegister >> 8;
		hidcmd = &hid_no_cmd;
	}

	args[index++] = size & 0xFF;
	args[index++] = size >> 8;

	if (reportID)
		args[index++] = reportID;

	memcpy(&args[index], buf, data_len);

	ret = __i2c_hid_command(client, hidcmd, reportID,
		reportType, args, args_len, NULL, 0);
	if (ret) {
		dev_err(&client->dev, "failed to set a report to device.\n");
		return ret;
	}

	return data_len;
}

static int i2c_hid_set_power(struct i2c_client *client, int power_state)
{
	struct i2c_hid *ihid = i2c_get_clientdata(client);
	int ret;
	/* Spruce code for OSPURCET-1227 by sunft3 at 2023/02/10 start */
	int i = 0;

	i2c_hid_dbg(ihid, "%s\n", __func__);

	/*
	 * Some devices require to send a command to wakeup before power on.
	 * The call will get a return value (EREMOTEIO) but device will be
	 * triggered and activated. After that, it goes like a normal device.
	 */
	if (power_state == I2C_HID_PWR_ON &&
	    ihid->quirks & I2C_HID_QUIRK_SET_PWR_WAKEUP_DEV) {
		i2c_hid_dbg(ihid, "%s:send out one cmd to wakeup\n",__func__);
		ret = i2c_hid_command(client, &hid_set_power_cmd, NULL, 0);

		/* Device was already activated */
		if (!ret) {
			i2c_hid_dbg(ihid, "%s:Device was already activated\n",__func__);
			goto set_pwr_exit;
		}
	}
	msleep(10);
	ret = __i2c_hid_command(client, &hid_set_power_cmd, power_state,
		0, NULL, 0, NULL, 0);

	if (ret) {
		dev_err(&client->dev, "failed to change power setting,then retry\n");
		while(i<3) {
			msleep(10);
			ret = __i2c_hid_command(client, &hid_set_power_cmd, power_state,
			0, NULL, 0, NULL, 0);
			if (!ret) {
				break;
			}
			i++;
		}
	}
	if(ret) {
		dev_err(&client->dev, "failed to change power setting retry 3 times,set reset pin to 1.\n");
		gpio_set_value(mcu_rst_gpio,0);
		msleep(10);
		gpio_set_value(mcu_rst_gpio,1);
		msleep(10);
		dev_err(&client->dev, "power setting!\n");
		pr_err("%s:mcu_gpio value is %d after setting as 1\n", __func__,mcu_rst_gpio);
	}
		/* Spruce code for OSPURCET-1227 by sunft3 at 2023/02/10 end */

set_pwr_exit:
	return ret;
}

static int i2c_hid_hwreset(struct i2c_client *client)
{
	struct i2c_hid *ihid = i2c_get_clientdata(client);
	int ret;

	i2c_hid_dbg(ihid, "%s\n", __func__);
	/* Spruce code for OSPURCET-780 by sunft3 at 2023/01/18 start */
	if(g_screen_on == 0){
		i2c_hid_dbg(ihid, "%s screen is off didn't reset\n", __func__);
		return 0;
	}
	/* Spruce code for OSPURCET-780 by sunft3 at 2023/01/18 end */

	/*
	 * This prevents sending feature reports while the device is
	 * being reset. Otherwise we may lose the reset complete
	 * interrupt.
	 */
	mutex_lock(&ihid->reset_lock);

	/*Spruce code for OSPURCET-1235 by chenzm9 at 2023/2/16 start*/
	ret = kb_i2c_hid_resume();
	/*Spruce code for OSPURCET-1235 by chenzm9 at 2023/2/16 end*/
	if (ret)
		goto out_unlock;

	/*
	 * The HID over I2C specification states that if a DEVICE needs time
	 * after the PWR_ON request, it should utilise CLOCK stretching.
	 * However, it has been observered that the Windows driver provides a
	 * 1ms sleep between the PWR_ON and RESET requests and that some devices
	 * rely on this.
	 */
	usleep_range(1000, 5000);

	i2c_hid_dbg(ihid, "resetting...\n");

	ret = i2c_hid_command(client, &hid_reset_cmd, NULL, 0);
	if (ret) {
		dev_err(&client->dev, "failed to reset device.\n");
		/*Spruce code for OSPURCET-1235 by chenzm9 at 2023/2/16 start*/
		kb_i2c_hid_suspend();
		/*Spruce code for OSPURCET-1235 by chenzm9 at 2023/2/16 end*/
	}

out_unlock:
	mutex_unlock(&ihid->reset_lock);
	return ret;
}

int report_special_key(unsigned int key_value,char up_down)
{
	printk(KERN_ERR "===%s=== key_value=%02x event=EV_KEY\n",__func__,key_value);
	if(kb_input_dev==NULL){
		printk(KERN_ERR "kb_input_dev is NULL");
		return -1;
	}
	input_event(kb_input_dev, EV_KEY, key_value, up_down);
	input_sync(kb_input_dev);
	//input_report_key(kb_input_dev, key_value, 0);
	//input_sync(kb_input_dev);
	return 0;
}

//#define kb_debug
#ifdef kb_debug
static int i=0;

#endif

/* Spruce code for OSPURCET-780 by sunft3 at 2023/01/18 start */
//static int kb_laststatus=0/*,spec_key_updown = 0*/;
static void i2c_hid_get_input(struct i2c_hid *ihid)
{
	int ret;
	u32 ret_size;
	int adc_value = 0;
	int kb_is_enabled;
	int size = le16_to_cpu(ihid->hdesc.wMaxInputLength);
	printk("0401===%s===\n",__func__);
	if (size > ihid->bufsize)
		size = ihid->bufsize;

	ret = i2c_master_recv(ihid->client, ihid->inbuf, size);
	if (ret != size) {
		if (ret < 0)
			return;

		dev_err(&ihid->client->dev, "%s: got %d data instead of %d\n",
			__func__, ret, size);
		return;
	}

	ret_size = ihid->inbuf[0] | ihid->inbuf[1] << 8;

	if (!ret_size) {
		/* host or device initiated RESET completed */
		if (test_and_clear_bit(I2C_HID_RESET_PENDING, &ihid->flags))
			wake_up(&ihid->wait);
		return;
	}

	if ((ret_size > size) || (ret_size < 2)) {
		dev_err(&ihid->client->dev, "%s: incomplete report (%d/%d)\n",
			__func__, size, ret_size);
		return;
	}

	i2c_hid_dbg(ihid, "input: %*ph\n", ret_size, ihid->inbuf);

	/* Spruce code for OSPURCET-749 by chenzm9 at 2023/1/12 start */
	if(ihid->inbuf[2]==0x06){
		adc_value= ihid->inbuf[7]| ihid->inbuf[8]<<8;
		device_keyboard->kb_hall=ihid->inbuf[3]&0x02?1:0;
		device_keyboard->kb_connect_status=ihid->inbuf[3]&0x01;
		if(g_screen_on ==0 && device_keyboard->kb_connect_status == 1){
			printk("%s:kb_report_power_key\n",__func__);
			kb_report_power_key();
		}
		/* Spruce code for OSPURCET-1347 by chenzm9 at 2023/2/17 start */
		if((ihid->hid->vendor==0x17EF && ihid->hid->product==0x6175) || (ihid->hid->vendor==0x04F3 && ihid->hid->product==0x31A8) || (ihid->hid->vendor==0x32CD && ihid->hid->product==0x003A)) {
			if(!device_keyboard->input_registered && device_keyboard->kb_connect_status) {
				if(hidinput_connect(ihid->hid,0) == 0)
					device_keyboard->input_registered = true;
			} else if(device_keyboard->input_registered && !device_keyboard->kb_connect_status) {
				hidinput_disconnect(ihid->hid);
				device_keyboard->input_registered = false;
			} else {
				printk("%s adc_value =0x%x,kb_connect_status=%d,input_registered=%d return\n",
					__func__,adc_value,device_keyboard->kb_connect_status,device_keyboard->input_registered);
				return;
			}

		}
		/* Spruce code for OSPURCET-1347 by chenzm9 at 2023/2/17 end */
		printk("adc_value =0x%x,device_keyboard->kb_connect_status=%d,kb_hall=%d,input_registered=%d,kb_hall_gpio:[%d]\n",
		adc_value,device_keyboard->kb_connect_status,device_keyboard->kb_hall,device_keyboard->input_registered,gpio_get_value(kb_hall_gpio));
		/* Spruce code for OSPURCET-780 by sunft3 at 2023/01/18 end */
	}
	/* Spruce code for OSPURCET-749 by chenzm9 at 2023/1/12 end */
	/* Spruce code for OSPURCET-1227 by sunft3 at 2023/02/10 start */
	if(ihid->inbuf[2]==0x07){
		kb_is_enabled = ihid->inbuf[3] & 0x01;
		if (kb_is_enabled) {
			input_report_key(kb_input_dev, SPECIAL_KEY_KB_ENABLE, 1);
			input_sync(kb_input_dev);
			input_report_key(kb_input_dev, SPECIAL_KEY_KB_ENABLE, 0);
			input_sync(kb_input_dev);
		} else {
			input_report_key(kb_input_dev, SPECIAL_KEY_KB_DISABLE, 1);
			input_sync(kb_input_dev);
			input_report_key(kb_input_dev, SPECIAL_KEY_KB_DISABLE, 0);
			input_sync(kb_input_dev);
		}
		printk("%s:input_report_key:[%d].\n", __func__, kb_is_enabled);
	}
	/* Spruce code for OSPURCET-1227 by sunft3 at 2023/02/10 end */

	/* Spruce code for OSPURCET-780 by chenzm9 at 2023/1/13 start */
	if ((ihid->hid->vendor==0x04F3&&ihid->hid->product==0x31A8) || (ihid->hid->vendor==0x32CD&&ihid->hid->product==0x003A) || (ihid->hid->vendor==0x17EF&&ihid->hid->product==0x6175)){
		if (test_bit(I2C_HID_STARTED, &ihid->flags) && device_keyboard->kb_connect_status == 1) {
			if (ihid->inbuf[2]==0x05) {
				if ((ihid->inbuf[3] & 0x40) && !spec_key_lockscreen) {
					report_special_key(SPECIAL_KEY_LOCKSCREEN,1);
					spec_key_lockscreen = true;
					return;
				}
				if (((~ihid->inbuf[3]) & 0x40) && spec_key_lockscreen) {
					report_special_key(SPECIAL_KEY_LOCKSCREEN,0);
					spec_key_lockscreen = false;
					return;
				}
				if ((ihid->inbuf[3] & 0x80) && !spec_key_switchlanguage) {
					report_special_key(SPECIAL_KEY_SWITCHLANGUAGE,1);
					spec_key_switchlanguage = true;
					return;
				}
				if (((~ihid->inbuf[3]) & 0x80) && spec_key_switchlanguage) {
					report_special_key(SPECIAL_KEY_SWITCHLANGUAGE,0);
					spec_key_switchlanguage = false;
					return;
				}
				if ((ihid->inbuf[4] & 0x01) && !spec_key_micdisable) {
					report_special_key(SPECIAL_KEY_MICDISABLE,1);
					spec_key_micdisable = true;
					return;
				}
				if (((~ihid->inbuf[4]) & 0x01) && spec_key_micdisable) {
					report_special_key(SPECIAL_KEY_MICDISABLE,0);
					spec_key_micdisable = false;
					return;
				}
				if ((ihid->inbuf[4] & 0x10) && !spec_key_touchpanelmute) {
					report_special_key(SPECIAL_KEY_TOUCHPANELMUTE,1);
					spec_key_touchpanelmute = true;
					return;
				}
				if (((~ihid->inbuf[4]) & 0x10) && spec_key_touchpanelmute) {
					report_special_key(SPECIAL_KEY_TOUCHPANELMUTE,0);
					spec_key_touchpanelmute = false;
					return;
				}
				if ((ihid->inbuf[5] & 0x02) && !spec_key_globalsearch) {
					report_special_key(SPECIAL_KEY_GLOBALSEARCH,1);
					spec_key_globalsearch = true;
					return;
				}
				if (((~ihid->inbuf[5]) & 0x02) && spec_key_globalsearch) {
					report_special_key(SPECIAL_KEY_GLOBALSEARCH,0);
					spec_key_globalsearch = false;
					return;
				}
				if ((ihid->inbuf[4] & 0x02) && !spec_key_fullscreen) {
					report_special_key(SPECIAL_KEY_FULLSCREEN,1);
					spec_key_fullscreen = true;
					return;
				}
				if (((~ihid->inbuf[4]) & 0x02) && spec_key_fullscreen) {
					report_special_key(SPECIAL_KEY_FULLSCREEN,0);
					spec_key_fullscreen = false;
					return;
				}
				if ((ihid->inbuf[4] & 0x04) && !spec_key_splitscreen) {
					report_special_key(SPECIAL_KEY_SPLITSCREEN,1);
					spec_key_splitscreen = true;
					return;
				}
				if (((~ihid->inbuf[4]) & 0x04) && spec_key_splitscreen) {
					report_special_key(SPECIAL_KEY_SPLITSCREEN,0);
					spec_key_splitscreen = false;
					return;
				}
				if ((ihid->inbuf[4] & 0x20) && !spec_key_superintcon) {
					report_special_key(SPECIAL_KEY_SUPERINTCON,1);
					spec_key_superintcon = true;
					return;
				}
				if (((~ihid->inbuf[4]) & 0x20) && spec_key_superintcon) {
					report_special_key(SPECIAL_KEY_SUPERINTCON,0);
					spec_key_superintcon = false;
					return;
				}
				if ((ihid->inbuf[4] & 0x40) && !spec_key_customerapp1) {
					report_special_key(SPECIAL_KEY_CUSTOMERAPP1,1);
					spec_key_customerapp1 = true;
					return;
				}
				if (((~ihid->inbuf[4]) & 0x40) && spec_key_customerapp1) {
					report_special_key(SPECIAL_KEY_CUSTOMERAPP1,0);
					spec_key_customerapp1 = false;
					return;
				}
				if ((ihid->inbuf[4] & 0x80) && !spec_key_customerapp2) {
					report_special_key(SPECIAL_KEY_CUSTOMERAPP2,1);
					spec_key_customerapp2 = true;
					return;
				}
				if (((~ihid->inbuf[4]) & 0x80) && spec_key_customerapp2) {
					report_special_key(SPECIAL_KEY_CUSTOMERAPP2,0);
					spec_key_customerapp2 = false;
					return;
				}
				hid_input_report(ihid->hid, HID_INPUT_REPORT, ihid->inbuf + 2,
					ret_size - 2, 1);
			} else {
				hid_input_report(ihid->hid, HID_INPUT_REPORT, ihid->inbuf + 2,
					ret_size - 2, 1);
			}
		}
	} else {
		if (test_bit(I2C_HID_STARTED, &ihid->flags))
			hid_input_report(ihid->hid, HID_INPUT_REPORT, ihid->inbuf + 2,
				ret_size - 2, 1);
	}
	/* Spruce code for OSPURCET-780 by chenzm9 at 2023/1/13 end */
	return;
}

/* Spruce code for OSPURCET-749 by chenzm9 at 2023/1/12 start */
static ssize_t kb_hall_show(struct kobject *kobj,
                       struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", device_keyboard->kb_hall);
}

/* Spruce code for OSPURCET-1347 by chenzm9 at 2023/2/17 start */
static ssize_t kb_id_show(struct kobject *kobj,
                       struct kobj_attribute *attr, char *buf)
{
        return sprintf(buf, "VID:[0x%x],PID:[0x%x]\n", g_phid->vendor, g_phid->product);
}
/* Spruce code for OSPURCET-1347 by chenzm9 at 2023/2/17 end */

static ssize_t kb_status_show(struct kobject *kobj,
                       struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", device_keyboard->kb_connect_status);
}

static struct kobj_attribute kb_hall = {
       .attr = {
                .name = "kb_hall_value",
                .mode = 0644,
                },
       .show = &kb_hall_show,
};

/* Spruce code for OSPURCET-1347 by chenzm9 at 2023/2/17 start */
static struct kobj_attribute kb_id = {
       .attr = {
                .name = "kb_vid_pid",
                .mode = 0644,
                },
       .show = &kb_id_show,
};
/* Spruce code for OSPURCET-1347 by chenzm9 at 2023/2/17 end */

static struct kobj_attribute kb_status = {
       .attr = {
                .name = "kb_status_value",
                .mode = 0644,
                },
       .show = &kb_status_show,
};

/* Spruce code for OSPURCET-1347 by chenzm9 at 2023/2/17 start */
static struct attribute *mtk_kb_status_attrs[] = {
       &kb_status.attr,
       &kb_hall.attr,
       &kb_id.attr,
       NULL
};
/* Spruce code for OSPURCET-1347 by chenzm9 at 2023/2/17 end */

static struct attribute_group mtk_kb_status_attr_group = {
      .attrs = mtk_kb_status_attrs,
};
/* Spruce code for OSPURCET-749 by chenzm9 at 2023/1/12 end */

void creat_kb_status_node(void)
{
	int m=0;
	kobject_status = kobject_create_and_add("kb_status", NULL);
	if (kobject_status)
		m = sysfs_create_group(kobject_status ,
                               &mtk_kb_status_attr_group);
	if (!kobject_status || m)
              printk("failed to create kb_status\n",__func__);
}

static irqreturn_t i2c_hid_irq(int irq, void *dev_id)
{
	struct i2c_hid *ihid = dev_id;
	printk("0401===%s===1\n",__func__);
	if (test_bit(I2C_HID_READ_PENDING, &ihid->flags))
		return IRQ_HANDLED;
	printk("0401===%s===2\n",__func__);
	i2c_hid_get_input(ihid);

	return IRQ_HANDLED;
}

static int i2c_hid_get_report_length(struct hid_report *report)
{
	return ((report->size - 1) >> 3) + 1 +
		report->device->report_enum[report->type].numbered + 2;
}

/*
 * Traverse the supplied list of reports and find the longest
 */
static void i2c_hid_find_max_report(struct hid_device *hid, unsigned int type,
		unsigned int *max)
{
	struct hid_report *report;
	unsigned int size;

	/* We should not rely on wMaxInputLength, as some devices may set it to
	 * a wrong length. */
	list_for_each_entry(report, &hid->report_enum[type].report_list, list) {
		size = i2c_hid_get_report_length(report);
		if (*max < size)
			*max = size;
	}
}

static void i2c_hid_free_buffers(struct i2c_hid *ihid)
{
	kfree(ihid->inbuf);
	kfree(ihid->rawbuf);
	kfree(ihid->argsbuf);
	kfree(ihid->cmdbuf);
	ihid->inbuf = NULL;
	ihid->rawbuf = NULL;
	ihid->cmdbuf = NULL;
	ihid->argsbuf = NULL;
	ihid->bufsize = 0;
}

static int i2c_hid_alloc_buffers(struct i2c_hid *ihid, size_t report_size)
{
	/* the worst case is computed from the set_report command with a
	 * reportID > 15 and the maximum report length */
	int args_len = sizeof(__u8) + /* ReportID */
		       sizeof(__u8) + /* optional ReportID byte */
		       sizeof(__u16) + /* data register */
		       sizeof(__u16) + /* size of the report */
		       report_size; /* report */

	ihid->inbuf = kzalloc(report_size, GFP_KERNEL);
	ihid->rawbuf = kzalloc(report_size, GFP_KERNEL);
	ihid->argsbuf = kzalloc(args_len, GFP_KERNEL);
	ihid->cmdbuf = kzalloc(sizeof(union command) + args_len, GFP_KERNEL);

	if (!ihid->inbuf || !ihid->rawbuf || !ihid->argsbuf || !ihid->cmdbuf) {
		i2c_hid_free_buffers(ihid);
		return -ENOMEM;
	}

	ihid->bufsize = report_size;

	return 0;
}

static int i2c_hid_get_raw_report(struct hid_device *hid,
		unsigned char report_number, __u8 *buf, size_t count,
		unsigned char report_type)
{
	struct i2c_client *client = hid->driver_data;
	struct i2c_hid *ihid = i2c_get_clientdata(client);
	size_t ret_count, ask_count;
	int ret;

	if (report_type == HID_OUTPUT_REPORT)
		return -EINVAL;

	/* +2 bytes to include the size of the reply in the query buffer */
	ask_count = min(count + 2, (size_t)ihid->bufsize);

	ret = i2c_hid_get_report(client,
			report_type == HID_FEATURE_REPORT ? 0x03 : 0x01,
			report_number, ihid->rawbuf, ask_count);

	if (ret < 0)
		return ret;

	ret_count = ihid->rawbuf[0] | (ihid->rawbuf[1] << 8);

	if (ret_count <= 2) {
		memset(buf, 0 ,count);
		return 0;
	}

	ret_count = min(ret_count, ask_count);

	/* The query buffer contains the size, dropping it in the reply */
	count = min(count, ret_count - 2);
	memcpy(buf, ihid->rawbuf + 2, count);

	return count;
}

static int i2c_hid_output_raw_report(struct hid_device *hid, __u8 *buf,
		size_t count, unsigned char report_type, bool use_data)
{
	struct i2c_client *client = hid->driver_data;
	struct i2c_hid *ihid = i2c_get_clientdata(client);
	int report_id = buf[0];
	int ret;
	/* Spruce code for OSPURCET-1347 by chenzm9 at 2023/02/16 start */
	printk("%s,g_screen_on = %d,device_keyboard->input_registered = %d\n",__func__,g_screen_on,device_keyboard->input_registered);
	if(!(hid->vendor==0x32CD&&hid->product==0x003A)){
//		if(g_screen_on == 0 || !device_keyboard->input_registered) {
		if(g_screen_on == 0) {
			printk("---i2c_hid screen is off or keyboard is not on position cmd is ignore---\n");
			return -EINVAL;
		}
	}
	/* Spruce code for OSPURCET-1347 by chenzm9 at 2023/02/16 end */
	if (report_type == HID_INPUT_REPORT)
		return -EINVAL;

	mutex_lock(&ihid->reset_lock);

	if (report_id) {
		buf++;
		count--;
	}

	ret = i2c_hid_set_or_send_report(client,
				report_type == HID_FEATURE_REPORT ? 0x03 : 0x02,
				report_id, buf, count, use_data);

	if (report_id && ret >= 0)
		ret++; /* add report_id to the number of transfered bytes */

	mutex_unlock(&ihid->reset_lock);

	return ret;
}

static int i2c_hid_output_report(struct hid_device *hid, __u8 *buf,
		size_t count)
{
	return i2c_hid_output_raw_report(hid, buf, count, HID_OUTPUT_REPORT,
			false);
}

static int i2c_hid_raw_request(struct hid_device *hid, unsigned char reportnum,
			       __u8 *buf, size_t len, unsigned char rtype,
			       int reqtype)
{
	switch (reqtype) {
	case HID_REQ_GET_REPORT:
		return i2c_hid_get_raw_report(hid, reportnum, buf, len, rtype);
	case HID_REQ_SET_REPORT:
		if (buf[0] != reportnum)
			return -EINVAL;
		return i2c_hid_output_raw_report(hid, buf, len, rtype, true);
	default:
		return -EIO;
	}
}

static int i2c_hid_parse(struct hid_device *hid)
{
	struct i2c_client *client = hid->driver_data;
	struct i2c_hid *ihid = i2c_get_clientdata(client);
	struct i2c_hid_desc *hdesc = &ihid->hdesc;
	unsigned int rsize;
	char *rdesc;
	int ret;
	int tries = 3;
	char *use_override;

	i2c_hid_dbg(ihid, "entering %s\n", __func__);

	rsize = le16_to_cpu(hdesc->wReportDescLength);
	if (!rsize || rsize > HID_MAX_DESCRIPTOR_SIZE) {
		dbg_hid("weird size of report descriptor (%u)\n", rsize);
		return -EINVAL;
	}

	do {
		ret = i2c_hid_hwreset(client);
		if (ret)
			msleep(1000);
	} while (tries-- > 0 && ret);

	if (ret)
		return ret;

	use_override = i2c_hid_get_dmi_hid_report_desc_override(client->name,
								&rsize);

	if (use_override) {
		rdesc = use_override;
		i2c_hid_dbg(ihid, "Using a HID report descriptor override\n");
	} else {
		rdesc = kzalloc(rsize, GFP_KERNEL);

		if (!rdesc) {
			dbg_hid("couldn't allocate rdesc memory\n");
			return -ENOMEM;
		}

		i2c_hid_dbg(ihid, "asking HID report descriptor\n");

		ret = i2c_hid_command(client, &hid_report_descr_cmd,
				      rdesc, rsize);
		if (ret) {
			hid_err(hid, "reading report descriptor failed\n");
			kfree(rdesc);
			return -EIO;
		}
	}

	i2c_hid_dbg(ihid, "Report Descriptor: %*ph\n", rsize, rdesc);

	ret = hid_parse_report(hid, rdesc, rsize);
	if (!use_override)
		kfree(rdesc);

	if (ret) {
		dbg_hid("parsing report descriptor failed\n");
		return ret;
	}

	return 0;
}

static int i2c_hid_start(struct hid_device *hid)
{
	struct i2c_client *client = hid->driver_data;
	struct i2c_hid *ihid = i2c_get_clientdata(client);
	int ret;
	unsigned int bufsize = HID_MIN_BUFFER_SIZE;

	i2c_hid_find_max_report(hid, HID_INPUT_REPORT, &bufsize);
	i2c_hid_find_max_report(hid, HID_OUTPUT_REPORT, &bufsize);
	i2c_hid_find_max_report(hid, HID_FEATURE_REPORT, &bufsize);

	if (bufsize > ihid->bufsize) {
		disable_irq(client->irq);
		i2c_hid_free_buffers(ihid);

		ret = i2c_hid_alloc_buffers(ihid, bufsize);
		enable_irq(client->irq);

		if (ret)
			return ret;
	}

	return 0;
}

static void i2c_hid_stop(struct hid_device *hid)
{
	hid->claimed = 0;
}

static int i2c_hid_open(struct hid_device *hid)
{
	struct i2c_client *client = hid->driver_data;
	struct i2c_hid *ihid = i2c_get_clientdata(client);
	int ret = 0;

	ret = pm_runtime_get_sync(&client->dev);
	if (ret < 0)
		return ret;

	set_bit(I2C_HID_STARTED, &ihid->flags);
	return 0;
}

static void i2c_hid_close(struct hid_device *hid)
{
	struct i2c_client *client = hid->driver_data;
	struct i2c_hid *ihid = i2c_get_clientdata(client);

	clear_bit(I2C_HID_STARTED, &ihid->flags);

	/* Save some power */
	pm_runtime_put(&client->dev);
}

static int i2c_hid_power(struct hid_device *hid, int lvl)
{
	struct i2c_client *client = hid->driver_data;
	struct i2c_hid *ihid = i2c_get_clientdata(client);

	i2c_hid_dbg(ihid, "%s lvl:%d\n", __func__, lvl);

	switch (lvl) {
	case PM_HINT_FULLON:
		pm_runtime_get_sync(&client->dev);
		break;
	case PM_HINT_NORMAL:
		pm_runtime_put(&client->dev);
		break;
	}
	return 0;
}

struct hid_ll_driver i2c_hid_ll_driver = {
	.parse = i2c_hid_parse,
	.start = i2c_hid_start,
	.stop = i2c_hid_stop,
	.open = i2c_hid_open,
	.close = i2c_hid_close,
	.power = i2c_hid_power,
	.output_report = i2c_hid_output_report,
	.raw_request = i2c_hid_raw_request,
};
EXPORT_SYMBOL_GPL(i2c_hid_ll_driver);

static int i2c_hid_init_irq(struct i2c_client *client)
{
	struct i2c_hid *ihid = i2c_get_clientdata(client);
	unsigned long irqflags = 0;
	int ret;

	dev_err(&client->dev, "Requesting IRQ: %d\n", client->irq);

	if (!irq_get_trigger_type(client->irq))
		irqflags = IRQF_TRIGGER_LOW;

	ret = request_threaded_irq(client->irq, NULL, i2c_hid_irq,
				   irqflags | IRQF_ONESHOT, client->name, ihid);
	if (ret < 0) {
		dev_warn(&client->dev,
			"Could not register for %s interrupt, irq = %d,"
			" ret = %d\n",
			client->name, client->irq, ret);

		return ret;
	}
	enable_irq_wake(client->irq);
	device_init_wakeup(&client->dev, true);
	return 0;
}

static int i2c_hid_fetch_hid_descriptor(struct i2c_hid *ihid)
{
	struct i2c_client *client = ihid->client;
	struct i2c_hid_desc *hdesc = &ihid->hdesc;
	unsigned int dsize;
	int ret;

	/* i2c hid fetch using a fixed descriptor size (30 bytes) */
	if (i2c_hid_get_dmi_i2c_hid_desc_override(client->name)) {
		i2c_hid_dbg(ihid, "Using a HID descriptor override\n");
		ihid->hdesc =
			*i2c_hid_get_dmi_i2c_hid_desc_override(client->name);
	} else {
		i2c_hid_dbg(ihid, "Fetching the HID descriptor\n");
		ret = i2c_hid_command(client, &hid_descr_cmd,
				      ihid->hdesc_buffer,
				      sizeof(struct i2c_hid_desc));
		if (ret) {
			dev_err(&client->dev, "hid_descr_cmd failed\n");
			return -ENODEV;
		}
	}

	/* Validate the length of HID descriptor, the 4 first bytes:
	 * bytes 0-1 -> length
	 * bytes 2-3 -> bcdVersion (has to be 1.00) */
	/* check bcdVersion == 1.0 */
	if (le16_to_cpu(hdesc->bcdVersion) != 0x0100) {
		dev_err(&client->dev,
			"unexpected HID descriptor bcdVersion (0x%04hx)\n",
			le16_to_cpu(hdesc->bcdVersion));
		return -ENODEV;
	}

	/* Descriptor length should be 30 bytes as per the specification */
	dsize = le16_to_cpu(hdesc->wHIDDescLength);
	if (dsize != sizeof(struct i2c_hid_desc)) {
		dev_err(&client->dev, "weird size of HID descriptor (%u)\n",
			dsize);
		return -ENODEV;
	}
	i2c_hid_dbg(ihid, "HID Descriptor: %*ph\n", dsize, ihid->hdesc_buffer);
	return 0;
}

#ifdef CONFIG_ACPI
static int i2c_hid_acpi_pdata(struct i2c_client *client,
		struct i2c_hid_platform_data *pdata)
{
	static guid_t i2c_hid_guid =
		GUID_INIT(0x3CDFF6F7, 0x4267, 0x4555,
			  0xAD, 0x05, 0xB3, 0x0A, 0x3D, 0x89, 0x38, 0xDE);
	union acpi_object *obj;
	struct acpi_device *adev;
	acpi_handle handle;

	handle = ACPI_HANDLE(&client->dev);
	if (!handle || acpi_bus_get_device(handle, &adev))
		return -ENODEV;

	obj = acpi_evaluate_dsm_typed(handle, &i2c_hid_guid, 1, 1, NULL,
				      ACPI_TYPE_INTEGER);
	if (!obj) {
		dev_err(&client->dev, "device _DSM execution failed\n");
		return -ENODEV;
	}

	pdata->hid_descriptor_address = obj->integer.value;
	ACPI_FREE(obj);

	return 0;
}

static void i2c_hid_acpi_fix_up_power(struct device *dev)
{
	acpi_handle handle = ACPI_HANDLE(dev);
	struct acpi_device *adev;

	if (handle && acpi_bus_get_device(handle, &adev) == 0)
		acpi_device_fix_up_power(adev);
}

static const struct acpi_device_id i2c_hid_acpi_match[] = {
	{"ACPI0C50", 0 },
	{"PNP0C50", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, i2c_hid_acpi_match);
#else
static inline int i2c_hid_acpi_pdata(struct i2c_client *client,
		struct i2c_hid_platform_data *pdata)
{
	return -ENODEV;
}

static inline void i2c_hid_acpi_fix_up_power(struct device *dev) {}
#endif

static bool is_mcu_init = false;
static int  i2c_enable_mcu(struct i2c_client *client,
		struct i2c_hid_platform_data *pdata)
{
	int ret =0;
	int mcu_gpio_value = 0;
#if 0
	ret = gpio_request(mcu_en_gpio, "mcu_en_gpio");
	if(ret)
	{
		pr_err("%s: request mcu_en_gpio failed\n", __func__);
		goto free_en_gpio;
	}
#endif
	mcu_gpio_value = gpio_get_value(mcu_en_gpio);
	pr_err("%s:mcu_gpio value is %d before setting as 1\n", __func__,mcu_gpio_value);
#if 0
	ret = gpio_direction_output(mcu_en_gpio, 1);

	if(ret)
	{
		pr_err("%s: set mcu_en_gpio failed\n", __func__);
		goto free_en_gpio;
	}
	else
	{
		printk("---qzr test set mcu_en_gpio success---");
	}
#endif
	msleep(100);

	/*Spruce code for OSPURCET-1235 by chenzm9 at 2023/2/15 start*/
	ret = gpio_request(mcu_resume_gpio, "mcu_resume_gpio");
        if(ret)
        {
                pr_err("%s: request mcu_resume_gpio failed\n", __func__);
                goto free_resume_gpio;
        }
	/*Spruce code for OSPURCET-1235 by chenzm9 at 2023/2/15 end*/

	ret = gpio_request(mcu_rst_gpio, "mcu_rst_gpio");
	if(ret)
	{
		pr_err("%s: request mcu_rst_gpio failed\n", __func__);
		goto free_rst_gpio;
	}
	ret = gpio_direction_output(mcu_rst_gpio, 1);

	if(ret)
	{
		pr_err("%s: set mcu_rst_gpio failed\n", __func__);
		goto free_rst_gpio;
	}

	pr_err("%s:mcu_gpio value is %d after setting as 1\n", __func__,mcu_gpio_value);

	return 0;
	free_rst_gpio:
		gpio_free(mcu_rst_gpio);
	/*Spruce code for OSPURCET-1235 by chenzm9 at 2023/2/15 start*/
	free_resume_gpio:
		gpio_free(mcu_resume_gpio);
	/*Spruce code for OSPURCET-1235 by chenzm9 at 2023/2/15 end*/
	//free_en_gpio:
		//gpio_free(mcu_en_gpio);
		return ret;
}

#ifdef CONFIG_OF
static int i2c_hid_of_probe(struct i2c_client *client,
		struct i2c_hid_platform_data *pdata)
{
	struct device *dev = &client->dev;
	u32 val;
	int ret;
	printk(KERN_CRIT "%s:\n", __func__);
	ret = of_property_read_u32(dev->of_node, "hid-descr-addr", &val);
	if (ret) {
		dev_err(&client->dev, "HID register address not provided\n");
		return -ENODEV;
	}
	if (val >> 16) {
		dev_err(&client->dev, "Bad HID register address: 0x%08x\n",
			val);
		return -EINVAL;
	}
	pdata->hid_descriptor_address = val;

	ret = of_property_read_u32(dev->of_node, "post-power-on-delay-ms",
				   &val);
	if (!ret)
		pdata->post_power_delay_ms = val;

	ret = of_get_named_gpio(dev->of_node, "mcu_en_gpio", 0);
	if (ret < 0 )
	{
		pr_err("%s:Not support pogo keyboard and mouse\n", __func__);
		return -ENODEV;
	}
	else{
		mcu_en_gpio = ret;
	}
	/*Spruce code for OSPURCET-1235 by chenzm9 at 2023/2/15 start*/
	ret = of_get_named_gpio(dev->of_node, "mcu_rst_gpio", 0);
	if (ret < 0 )
	{
		pr_err("%s:Not support pogo keyboard and mouse rst pin\n", __func__);
		return -ENODEV;
	}
	else {
		mcu_rst_gpio = ret;
	}

	ret = of_get_named_gpio(dev->of_node, "mcu_resume_gpio", 0);
        if (ret < 0 )
        {
                pr_err("%s:Not support pogo keyboard and mouse resume pin\n", __func__);
                return -ENODEV;
        }
        else {
                mcu_resume_gpio = ret;
        }

	printk("%s: mcu_en_gpio %d,mcu_rst_gpio=%d,mcu_resume_gpio=%d,post_power_delay_ms=%d\n", __func__,
		mcu_en_gpio,mcu_rst_gpio,mcu_resume_gpio,pdata->post_power_delay_ms);
	/*Spruce code for OSPURCET-1235 by chenzm9 at 2023/2/15 end*/

        creat_kb_status_node();

	return 0;
}

static const struct of_device_id i2c_hid_of_match[] = {
	{ .compatible = "hid-over-i2c" },
	{},
};
MODULE_DEVICE_TABLE(of, i2c_hid_of_match);
#else
static inline int i2c_hid_of_probe(struct i2c_client *client,
		struct i2c_hid_platform_data *pdata)
{
	return -ENODEV;
}
#endif

static int i2c_hid_probe(struct i2c_client *client,
			 const struct i2c_device_id *dev_id)
{
	int ret,i2c_wait=0;
	struct i2c_hid *ihid;
	struct hid_device *hid;
	__u16 hidRegister;
	struct i2c_hid_platform_data *platform_data = client->dev.platform_data;

	printk("HID probe called for i2c 0x%02x\n", client->addr);

//	if((get_boot_mode() == KERNEL_POWER_OFF_CHARGING_BOOT) || 
//		(get_boot_mode() == LOW_POWER_OFF_CHARGING_BOOT)) {
//		pr_err("power off charge,no need keyboard.\n");
//		return -ENODEV;
//	}
	if (!client->irq) {
		dev_err(&client->dev,
			"HID over i2c has not been provided an Int IRQ\n");
		return -EINVAL;
	}

	if (client->irq < 0) {
		if (client->irq != -EPROBE_DEFER)
			dev_err(&client->dev,
				"HID over i2c doesn't have a valid IRQ\n");
		return client->irq;
	}

	ihid = kzalloc(sizeof(struct i2c_hid), GFP_KERNEL);
	if (!ihid)
		return -ENOMEM;

	device_keyboard = kzalloc(sizeof(struct hid_device_keyboard), GFP_KERNEL);
	if (!device_keyboard)
		return -ENOMEM;
	device_keyboard->kb_connect_status = 0;
	/* Spruce code for OSPURCET-749 by chenzm9 at 2023/1/12 start */
	device_keyboard->kb_hall = 0;
	/* Spruce code for OSPURCET-749 by chenzm9 at 2023/1/12 end */

	if (client->dev.of_node) {
		ret = i2c_hid_of_probe(client, &ihid->pdata);
		if (ret)
			goto err;
	} else if (!platform_data) {
		ret = i2c_hid_acpi_pdata(client, &ihid->pdata);
		if (ret) {
			dev_err(&client->dev,
				"HID register address not provided\n");
			goto err;
		}
	} else {
		ihid->pdata = *platform_data;
	}
	if(!is_mcu_init)
	{
		ret = i2c_enable_mcu(client, &ihid->pdata);
		if (ret)
			goto err;
	}
	is_mcu_init = true;  //gpio was set!

	if (ihid->pdata.post_power_delay_ms)
		msleep(ihid->pdata.post_power_delay_ms);

	i2c_set_clientdata(client, ihid);

	ihid->client = client;

	hidRegister = ihid->pdata.hid_descriptor_address;
	ihid->wHIDDescRegister = cpu_to_le16(hidRegister);

	init_waitqueue_head(&ihid->wait);
	mutex_init(&ihid->reset_lock);
	/* we need to allocate the command buffer without knowing the maximum
	 * size of the reports. Let's use HID_MIN_BUFFER_SIZE, then we do the
	 * real computation later. */
	ret = i2c_hid_alloc_buffers(ihid, HID_MIN_BUFFER_SIZE);
	if (ret < 0)
		goto err_regulator;

	i2c_hid_acpi_fix_up_power(&client->dev);

	pm_runtime_get_noresume(&client->dev);
	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	device_enable_async_suspend(&client->dev);
	/* Make sure there is something at this address */
	while(i2c_wait < 3)
	{
		printk("%s:%d\n",__func__,i2c_wait);
		ret = i2c_smbus_read_byte(client);
		if (ret < 0) {
			dev_err(&client->dev, "nothing at this address: %d\n", ret);
			//ret = -ENXIO;
			//goto err_pm;
			//msleep(15);
			//i2c_wait ++;
			//continue;
		}
		printk("%s:%d\n",__func__,i2c_wait);
		ret = i2c_hid_fetch_hid_descriptor(ihid);
		if (ret < 0)
		{
			//goto err_pm;
			msleep(15);
			i2c_wait ++;
		}
		else
		{
			printk("i2c success,retry count is:%d\n",i2c_wait);
			break;
		}
	}
	if(i2c_wait >= 3)
	{
		printk("i2c error,retry count is:%d\n",i2c_wait);
		ret = -ENXIO;
		goto err_pm;
	}
	ret = i2c_hid_init_irq(client);
	if (ret < 0)
		goto err_pm;
	hid = hid_allocate_device();
	if (IS_ERR(hid)) {
		ret = PTR_ERR(hid);
		goto err_irq;
	}
	g_phid = hid;
	ihid->hid = hid;

	hid->driver_data = client;
	hid->ll_driver = &i2c_hid_ll_driver;
	hid->dev.parent = &client->dev;
	hid->bus = BUS_I2C;
	hid->version = le16_to_cpu(ihid->hdesc.bcdVersion);
	hid->vendor = le16_to_cpu(ihid->hdesc.wVendorID);
	hid->product = le16_to_cpu(ihid->hdesc.wProductID);
	device_keyboard->input_registered = false;

	printk("%s:VID:[0x%x],PID:[0x%x]\n",__func__,hid->vendor,hid->product);

	if (hid->product == 0x3164){
		printk("%s:failed,because of old mouse fireware",__func__);
		//regulator_disable(ihid->pdata.supply);
		ret = -ENODEV;
		goto err_irq;
	}
	if (hid->product == 0x6103){
		printk("%s:failed,because of old kb fireware",__func__);
		ret = -ENODEV;
		goto err_irq;
	}
	/* Spruce code for OSPURCET-780 by chenzm9 at 2023/1/13 start */
	if (hid->vendor==0x17EF&&hid->product==0x6175){
		snprintf(hid->name, sizeof(hid->name), "Lenovo Keyboard Pack for Spruce");
		register_kb_wakeup_devices();
		g_client = client;
	} else if((hid->vendor==0x04F3&&hid->product==0x31A8) || (hid->vendor==0x32CD&&hid->product==0x003A)){
		snprintf(hid->name, sizeof(hid->name), "%s %04hX:%04hX Spruce keyboard",
		 client->name, hid->vendor, hid->product);
		register_kb_wakeup_devices();
	/* Spruce code for OSPURCET-1227 by sunft3 at 2023/02/10 start */
		g_client = client;
	/* Spruce code for OSPURCET-1227 by sunft3 at 2023/02/10 end */
	} else {
		snprintf(hid->name, sizeof(hid->name), "%s %04hX:%04hX",
		 client->name, hid->vendor, hid->product);
	}
	/* Spruce code for OSPURCET-780 by chenzm9 at 2023/1/13 end */
	printk(KERN_ERR "hid->version_id=0x%x ",le16_to_cpu(ihid->hdesc.wVersionID));

	strlcpy(hid->phys, dev_name(&client->dev), sizeof(hid->phys));

	ihid->quirks = i2c_hid_lookup_quirk(hid->vendor, hid->product);
	ret = hid_add_device(hid);
	if (ret) {
		if (ret != -ENODEV)
			hid_err(client, "can't add hid device: %d\n", ret);
		goto err_mem_free;
	}
	/* Spruce code for OSPURCET-1227 by sunft3 at 2023/02/10 start */
	device_keyboard->input_registered = true;
	/* Spruce code for OSPURCET-1227 by sunft3 at 2023/02/10 end */
	if (!(ihid->quirks & I2C_HID_QUIRK_NO_RUNTIME_PM))
		pm_runtime_put(&client->dev);

/*	if(hid->vendor==0x17EF&&hid->product==0x613D){
		INIT_DELAYED_WORK(&device_keyboard->connection_work, hidinput_connection_worker);
		schedule_delayed_work(&device_keyboard->connection_work,msecs_to_jiffies(100));
	}
*/
	printk("%s,success\n",__func__);
	return 0;

err_mem_free:
	hid_destroy_device(hid);

err_irq:
	free_irq(client->irq, ihid);

err_pm:
	pm_runtime_put_noidle(&client->dev);
	pm_runtime_disable(&client->dev);

err_regulator:
	//regulator_disable(ihid->pdata.supply);
	gpio_free(mcu_rst_gpio);
	/*Spruce code for OSPURCET-1235 by chenzm9 at 2023/2/15 start*/
	gpio_free(mcu_resume_gpio);
	/*Spruce code for OSPURCET-1235 by chenzm9 at 2023/2/15 end*/
	//gpio_free(mcu_en_gpio);

err:
	i2c_hid_free_buffers(ihid);
	kfree(ihid);
	return ret;
}

static int i2c_hid_remove(struct i2c_client *client)
{
	struct i2c_hid *ihid = i2c_get_clientdata(client);
	struct hid_device *hid;

	if (!(ihid->quirks & I2C_HID_QUIRK_NO_RUNTIME_PM))
		pm_runtime_get_sync(&client->dev);
	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);
	pm_runtime_put_noidle(&client->dev);

	hid = ihid->hid;
	hid_destroy_device(hid);

	/*Spruce code for OSPURCET-1235 by chenzm9 at 2023/2/15 start*/
	gpio_free(mcu_rst_gpio);
	gpio_free(mcu_resume_gpio);
	/*Spruce code for OSPURCET-1235 by chenzm9 at 2023/2/15 end*/

	free_irq(client->irq, ihid);

	if (ihid->bufsize)
		i2c_hid_free_buffers(ihid);

	//regulator_disable(ihid->pdata.supply);

	kfree(ihid);

	return 0;
}

static void i2c_hid_shutdown(struct i2c_client *client)
{
	struct i2c_hid *ihid = i2c_get_clientdata(client);

	/*Spruce code for OSPURCET-1235 by chenzm9 at 2023/2/16 start*/
	kb_i2c_hid_suspend();
	/*Spruce code for OSPURCET-1235 by chenzm9 at 2023/2/16 end*/
	free_irq(client->irq, ihid);
}

#ifdef CONFIG_PM_SLEEP
static int i2c_hid_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct i2c_hid *ihid = i2c_get_clientdata(client);
	struct hid_device *hid = ihid->hid;
	int ret;
	int wake_status;

	printk(KERN_ERR "=== %s ===\n",__func__);
	if (hid->driver && hid->driver->suspend) {
		/*
		 * Wake up the device so that IO issues in
		 * HID driver's suspend code can succeed.
		 */
		ret = pm_runtime_resume(dev);
		if (ret < 0)
			return ret;

		ret = hid->driver->suspend(hid, PMSG_SUSPEND);
		if (ret < 0)
			return ret;
	}

	if (!pm_runtime_suspended(dev)) {
		/* Save some power */
		/*Spruce code for OSPURCET-1235 by chenzm9 at 2023/2/16 start*/
		kb_i2c_hid_suspend();
		/*Spruce code for OSPURCET-1235 by chenzm9 at 2023/2/16 end*/

		disable_irq(client->irq);
	}

	if (device_may_wakeup(&client->dev)) {
		wake_status = enable_irq_wake(client->irq);
		if (!wake_status)
			ihid->irq_wake_enabled = true;
		else
			hid_warn(hid, "Failed to enable irq wake: %d\n",
				wake_status);
	} else {
//		ret = regulator_disable(ihid->pdata.supply);
//		if (ret < 0)
//			hid_warn(hid, "Failed to disable supply: %d\n", ret);
	}

	return 0;
}

static int i2c_hid_resume(struct device *dev)
{
	int ret;
	struct i2c_client *client = to_i2c_client(dev);
	struct i2c_hid *ihid = i2c_get_clientdata(client);
	struct hid_device *hid = ihid->hid;
	int wake_status;

	printk(KERN_ERR "=== %s ===!\n",__func__);
	if (!device_may_wakeup(&client->dev)) {
		//ret = regulator_enable(ihid->pdata.supply);
		//if (ret < 0)
		//	hid_warn(hid, "Failed to enable supply: %d\n", ret);
		if (ihid->pdata.post_power_delay_ms)
			msleep(ihid->pdata.post_power_delay_ms);
	} else if (ihid->irq_wake_enabled) {
		wake_status = disable_irq_wake(client->irq);
		if (!wake_status)
			ihid->irq_wake_enabled = false;
		else
			hid_warn(hid, "Failed to disable irq wake: %d\n",
				wake_status);
	}

	/* We'll resume to full power */
	pm_runtime_disable(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	enable_irq(client->irq);
	ret = i2c_hid_hwreset(client);
	if (ret)
		return ret;

	if (hid->driver && hid->driver->reset_resume) {
		ret = hid->driver->reset_resume(hid);
		return ret;
	}

	return 0;
}
#endif

#ifdef CONFIG_PM
static int i2c_hid_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	/*Spruce code for OSPURCET-1235 by chenzm9 at 2023/2/16 start*/
	kb_i2c_hid_suspend();
	/*Spruce code for OSPURCET-1235 by chenzm9 at 2023/2/16 end*/
	disable_irq(client->irq);
	return 0;
}

static int i2c_hid_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	enable_irq(client->irq);
	/*Spruce code for OSPURCET-1235 by chenzm9 at 2023/2/16 start*/
	kb_i2c_hid_resume();
	/*Spruce code for OSPURCET-1235 by chenzm9 at 2023/2/16 end*/
	return 0;
}

/*Spruce code for OSPURCET-1235 by chenzm9 at 2023/2/16 start*/
static int kb_i2c_hid_resume(void)
{
	int ret = 0;

	if (!g_already_sleep) {
		return -1;
	}

	ret = gpio_direction_output(mcu_resume_gpio, 1);
        if(ret) {
                pr_err("%s: set mcu_resume_gpio failed\n", __func__);
                return ret;
        }

	ret = i2c_hid_set_power(g_client, I2C_HID_PWR_ON);
	if (ret) {
		pr_err("%s: i2c set power on fail\n", __func__);
		return ret;
	}

	g_already_sleep = false;

	return 0;
}

static int kb_i2c_hid_suspend(void)
{
	int ret = 0;

	if (g_already_sleep) {
		return -1;
	}

	ret = gpio_direction_output(mcu_resume_gpio, 0);
        if(ret) {
                pr_err("%s: set mcu_resume_gpio failed\n", __func__);
                return ret;
        }

	ret = i2c_hid_set_power(g_client, I2C_HID_PWR_SLEEP);
	if (ret) {
		pr_err("%s: i2c set sleep fail\n", __func__);
		return ret;
	}

	g_already_sleep = true;

	return 0;
}
/*Spruce code for OSPURCET-1235 by chenzm9 at 2023/2/16 end*/

void kb_hid_suspend(void)
{
	int ret = 0;
	int args_len = 6;
	u8 args[6] = {07,00,04,00,00,05};

	if (!g_client)
		return;

	printk("-----hid suspend-----\n");

	ret = __i2c_hid_command(g_client, &hid_set_report_cmd, 2, 2, args, args_len,
				NULL, 0);
	if (ret)
		printk("%s:report screen off cmd failed\n");
	else
		g_screen_on = 0;

	/*Spruce code for OSPURCET-1235 by chenzm9 at 2023/2/16 start*/
	kb_i2c_hid_suspend();
	/*Spruce code for OSPURCET-1235 by chenzm9 at 2023/2/16 end*/

	return;
}
void kb_hid_resume(void)
{
	int ret = 0;
	int args_len = 6;
	u8 args[6] = {07,00,04,00,01,05};

	if (!g_client)
		return;

	printk("-----hid resume-----\n");
	/*Spruce code for OSPURCET-1235 by chenzm9 at 2023/2/16 start*/
	kb_i2c_hid_resume();
	/*Spruce code for OSPURCET-1235 by chenzm9 at 2023/2/16 end*/

	ret = __i2c_hid_command(g_client, &hid_set_report_cmd, 2, 2, args, args_len,
				NULL, 0);
	if (ret)
		printk("%s:report screen on cmd failed\n");
	else
		g_screen_on = 1;
	/* Spruce code for OSPURCET-780 by sunft3 at 2023/01/18 end */
	return;
}
#endif

static const struct dev_pm_ops i2c_hid_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(i2c_hid_suspend, i2c_hid_resume)
	SET_RUNTIME_PM_OPS(i2c_hid_runtime_suspend, i2c_hid_runtime_resume,
			   NULL)
};

static const struct i2c_device_id i2c_hid_id_table[] = {
	{ "hid", 0 },
	{ "hid-over-i2c", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, i2c_hid_id_table);


static struct i2c_driver i2c_hid_driver = {
	.driver = {
		.name	= "i2c_hid",
		.pm	= &i2c_hid_pm,
		.acpi_match_table = ACPI_PTR(i2c_hid_acpi_match),
		.of_match_table = of_match_ptr(i2c_hid_of_match),
	},

	.probe		= i2c_hid_probe,
	.remove		= i2c_hid_remove,
	.shutdown	= i2c_hid_shutdown,
	.id_table	= i2c_hid_id_table,
};

module_i2c_driver(i2c_hid_driver);

MODULE_DESCRIPTION("HID over I2C core driver");
MODULE_AUTHOR("Benjamin Tissoires <benjamin.tissoires@gmail.com>");
MODULE_LICENSE("GPL");
