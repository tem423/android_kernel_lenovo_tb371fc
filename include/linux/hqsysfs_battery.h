/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef __HQ_SYSFS_HEAD__
#define __HQ_SYSFS_HEAD__

#include <linux/module.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/kfifo.h>

#include <linux/firmware.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/of.h>
#ifdef CONFIG_OF
#include <linux/of_fdt.h>
#endif
#include <linux/atomic.h>

#define MAX_HW_DEVICE_NAME (64)

enum hardware_id{
	HWID_NONE = 0x00,
	HWID_DDR = 0x10,
	HWID_EMMC,
	HWID_NAND,
	HWID_UFS,
	HWID_UFS_MORE,
	HWID_UFS_WP,
	HWID_EFUSE,

	HWID_LCM = 0x20,
	HWID_SUB_LCM,
	HWID_BIAS_IC,
	HWID_CTP,

	HWID_MAIN_CAM = 0x30,
	HWID_WIDE_CAM,
	HWID_FRONT_CAM,
	HWID_MICRO_CAM,
	HWID_DEPTH_CAM,
	HWID_FLASHLIGHT,

	HWID_GSENSOR = 0x70,
	HWID_ALSPS,
	HWID_GYRO,
	HWID_MSENSOR,
	HWID_SARSENSOR,
	HWID_IRDA,
	HWID_BAROMETER,
	HWID_PEDOMETER,
	HWID_HUMIDITY,

	HWID_PCBA = 0x80,

	HWID_BATTERY = 0xA0,
	HWID_SMARTPA,
/*Linden code for JLINDEN-334 by zhangjiayu5 at 20221129 start*/
	HWID_AUDIO_CODEC,
/*Linden code for JLINDEN-334 by zhangjiayu5 at 20221129 end*/
	HWID_VIBRATOR,

	HWID_NFC = 0xC0,
	HWID_FP,

	HWID_WIFI = 0xD0,
	HWID_BT,
	HWID_FM,
	HWID_GPS,

	HWID_USB_TYPE_C = 0xE0,

	HWID_SUMMARY = 0xF0,
	HWID_VER,
/*Linden code for JLINDEN-17 by kangkai4 at 20221128 start*/
	HWID_MASTER_CHARGE,
	HWID_SLAVE_CHARGE,
/*Linden code for JLINDEN-17 by kangkai4 at 20221128 end*/
	HWID_END
};


struct hw_info{
	enum hardware_id hw_id;
	struct attribute attr;
	unsigned int hw_exist;
//	const char *hw_type_name;
	char *hw_device_name;
};


#define __INFO(_id, _hw_type_name) {				\
		.hw_id = _id,				\
		.attr = {.name = __stringify(_hw_type_name),				\
		 		.mode = VERIFY_OCTAL_PERMISSIONS(S_IWUSR|S_IRUGO) },		\
		.hw_exist	= 0,						\
		.hw_device_name	= NULL,						\
	}


#define HW_INFO(_id, _hw_type_name) \
	struct hw_info hw_info_##_hw_type_name = __INFO(_id, _hw_type_name)



#define HWINFO_CLASS_NAME       "hardinfo"
#define HWINFO_INTERFACE_NAME   "interface"
#define HWINFO_HWID_NAME        "hardware_info"

int hq_register_hw_info(enum hardware_id id,char *device_name);
int hq_deregister_hw_info(enum hardware_id id,char *device_name);
int register_kboj_under_hqsysfs(struct kobject *kobj, struct kobj_type *ktype,
			const char *fmt, ...);

#endif
