/*
 * Copyright (C) 2015 MediaTek Inc.
 * Modified for Qualcomm Snapdragon 870 (SM8250-AC)
 */

#include "hqsys_misc.h"

MISC_INFO(MISC_EMMC_SIZE, emmc_size);
MISC_INFO(MISC_RAM_SIZE, ram_size);
MISC_INFO(MISC_BOOT_MODE, boot_mode);
MISC_INFO(MISC_OTP_SN, otp_sn);

/* 骁龙870 eMMC路径（如果有eMMC设备） */
#define qcom_emmc "/sys/class/mmc_host/mmc0/mmc0:0001/block/mmcblk0/size"
#define qcom_emmc_len 18

unsigned int round_kbytes_to_readable_mbytes(unsigned int k)
{
	unsigned int r_size_m = 0;
	unsigned int in_mega = k / 1024;

	if (in_mega > 64 * 1024)
		r_size_m = 128 * 1024;
	else if (in_mega > 32 * 1024)
		r_size_m = 64 * 1024;
	else if (in_mega > 16 * 1024)
		r_size_m = 32 * 1024;
	else if (in_mega > 8 * 1024)
		r_size_m = 16 * 1024;
	else if (in_mega > 6 * 1024)
		r_size_m = 8 * 1024;
	else if (in_mega > 4 * 1024)
		r_size_m = 6 * 1024;
	else if (in_mega > 3 * 1024)
		r_size_m = 4 * 1024;
	else if (in_mega > 2 * 1024)
		r_size_m = 3 * 1024;
	else if (in_mega > 1024)
		r_size_m = 2 * 1024;
	else if (in_mega > 512)
		r_size_m = 1024;
	else if (in_mega > 256)
		r_size_m = 512;
	else if (in_mega > 128)
		r_size_m = 256;
	else
		k = 0;

	return r_size_m;
}

static struct attribute *hq_misc_attrs[] = {
	&misc_info_emmc_size.attr,
	&misc_info_ram_size.attr,
	&misc_info_boot_mode.attr,
	&misc_info_otp_sn.attr,
	NULL
};

#define SN_LEN 12

static ssize_t hq_misc_show(struct kobject *kobj, struct attribute *a, char *buf)
{
	ssize_t count = 0;
	struct misc_info *mi = container_of(a, struct misc_info, attr);

	switch (mi->m_id) {
	case MISC_RAM_SIZE: {
#define K(x) ((x) << (PAGE_SHIFT - 10))
		struct sysinfo i;
		si_meminfo(&i);

		if (round_kbytes_to_readable_mbytes(K(i.totalram)) >= 1024) {
			count = sprintf(buf, "%dGB", round_kbytes_to_readable_mbytes(K(i.totalram)) / 1024);
		} else {
			count = sprintf(buf, "%dMB", round_kbytes_to_readable_mbytes(K(i.totalram)));
		}
		break;
	}
	case MISC_EMMC_SIZE:
		/* 骁龙870 使用UFS，此节点不实现，返回0 */
		count = sprintf(buf, "0GB\n");
		break;
	case MISC_OTP_SN:
		/* 骁龙平台OTP SN读取需要厂商私有接口，默认返回未实现 */
		count = sprintf(buf, "SN not implemented on QCOM\n");
		break;
	default:
		count = sprintf(buf, "Not support");
		break;
	}

	return count;
}

static ssize_t hq_misc_store(struct kobject *kobj, struct attribute *a, const char *buf, size_t count)
{
	struct misc_info *mi = container_of(a, struct misc_info, attr);

	switch (mi->m_id) {
	default:
		break;
	}
	return count;
}

static struct kobject hq_misc_kobj;
static const struct sysfs_ops hq_misc_sysfs_ops = {
	.show = hq_misc_show,
	.store = hq_misc_store,
};

static struct kobj_type hq_misc_ktype = {
	.sysfs_ops = &hq_misc_sysfs_ops,
	.default_attrs = hq_misc_attrs
};

int create_misc(void)
{
	int ret;

	ret = register_kboj_under_hqsysfs(&hq_misc_kobj, &hq_misc_ktype, HUAQIN_MISC_NAME);
	if (ret < 0) {
		pr_err("%s fail to add hq_misc_kobj\n", __func__);
		return ret;
	}
	return 0;
}

static int __init hq_misc_sys_init(void)
{
	create_misc();
	return 0;
}

late_initcall(hq_misc_sys_init);
MODULE_AUTHOR("KaKa Ni <nigang@hq_misc.com>");
MODULE_DESCRIPTION("Huaqin Hardware Info Driver for QCOM");
MODULE_LICENSE("GPL");