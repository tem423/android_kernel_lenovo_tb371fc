/*
 * Copyright (C) 2015 MediaTek Inc.
 * Modified for Qualcomm Snapdragon 870 (SM8250-AC)
 */

#include <linux/hqsysfs.h>
#include "hqsys_misc.h"
#include "hqsys_pcba.h"
#include <linux/of.h>
#include <linux/types.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/sysinfo.h>
#include <linux/slab.h>

#define HQ_SYS_FS_VER "2022-1230 V2.0-QCOM-SM8250"

/* 骁龙870 UFS 路径（SM8250-AC） */
#define emmc_file "/sys/devices/platform/soc/1d84000.ufshc/geometry_descriptor/raw_device_capacity"
#define UFS_VENDOR_NAME "/sys/devices/platform/soc/1d84000.ufshc/string_descriptors/manufacturer_name"
#define emmc_len 18

#define SKU_CMDLINE "androidboot.product.hardware.sku="
#define STG_CMDLINE "androidboot.stage="

static HW_INFO(HWID_VER, ver);
static HW_INFO(HWID_SUMMARY, hw_summary);
static HW_INFO(HWID_MEMORY, memory);
static HW_INFO(HWID_LCM, lcm);
static HW_INFO(HWID_CTP, ctp);
static HW_INFO(HWID_SUB_CAM, sub_cam);
static HW_INFO(HWID_SUB_CAM_2, main0_cam);
static HW_INFO(HWID_MAIN_CAM, main1_cam);
static HW_INFO(HWID_MAIN_CAM_2, main2_cam);
static HW_INFO(HWID_MAIN_CAM_3, main3_cam);
static HW_INFO(HWID_MAIN_LENS, main_cam_len);
static HW_INFO(HWID_FLASHLIGHT, flashlight);
static HW_INFO(HWID_GSENSOR, gsensor);
static HW_INFO(HWID_ALSPS, alsps);
static HW_INFO(HWID_MSENSOR, msensor);
static HW_INFO(HWID_GYRO, gyro);
static HW_INFO(HWID_IRDA, irda);
static HW_INFO(HWID_FUEL_GAUGE_IC, fuel_gauge_ic);
static HW_INFO(HWID_NFC, nfc);
static HW_INFO(HWID_FP, fingerprint);
static HW_INFO(HWID_PCBA, pcba_config);
static HW_INFO(HWID_PMIC_VERSION, pmic_version);
static HW_INFO(HWID_AUDIO, audio_PA);
static HW_INFO(HWID_PCBA_INFO, pcba_info);

static struct attribute *huaqin_attrs[] = {
	&hw_info_ver.attr,
	&hw_info_hw_summary.attr,
	&hw_info_memory.attr,
	&hw_info_lcm.attr,
	&hw_info_ctp.attr,
	&hw_info_sub_cam.attr,
	&hw_info_main0_cam.attr,
	&hw_info_main1_cam.attr,
	&hw_info_main2_cam.attr,
	&hw_info_main3_cam.attr,
	&hw_info_main_cam_len.attr,
	&hw_info_flashlight.attr,
	&hw_info_gsensor.attr,
	&hw_info_alsps.attr,
	&hw_info_msensor.attr,
	&hw_info_gyro.attr,
	&hw_info_irda.attr,
	&hw_info_fuel_gauge_ic.attr,
	&hw_info_nfc.attr,
	&hw_info_fingerprint.attr,
	&hw_info_pcba_config.attr,
	&hw_info_pmic_version.attr,
	&hw_info_audio_PA.attr,
	&hw_info_pcba_info.attr,
	NULL
};

#define K(x) ((x) << (PAGE_SHIFT - 10))

static int get_ufs_vendor_name(char *buff_name)
{
	struct file *pfile = NULL;
	mm_segment_t old_fs;
	loff_t pos;
	ssize_t ret = 0;
	char vendor_name[emmc_len] = {0};

	if (buff_name == NULL)
		return -1;

	pfile = filp_open(UFS_VENDOR_NAME, O_RDONLY, 0);
	if (IS_ERR(pfile)) {
		pr_err("[HWINFO]: open UFS vendor file failed!\n");
		return -1;
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	pos = 0;

	ret = vfs_read(pfile, vendor_name, emmc_len, &pos);
	filp_close(pfile, NULL);
	set_fs(old_fs);

	if (ret <= 0) {
		pr_err("[HWINFO]: read UFS vendor failed!\n");
		return -1;
	}

	sprintf(buff_name, "%s", vendor_name);
	return 0;
}

static unsigned int get_emmc_size(void)
{
	unsigned int emmc_size = 32;
	struct file *pfile = NULL;
	mm_segment_t old_fs;
	loff_t pos;
	ssize_t ret = 0;
	unsigned long long Size_buf = 0;
	char buf_size[emmc_len] = {0};

	pfile = filp_open(emmc_file, O_RDONLY, 0);
	if (IS_ERR(pfile)) {
		pr_err("[HWINFO]: open UFS size file failed!\n");
		goto ERR_0;
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	pos = 0;

	ret = vfs_read(pfile, buf_size, emmc_len, &pos);
	filp_close(pfile, NULL);
	set_fs(old_fs);

	if (ret <= 0) {
		pr_err("[HWINFO]: read UFS size failed!\n");
		goto ERR_0;
	}

	Size_buf = simple_strtoull(buf_size, NULL, 0);
	Size_buf <<= 3; // Switch to KB
	emmc_size = (((unsigned int)Size_buf) / 1024) / 1024;

	if (emmc_size > 256)
		emmc_size = 512;
	else if (emmc_size > 128)
		emmc_size = 256;
	else if (emmc_size > 64)
		emmc_size = 128;
	else if (emmc_size > 32)
		emmc_size = 64;
	else if (emmc_size > 16)
		emmc_size = 32;
	else if (emmc_size > 8)
		emmc_size = 16;
	else if (emmc_size > 6)
		emmc_size = 8;
	else if (emmc_size > 4)
		emmc_size = 6;
	else if (emmc_size > 3)
		emmc_size = 4;
	else
		emmc_size = 0;

ERR_0:
	return emmc_size;
}

static unsigned int get_ram_size(void)
{
	unsigned int ram_size, temp_size;
	struct sysinfo info;

	si_meminfo(&info);

	temp_size = K(info.totalram) / 1024;
	if (temp_size > 7168)
		ram_size = 8;
	else if (temp_size > 5120)
		ram_size = 6;
	else if (temp_size > 3072)
		ram_size = 4;
	else if (temp_size > 2048)
		ram_size = 3;
	else if (temp_size > 1024)
		ram_size = 2;
	else if (temp_size > 512)
		ram_size = 1;
	else
		ram_size = 0;

	return ram_size;
}

static ssize_t huaqin_show(struct kobject *kobj, struct attribute *a, char *buf)
{
	ssize_t count = 0;
	struct hw_info *hw = container_of(a, struct hw_info, attr);
	struct PCBA_MSG *msg;
	char ufs_vendor_name[emmc_len] = {0};

	msg = get_pcba_msg();

	if (NULL == hw)
		return sprintf(buf, "Data error\n");

	if (HWID_VER == hw->hw_id) {
		count = sprintf(buf, "%s\n", HQ_SYS_FS_VER);
	} else if (HWID_SUMMARY == hw->hw_id) {
		int iterator = 0;
		struct hw_info *curent_hw = NULL;
		struct attribute *attr = huaqin_attrs[iterator];

		get_ufs_vendor_name(ufs_vendor_name);
		count = sprintf(buf, "%s:%dG+%dG UFS\n",
				ufs_vendor_name, get_ram_size(), get_emmc_size());

		while (attr) {
			curent_hw = container_of(attr, struct hw_info, attr);
			iterator += 1;
			attr = huaqin_attrs[iterator];

			if (curent_hw->hw_exist && (NULL != curent_hw->hw_device_name)) {
				count += sprintf(buf + count, "%s: %s\n",
						curent_hw->attr.name, curent_hw->hw_device_name);
			}
		}
	} else if (HWID_PCBA == hw->hw_id) {
		int x = 0, y = 0, SKU_CMDLINE_LEN = 33, STG_CMDLINE_LEN = 18, CMDLINE_SPACE = 32;
		char *temp_sku = NULL, *temp_stage = NULL;
		char pcba_config_string[20] = "PCBA_", real_sku[5] = { "0" }, real_stage[5] = { "0" };

		temp_sku = strstr(saved_command_line, SKU_CMDLINE);
		if (temp_sku) {
			temp_sku += SKU_CMDLINE_LEN;
			for (; *temp_sku != CMDLINE_SPACE && *temp_sku != '\0'; temp_sku++) {
				real_sku[x++] = *temp_sku;
			}
		}
		strcat(real_sku, "_");
		strcat(pcba_config_string, real_sku);

		temp_stage = strstr(saved_command_line, STG_CMDLINE);
		if (temp_stage) {
			temp_stage += STG_CMDLINE_LEN;
			for (; *temp_stage != CMDLINE_SPACE && *temp_stage != '\0'; temp_stage++) {
				real_stage[y++] = *temp_stage;
			}
		}
		strcat(pcba_config_string, real_stage);
		count = sprintf(buf, "%s\n", pcba_config_string);
		return count;
	} else if (HWID_PCBA_INFO == hw->hw_id) {
		/* 骁龙870: 直接硬编码 SM8250 */
		count = sprintf(buf, "SM8250_%dG\n", get_ram_size());
		return count;
	} else if (HWID_PMIC_VERSION == hw->hw_id) {
		/* 骁龙870 主PMIC: PM8150 */
		count = sprintf(buf, "%s\n", "PMIC: PM8150");
	} else if (HWID_MEMORY == hw->hw_id) {
		get_ufs_vendor_name(ufs_vendor_name);
		count = sprintf(buf, "%s:%dG+%dG UFS\n",
				ufs_vendor_name, get_ram_size(), get_emmc_size());
	} else {
		if (0 == hw->hw_exist) {
			count = sprintf(buf, "Not support\n");
		} else if (NULL == hw->hw_device_name) {
			count = sprintf(buf, "Installed with no device Name\n");
		} else {
			count = sprintf(buf, "%s\n", hw->hw_device_name);
		}
	}

	return count;
}

static ssize_t huaqin_store(struct kobject *kobj, struct attribute *a, const char *buf, size_t count)
{
	return count;
}

static struct kobject huaqin_kobj;
static const struct sysfs_ops huaqin_sysfs_ops = {
	.show = huaqin_show,
	.store = huaqin_store,
};

static struct kobj_type huaqin_ktype = {
	.sysfs_ops = &huaqin_sysfs_ops,
	.default_attrs = huaqin_attrs
};

static struct class *huaqin_class;
static struct device *huaqin_hw_device;

int register_kboj_under_hqsysfs(struct kobject *kobj, struct kobj_type *ktype, const char *fmt, ...)
{
	return kobject_init_and_add(kobj, ktype, &(huaqin_hw_device->kobj), fmt);
}
EXPORT_SYMBOL_GPL(register_kboj_under_hqsysfs);

static int __init create_sysfs(void)
{
	int ret;

	huaqin_class = class_create(THIS_MODULE, HUAQIN_CLASS_NAME);
	if (IS_ERR(huaqin_class)) {
		pr_err("%s fail to create class\n", __func__);
		return -1;
	}

	huaqin_hw_device = device_create(huaqin_class, NULL, MKDEV(0, 0), NULL, HUAIN_INTERFACE_NAME);
	if (IS_ERR(huaqin_hw_device)) {
		pr_warn("fail to create device\n");
		return -1;
	}

	ret = kobject_init_and_add(&huaqin_kobj, &huaqin_ktype, &(huaqin_hw_device->kobj), HUAQIN_HWID_NAME);
	if (ret < 0) {
		pr_err("%s fail to add kobject\n", __func__);
		return ret;
	}

	return 0;
}

int hq_deregister_hw_info(enum hardware_id id, char *device_name)
{
	int ret = 0;
	int find_hw_id = 0;
	int iterator = 0;
	struct hw_info *hw = NULL;
	struct attribute *attr = huaqin_attrs[iterator];

	if (NULL == device_name) {
		pr_err("[%s]: device_name does not allow empty\n", __func__);
		return -2;
	}

	while (attr) {
		hw = container_of(attr, struct hw_info, attr);
		iterator += 1;
		attr = huaqin_attrs[iterator];

		if (NULL == hw)
			continue;

		if (id == hw->hw_id) {
			find_hw_id = 1;
			if (0 == hw->hw_exist) {
				pr_err("[%s]: device has not registed hw->id:0x%x\n", __func__, hw->hw_id);
				return -4;
			} else if (NULL == hw->hw_device_name) {
				pr_err("[%s]: hw_id is 0x%x Device name cant be NULL\n", __func__, hw->hw_id);
				return -5;
			} else {
				if (0 == strncmp(hw->hw_device_name, device_name, strlen(hw->hw_device_name))) {
					hw->hw_device_name = NULL;
					hw->hw_exist = 0;
				} else {
					pr_err("[%s]: hw_id is 0x%x Registered device name %s, want to deregister: %s\n",
						__func__, hw->hw_id, hw->hw_device_name, device_name);
					return -6;
				}
			}
			goto err;
		}
	}

	if (0 == find_hw_id) {
		pr_err("[%s]: Cant find correct hardware_id: 0x%x\n", __func__, id);
		return -3;
	}

err:
	return ret;
}

static char *audio_pa;

int hq_regiser_hw_info(enum hardware_id id, char *device_name)
{
	int ret = 0;
	int find_hw_id = 0;
	int iterator = 0;
	struct hw_info *hw = NULL;
	struct attribute *attr = huaqin_attrs[iterator];

	if (NULL == device_name) {
		pr_err("[%s]: device_name does not allow empty\n", __func__);
		return -2;
	}

	while (attr) {
		hw = container_of(attr, struct hw_info, attr);
		iterator += 1;
		attr = huaqin_attrs[iterator];

		if (NULL == hw)
			continue;

		if (id == hw->hw_id) {
			find_hw_id = 1;
			if (hw->hw_exist) {
				pr_err("[%s]: device has already registed hw->id:0x%x hw_device_name:%s\n",
					__func__, hw->hw_id, hw->hw_device_name);
				return -4;
			}
			if (hw->hw_id == HWID_AUDIO) {
				audio_pa = device_name;
			}
			hw->hw_device_name = device_name;
			hw->hw_exist = 1;
			goto err;
		}
	}

	if (0 == find_hw_id) {
		pr_err("[%s]: Cant find correct hardware_id: 0x%x\n", __func__, id);
		return -3;
	}

err:
	return ret;
}

#include <linux/proc_fs.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>

#define PROC_BOOT_REASON_FILE "boot_status"
#define SDC_DETECT_STATUS "sdc_det_gpio_status"

// 骁龙870的SD卡检测GPIO需要查具体机型原理图，这里默认注释掉
// #define SDC_DETECT_GPIO  xxx

static struct proc_dir_entry *boot_reason_proc;
static struct proc_dir_entry *sdc_detect_status;
static unsigned int boot_into_factory;

static int boot_reason_proc_show(struct seq_file *file, void *data)
{
	char temp[40] = {0};
	sprintf(temp, "%d\n", boot_into_factory);
	seq_printf(file, "%s\n", temp);
	return 0;
}

static int boot_reason_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, boot_reason_proc_show, inode->i_private);
}

static const struct file_operations boot_reason_proc_fops = {
	.open = boot_reason_proc_open,
	.read = seq_read,
};

static int sdc_detect_proc_show(struct seq_file *file, void *data)
{
	// 骁龙870需要配置正确的GPIO号
	seq_printf(file, "%d\n", -1); // 默认返回-1表示未实现
	return 0;
}

static int sdc_detect_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, sdc_detect_proc_show, inode->i_private);
}

static const struct file_operations sdc_detect_proc_fops = {
	.open = sdc_detect_proc_open,
	.read = seq_read,
};

static int sdc_detect_init(void)
{
	sdc_detect_status = proc_create(SDC_DETECT_STATUS, 0644, NULL, &sdc_detect_proc_fops);
	if (sdc_detect_status == NULL) {
		pr_err("[%s]: create_proc_entry sdc_detect_status failed\n", __func__);
		return -1;
	}
	return 0;
}

static int __init hq_harware_init(void)
{
	create_sysfs();

	boot_reason_proc = proc_create(PROC_BOOT_REASON_FILE, 0644, NULL, &boot_reason_proc_fops);
	if (boot_reason_proc == NULL) {
		pr_err("[%s]: create_proc_entry boot_reason_proc failed\n", __func__);
	}

	if (sdc_detect_init() < 0)
		pr_err("[%s]: create_proc_entry sdc_detect_proc failed\n", __func__);

	return 0;
}

char *get_audio_pa_vendor(void)
{
	return audio_pa;
}
EXPORT_SYMBOL(get_audio_pa_vendor);

core_initcall(hq_harware_init);
MODULE_AUTHOR("KaKa Ni <nigang@huaqin.com>");
MODULE_DESCRIPTION("Huaqin Hardware Info Driver for Qualcomm SM8250");
MODULE_LICENSE("GPL");