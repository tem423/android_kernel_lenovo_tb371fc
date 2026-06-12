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

#include <linux/hqsysfs.h>

//#include "hqsys_misc.h"
//#include "hqsys_pcba.h"
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/gpio.h>

/*#define GPIO_CHIP_BASE       1134

#define SDCARD_DET_N_GPIO    69*/
#define HQ_SYS_FS_VER        "2022-02-25 V0.4"
#define BOARD_INFO_LEN 50
char boardinfo[BOARD_INFO_LEN] = {0};

static HW_INFO(HWID_VER, ver);
static HW_INFO(HWID_SUMMARY, hw_summary);
static HW_INFO(HWID_DDR, ram);
static HW_INFO(HWID_EMMC, emmc);
static HW_INFO(HWID_UFS, ufs);
static HW_INFO(HWID_UFS_MORE, ufs_more);
static HW_INFO(HWID_UFS_WP, ufs_wp);
static HW_INFO(HWID_EFUSE, efuse);
static HW_INFO(HWID_LCM, lcm);
static HW_INFO(HWID_CTP, ctp);
static HW_INFO(HWID_MAIN_CAM, main_cam);
static HW_INFO(HWID_WIDE_CAM, wide_cam);
static HW_INFO(HWID_DEPTH_CAM, depth_cam);
static HW_INFO(HWID_MICRO_CAM, micro_cam);
static HW_INFO(HWID_FRONT_CAM, front_cam);
static HW_INFO(HWID_GSENSOR, gsensor);
static HW_INFO(HWID_ALSPS, alsps);
static HW_INFO(HWID_MSENSOR, msensor);
static HW_INFO(HWID_GYRO, gyro);
static HW_INFO(HWID_SARSENSOR, sarsensor);

static HW_INFO(HWID_BATTERY, battery_manufacturer);
static HW_INFO(HWID_SMARTPA, smartpa);
/*Linden code for JLINDEN-334 by zhangjiayu5 at 20221129 start*/
static HW_INFO(HWID_AUDIO_CODEC, audio_codec);
/*Linden code for JLINDEN-334 by zhangjiayu5 at 20221129 end*/
static HW_INFO(HWID_VIBRATOR, vibrator);
static HW_INFO(HWID_NFC, nfc);
static HW_INFO(HWID_FP, fingerprint);
static HW_INFO(HWID_WIFI, wifi);
static HW_INFO(HWID_BT, bt);
static HW_INFO(HWID_FM, fm);
static HW_INFO(HWID_GPS, gps);
static HW_INFO(HWID_PCBA, pcba_info);
/*Linden code for JLINDEN-17 by kangkai4 at 20221128 start*/
static HW_INFO(HWID_MASTER_CHARGE, master_charge);
static HW_INFO(HWID_SLAVE_CHARGE, slave_charge);
/*Linden code for JLINDEN-17 by kangkai4 at 20221128 end*/

static struct attribute *huaqin_attrs[] = {
	&hw_info_ver.attr,
	&hw_info_hw_summary.attr,
	&hw_info_ram.attr,
	&hw_info_emmc.attr,
	&hw_info_ufs.attr,
	&hw_info_ufs_more.attr,
	&hw_info_ufs_wp.attr,
	&hw_info_efuse.attr,
	&hw_info_lcm.attr,
	&hw_info_ctp.attr,
	&hw_info_main_cam.attr,
	&hw_info_wide_cam.attr,
	&hw_info_depth_cam.attr,
	&hw_info_micro_cam.attr,
	&hw_info_front_cam.attr,
	&hw_info_gsensor.attr,
	&hw_info_alsps.attr,
	&hw_info_msensor.attr,
	&hw_info_gyro.attr,
	&hw_info_sarsensor.attr,
	&hw_info_battery_manufacturer.attr,
	&hw_info_smartpa.attr,
/*Linden code for JLINDEN-334 by zhangjiayu5 at 20221129 start*/
	&hw_info_audio_codec.attr,
/*Linden code for JLINDEN-334 by zhangjiayu5 at 20221129 end*/
	&hw_info_vibrator.attr,
	&hw_info_nfc.attr,
	&hw_info_fingerprint.attr,
	&hw_info_wifi.attr,
	&hw_info_bt.attr,
	&hw_info_fm.attr,
	&hw_info_gps.attr,
	&hw_info_pcba_info.attr,
/*Linden code for JLINDEN-17 by kangkai4 at 20221128 start*/
	&hw_info_master_charge.attr,
	&hw_info_slave_charge.attr,
/*Linden code for JLINDEN-17 by kangkai4 at 20221128 end*/
	NULL
};

unsigned int round_KB_to_readable_MB(unsigned int k){
	unsigned int r_size_m = 0;
	unsigned int in_mega = k/1024;

	if (in_mega > 64*1024){  //if memory is larger than 64G
		r_size_m = 128*1024; // It should be 128G
	} else if (in_mega > 32*1024){
		r_size_m = 64*1024;  // It should be 64G
	} else if (in_mega > 16*1024){
		r_size_m = 32*1024;
	} else if (in_mega > 8*1024){
		r_size_m = 16*1024;
	} else if (in_mega > 6*1024){
		r_size_m = 8*1024;
	} else if (in_mega > 4*1024){
		r_size_m = 6*1024;  // RAM may be 6G
	} else if (in_mega > 3*1024){
		r_size_m = 4*1024;
	} else{
		k = 0;
	}

	return r_size_m;
}
EXPORT_SYMBOL(round_KB_to_readable_MB);

static ssize_t huaqin_show(struct kobject *kobj, struct attribute *a, char *buf)
{
	ssize_t count = 0;
	struct hw_info *hw = container_of(a, struct hw_info , attr);

	if (NULL == hw)
		return sprintf(buf, "Data error\n");

	if (HWID_VER == hw->hw_id) {
		count = sprintf(buf, "%s\n", HQ_SYS_FS_VER);
	} else if (HWID_SUMMARY == hw->hw_id) {
		//iterate all device and output the detail
		int iterator = 0;
		struct hw_info *curent_hw = NULL;
		struct attribute *attr = huaqin_attrs[iterator];

		while(attr){
			curent_hw = container_of(attr, struct hw_info , attr);
			iterator += 1;
			attr = huaqin_attrs[iterator];

			if(curent_hw->hw_exist && (NULL != curent_hw->hw_device_name)){
				count += sprintf(buf+count, "%s: %s\n",
						curent_hw->attr.name,curent_hw->hw_device_name);
			}
		}

	} else {
		if (0 == hw->hw_exist)
/*Linden code for JLINDEN-1380 by liuxg9 at 20230118 start*/
			count = sprintf(buf, "fail\n");
/*Linden code for JLINDEN-1380 by liuxg9 at 20230118 end*/
		else if(NULL == hw->hw_device_name)
			count = sprintf(buf, "Installed with no device Name\n");
		else
			count = sprintf(buf, "%s\n" ,hw->hw_device_name);
	}

	return count;
}

static ssize_t huaqin_store(struct kobject *kobj, struct attribute *a,
		const char *buf, size_t count)
{
	return count;
}

/* huaqin object */
static struct kobject huaqin_kobj;
static const struct sysfs_ops huaqin_sysfs_ops = {
	.show = huaqin_show,
	.store = huaqin_store,
};

/* huaqin type */
static struct kobj_type huaqin_ktype = {
	.sysfs_ops = &huaqin_sysfs_ops,
	.default_attrs = huaqin_attrs
};

/* huaqin device class */
static struct class  *huaqin_class;
static struct device *huaqin_hw_device;


int register_kboj_under_hqsysfs(struct kobject *kobj, struct kobj_type *ktype,
			const char *fmt, ...){
	return kobject_init_and_add(kobj, ktype, &(huaqin_hw_device->kobj), fmt);
}

static int __init boardid_setup(char *str)
{
    if (str != NULL) {
        if (strlen(str) < BOARD_INFO_LEN) {
		    strncpy(boardinfo, str, strlen(str));
	    }
	} else {
		pr_err("%s fail to get board_info, str is null\n",__func__);
	}

	return 1;
}

static int __init create_sysfs(void)
{
	int ret;
	/* create class (device model) */
	huaqin_class = class_create(THIS_MODULE, HWINFO_CLASS_NAME);
	if (IS_ERR(huaqin_class)) {
		pr_err("%s fail to create class\n",__func__);
		return -1;
	}

	huaqin_hw_device = device_create(huaqin_class, NULL, 
						MKDEV(0, 0), NULL, HWINFO_INTERFACE_NAME);
	if (IS_ERR(huaqin_hw_device)) {
		pr_warn("fail to create device\n");
		return -1;
	}

	/* add kobject */
	ret = kobject_init_and_add(&huaqin_kobj, &huaqin_ktype, 
					&(huaqin_hw_device->kobj), HWINFO_HWID_NAME);
	if (ret < 0) {
		pr_err("%s fail to add kobject\n",__func__);
		return ret;
	}

	/* add wifi/bt/fm/gps */
	hq_register_hw_info(HWID_WIFI, "WCN-3998-1-116WLPSP-HR-0T-1");
	hq_register_hw_info(HWID_BT,   "WCN-3998-1-116WLPSP-HR-0T-1");
	hq_register_hw_info(HWID_FM,   "WCN-3998-1-116WLPSP-HR-0T-1");
	hq_register_hw_info(HWID_GPS,  "SDR735");
	hq_register_hw_info(HWID_PCBA, boardinfo);

	return 0;
}

int hq_deregister_hw_info(enum hardware_id id, char *device_name)
{
	int ret = 0;
	int find_hw_id = 0;
	int iterator = 0;
	struct hw_info *hw = NULL;
	struct attribute *attr = huaqin_attrs[iterator];

	if (!device_name) {
		pr_err("[%s]: device_name can't be empty\n",__func__);
		ret = -2;
		goto err;
	}

	while (attr) {
		hw = container_of(attr, struct hw_info , attr);

		iterator += 1;
		attr = huaqin_attrs[iterator];

		if (NULL == hw)
			continue;

		if (id == hw->hw_id) {
			find_hw_id = 1;

			if(0 == hw->hw_exist){
				pr_err("%s: device no registed hw_id:0x%x. Cant deregistered\n"
					,__func__
					,hw->hw_id);

				ret = -4;
				goto err;
			} else if (NULL == hw->hw_device_name) {

				pr_err("%s: hw_id is 0x%x Device name can't be NULL\n"
					,__func__
					,hw->hw_id);
				ret = -5;
				goto err;
			} else {
				if (0 == strncmp(hw->hw_device_name, 
							device_name, strlen(hw->hw_device_name))) {
					hw->hw_device_name = NULL;
					hw->hw_exist = 0;
				} else {
					pr_err("%s: hw_id=0x%x Registered dev name %s, want to deregister: %s\n"
						,__func__
						,hw->hw_id
						,hw->hw_device_name
						,device_name);
					ret = -6;
					goto err;
				}
			}

			goto err;
		}
	}

	if (0 == find_hw_id) {
		pr_err("%s: Can not find hw_id: 0x%x\n",__func__,id);
		ret = -3;
	}

err:
	return ret;
}
EXPORT_SYMBOL(hq_deregister_hw_info);

int hq_register_hw_info(enum hardware_id id,char *device_name)
{
	int ret = 0;
	int find_hw_id = 0;
	int iterator = 0;

	struct hw_info *hw = NULL;
	struct attribute *attr = huaqin_attrs[iterator];

	if (NULL == device_name) {
		pr_err("[%s]: device_name does not allow empty\n",__func__);
		ret = -2;
		goto err;
	}

	while (attr) {
		hw = container_of(attr, struct hw_info , attr);

		iterator += 1;
		attr = huaqin_attrs[iterator];

		if (NULL == hw)
			continue;

		if(id == hw->hw_id){
			find_hw_id = 1;

			if(hw->hw_exist){
				pr_err("[%s]: device already registed id:0x%x name:%s\n"
					,__func__
					,hw->hw_id
					,hw->hw_device_name);
				ret = -4;
				goto err;
			}

			hw->hw_device_name = device_name;
			hw->hw_exist = 1;
			goto err;
		}
	}

	if (0 == find_hw_id) {
		pr_err("%s: Cannot find hw_id: 0x%x\n", __func__, id);
		ret = -3;
	}

err:
	return ret;
}
EXPORT_SYMBOL(hq_register_hw_info);

#include <linux/proc_fs.h>

//Add node for sdc_det_gpio_status
/*static struct proc_dir_entry *sdc_det_gpio_status = NULL;

#define SDC_DET_GPIO_STATUS "cd_gpio_status"

static int cd_gpio_proc_show(struct seq_file *file, void* data)
{
	int cd_gpio_sts = 0;
        int cd_gpio = GPIO_CHIP_BASE + SDCARD_DET_N_GPIO;

	if (gpio_is_valid(cd_gpio)) {
		cd_gpio_sts = gpio_get_value(cd_gpio);
		pr_err("gpio_get_value of cd_gpio_sts is %d\n", cd_gpio_sts);
	} else {
		pr_err("gpio of SDC_DET_N_GPIO is not valid\n");
	}

	seq_printf(file, "%d\n", cd_gpio_sts);

	return 0;
}

static int cd_gpio_proc_open(struct inode* inode, struct file* file)
{
	return single_open(file, cd_gpio_proc_show, inode->i_private);
}

static const struct file_operations cd_gpio_status_ops =
{
	.open = cd_gpio_proc_open,
	.read = seq_read,
};*/

static int __init hq_harware_init(void)
{
	/* create sysfs entry at /sys/class/huaqin/interface/hw_info */
	create_sysfs();

	/*sdc_det_gpio_status = proc_create(SDC_DET_GPIO_STATUS, 
				0644, NULL, &cd_gpio_status_ops);
	if (sdc_det_gpio_status == NULL)
		pr_err("%s: create sdc_det_gpio_status failed\n");*/

	return 0;
}

core_initcall(hq_harware_init);
__setup("androidboot.hwboardid=", boardid_setup);
