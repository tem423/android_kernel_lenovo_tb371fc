#define pr_fmt(fmt) "bootlog: " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>

#define LOG_BUF_SIZE    4096

enum bootlog_level {
	FIRST_BOOTLOG,
	SECOND_BOOTLOG,
	MAX_BOOTLOG,
};

static char *bootlog_vddr;
static u32 log_mem_size;
/* The second bootloader log offset in memory */
static u32 log_offset;

static enum bootlog_level curr_level = FIRST_BOOTLOG;
static int log_show_times;

static char *get_bootlog_offset_addr(enum bootlog_level *level)
{
	int buf_size = LOG_BUF_SIZE;
	char *start_addr = NULL;
	char *log_addr = NULL;
	int max_show_times = 0;

	if (*level == FIRST_BOOTLOG) {
		start_addr = bootlog_vddr;
		max_show_times = log_offset/LOG_BUF_SIZE;
	} else {
		start_addr = bootlog_vddr + log_offset;
		max_show_times = (log_mem_size - log_offset)/LOG_BUF_SIZE;
	}

	log_addr = start_addr + (buf_size * log_show_times);
	log_show_times += 1;

	if ((*log_addr == '\0') || (log_show_times >= max_show_times)) {
		if (*level == FIRST_BOOTLOG) {
			pr_info("The First bootlog print complete\n");
			*level = SECOND_BOOTLOG;
			log_show_times = 0;
		} else {
			pr_info("The second bootlog print complete\n");
			*level = MAX_BOOTLOG;
		}
	}

	return log_addr;
}

static ssize_t debug_show_bootlog(struct file *file, char __user *ubuf,
				       size_t count, loff_t *ppos)
{
	int ret = 0;
	char *buf = NULL;
	char *log_addr = NULL;
	unsigned int write_sz = 0;

	buf = kzalloc(sizeof(char) * LOG_BUF_SIZE, GFP_KERNEL);
	if (!buf) {
		pr_err("%s: Error allocating memory\n", __func__);
		return -ENOMEM;
	}

	log_addr = get_bootlog_offset_addr(&curr_level);
	if (curr_level < MAX_BOOTLOG) {
		if (NULL == log_addr) {
			ret = -EINVAL;
			goto print_end;
		}

		pr_info("current address %p\n", log_addr);
		write_sz = scnprintf(buf, LOG_BUF_SIZE, "%s\n", log_addr);
	} else {
		/* read complete, reset */
		log_show_times = 0;
		curr_level = FIRST_BOOTLOG;
		ret = 0;
		goto print_end;
	}

	*ppos = 0;
	ret = simple_read_from_buffer(ubuf, count, ppos, buf, write_sz);
print_end:
	kfree(buf);
	return ret;
}

const struct file_operations show_bootlog_procfs_ops = {
	.read = debug_show_bootlog,
};

int show_bootlog_procfs_init(void)
{
	proc_create("bootloader_log", S_IRUGO, NULL,
			&show_bootlog_procfs_ops);
	return 0;
}

static int show_bootlog_parse_dt(struct device *dev, u32 *data)
{
	struct device_node *pnode;
	const u32 *addr;
	u64 size;

	pnode = of_parse_phandle(dev->of_node, "linux,contiguous-region", 0);
	if (pnode == NULL) {
		pr_err("mem reservation for bootlog not find\n");
		return -EINVAL;
	}

	addr = of_get_address(pnode, 0, &size, NULL);
	if (!addr) {
		pr_err("failed to parse the bootlog reserve memory\n");
		of_node_put(pnode);
		return -EINVAL;
	}

	data[0] = (u32) of_read_ulong(addr, 2);
	data[1] = (u32) size;
	log_mem_size = data[1];

	of_property_read_u32(dev->of_node, "second_log_offset", &log_offset);
	pr_info("the log_size is 0x%x\n", log_offset);

	return 0;
}

static int show_bootlog_probe(struct platform_device *pdev)
{
	int ret = 0;
	void *vaddr;
	u32 offsets[2];

	ret = show_bootlog_parse_dt(&pdev->dev, offsets);
	if (ret < 0) {
		pr_err("parse dts failed %d\n", ret);
		return -EINVAL;
	}

	vaddr = ioremap_wc(offsets[0], offsets[1]);
	pr_info("phys_base=0x%x, size=0x%x vaddr=%p\n",
			offsets[0], offsets[1], vaddr);
	bootlog_vddr = (char *)vaddr;

	ret = show_bootlog_procfs_init();
	if (ret)
		return -EPERM;

	return 0;
}

static int show_bootlog_remove(struct platform_device *pdev)
{
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id bootlog_of_match[] = {
	{ .compatible = "debug,bootlog", },
	{},
};
MODULE_DEVICE_TABLE(of, bootlog_of_match);
#endif

static struct platform_driver show_bootlog_driver = {
	.probe  = show_bootlog_probe,
	.remove = show_bootlog_remove,
	.driver = {
		.name = "show_bootlog",
#ifdef CONFIG_OF
		.of_match_table	= of_match_ptr(bootlog_of_match),
#endif
	}
};

static int __init show_bootlog_init(void)
{
	return platform_driver_register(&show_bootlog_driver);
}

static void show_bootlog_exit(void)
{
	pr_info("enter %s\n", __func__);
	platform_driver_unregister(&show_bootlog_driver);
}

module_init(show_bootlog_init);
module_exit(show_bootlog_exit);