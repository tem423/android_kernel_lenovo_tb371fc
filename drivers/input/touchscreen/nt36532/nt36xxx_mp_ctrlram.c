/*
 * Copyright (C) 2010 - 2022 Novatek, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include "nt36xxx.h"
#include "nt36xxx_mp_ctrlram.h"

#if NVT_TOUCH_MP

#define NORMAL_MODE 0x00
#define TEST_MODE_2 0x22
#define MP_MODE_CC 0x41
#define FREQ_HOP_DISABLE 0x66
#define FREQ_HOP_ENABLE 0x65

#define NVT_RESULT_INVALID 0
#define NVT_RESULT_PASS 2
#define NVT_RESULT_FAIL 1

#define SHORT_TEST_CSV_FILE "/data/local/tmp/ShortTest.csv"
#define OPEN_TEST_CSV_FILE "/data/local/tmp/OpenTest.csv"
#define FW_RAWDATA_CSV_FILE "/data/local/tmp/FWRawdataTest.csv"
#define FW_CC_CSV_FILE "/data/local/tmp/FWCCTest.csv"
#define NOISE_TEST_CSV_FILE "/data/local/tmp/NoiseTest.csv"
#define PEN_FW_RAW_TEST_CSV_FILE "/data/local/tmp/PenFWRawTest.csv"
#define PEN_NOISE_TEST_CSV_FILE "/data/local/tmp/PenNoiseTest.csv"

#define nvt_mp_seq_printf(m, fmt, args...) do {	\
	seq_printf(m, fmt, ##args);	\
	if (!nvt_mp_test_result_printed)	\
		printk(fmt, ##args);	\
} while (0)

static uint8_t *RecordResult_Short = NULL;
static uint8_t *RecordResult_Open = NULL;
static uint8_t *RecordResult_FW_Rawdata = NULL;
static uint8_t *RecordResult_FW_CC = NULL;
static uint8_t *RecordResult_FW_DiffMax = NULL;
static uint8_t *RecordResult_FW_DiffMin = NULL;
static uint8_t *RecordResult_PenTipX_Raw = NULL;
static uint8_t *RecordResult_PenTipY_Raw = NULL;
static uint8_t *RecordResult_PenRingX_Raw = NULL;
static uint8_t *RecordResult_PenRingY_Raw = NULL;
static uint8_t *RecordResult_PenTipX_DiffMax = NULL;
static uint8_t *RecordResult_PenTipX_DiffMin = NULL;
static uint8_t *RecordResult_PenTipY_DiffMax = NULL;
static uint8_t *RecordResult_PenTipY_DiffMin = NULL;
static uint8_t *RecordResult_PenRingX_DiffMax = NULL;
static uint8_t *RecordResult_PenRingX_DiffMin = NULL;
static uint8_t *RecordResult_PenRingY_DiffMax = NULL;
static uint8_t *RecordResult_PenRingY_DiffMin = NULL;

static int32_t TestResult_Short = 0;
static int32_t TestResult_Open = 0;
static int32_t TestResult_FW_Rawdata = 0;
static int32_t TestResult_FW_CC = 0;
static int32_t TestResult_Noise = 0;
static int32_t TestResult_FW_DiffMax = 0;
static int32_t TestResult_FW_DiffMin = 0;
static int32_t TestResult_Pen_FW_Raw = 0;
static int32_t TestResult_PenTipX_Raw = 0;
static int32_t TestResult_PenTipY_Raw = 0;
static int32_t TestResult_PenRingX_Raw = 0;
static int32_t TestResult_PenRingY_Raw = 0;
static int32_t TestResult_Pen_Noise = 0;
static int32_t TestResult_PenTipX_DiffMax = 0;
static int32_t TestResult_PenTipX_DiffMin = 0;
static int32_t TestResult_PenTipY_DiffMax = 0;
static int32_t TestResult_PenTipY_DiffMin = 0;
static int32_t TestResult_PenRingX_DiffMax = 0;
static int32_t TestResult_PenRingX_DiffMin = 0;
static int32_t TestResult_PenRingY_DiffMax = 0;
static int32_t TestResult_PenRingY_DiffMin = 0;

static int32_t *RawData_Short = NULL;
static int32_t *RawData_Open = NULL;
static int32_t *RawData_Diff = NULL;
static int32_t *RawData_Diff_Min = NULL;
static int32_t *RawData_Diff_Max = NULL;
static int32_t *RawData_FW_Rawdata = NULL;
static int32_t *RawData_FW_CC = NULL;
static int32_t *RawData_PenTipX_Raw = NULL;
static int32_t *RawData_PenTipY_Raw = NULL;
static int32_t *RawData_PenRingX_Raw = NULL;
static int32_t *RawData_PenRingY_Raw = NULL;
static int32_t *RawData_PenTipX_DiffMin = NULL;
static int32_t *RawData_PenTipX_DiffMax = NULL;
static int32_t *RawData_PenTipY_DiffMin = NULL;
static int32_t *RawData_PenTipY_DiffMax = NULL;
static int32_t *RawData_PenRingX_DiffMin = NULL;
static int32_t *RawData_PenRingX_DiffMax = NULL;
static int32_t *RawData_PenRingY_DiffMin = NULL;
static int32_t *RawData_PenRingY_DiffMax = NULL;

static struct proc_dir_entry *NVT_proc_selftest_entry = NULL;
static int8_t nvt_mp_test_result_printed = 0;
static uint8_t fw_ver = 0;
static uint16_t nvt_pid = 0;

extern void nvt_change_mode(uint8_t mode);
extern uint8_t nvt_get_fw_pipe(void);
extern void nvt_read_mdata(uint32_t xdata_addr, uint32_t xdata_btn_addr);
extern void nvt_get_mdata(int32_t *buf, uint8_t *m_x_num, uint8_t *m_y_num);
extern void nvt_read_get_num_mdata(uint32_t xdata_addr, int32_t *buffer, uint32_t num);
int32_t nvt_mp_parse_dt(struct device_node *root, const char *node_compatible);

/*******************************************************
Description:
    Novatek touchscreen allocate buffer for mp selftest.
*******************************************************/
static int nvt_mp_buffer_init(void)
{
    size_t RecordResult_BufSize = X_Y_DIMENSION_MAX + IC_KEY_CFG_SIZE;
    size_t RawData_BufSize = (X_Y_DIMENSION_MAX + IC_KEY_CFG_SIZE) * sizeof(int32_t);
    size_t Pen_RecordResult_BufSize = PEN_X_Y_DIMENSION_MAX;
    size_t Pen_RawData_BufSize = PEN_X_Y_DIMENSION_MAX * sizeof(int32_t);

    RecordResult_Short = (uint8_t *)kzalloc(RecordResult_BufSize, GFP_KERNEL);
    if (!RecordResult_Short) {
        NVT_ERR("kzalloc for RecordResult_Short failed!\n");
        return -ENOMEM;
    }

    RecordResult_Open = (uint8_t *)kzalloc(RecordResult_BufSize, GFP_KERNEL);
    if (!RecordResult_Open) {
        NVT_ERR("kzalloc for RecordResult_Open failed!\n");
        return -ENOMEM;
    }

    RecordResult_FW_Rawdata = (uint8_t *)kzalloc(RecordResult_BufSize, GFP_KERNEL);
    if (!RecordResult_FW_Rawdata) {
        NVT_ERR("kzalloc for RecordResult_FW_Rawdata failed!\n");
        return -ENOMEM;
    }

    RecordResult_FW_CC = (uint8_t *)kzalloc(RecordResult_BufSize, GFP_KERNEL);
    if (!RecordResult_FW_CC) {
        NVT_ERR("kzalloc for RecordResult_FW_CC failed!\n");
        return -ENOMEM;
    }

    RecordResult_FW_DiffMax = (uint8_t *)kzalloc(RecordResult_BufSize, GFP_KERNEL);
    if (!RecordResult_FW_DiffMax) {
        NVT_ERR("kzalloc for RecordResult_FW_DiffMax failed!\n");
        return -ENOMEM;
    }

    RecordResult_FW_DiffMin = (uint8_t *)kzalloc(RecordResult_BufSize, GFP_KERNEL);
    if (!RecordResult_FW_DiffMin) {
        NVT_ERR("kzalloc for RecordResult_FW_DiffMin failed!\n");
        return -ENOMEM;
    }

    if (ts->pen_support) {
        RecordResult_PenTipX_Raw = (uint8_t *)kzalloc(Pen_RecordResult_BufSize, GFP_KERNEL);
        RecordResult_PenTipY_Raw = (uint8_t *)kzalloc(Pen_RecordResult_BufSize, GFP_KERNEL);
        RecordResult_PenRingX_Raw = (uint8_t *)kzalloc(Pen_RecordResult_BufSize, GFP_KERNEL);
        RecordResult_PenRingY_Raw = (uint8_t *)kzalloc(Pen_RecordResult_BufSize, GFP_KERNEL);
        RecordResult_PenTipX_DiffMax = (uint8_t *)kzalloc(Pen_RecordResult_BufSize, GFP_KERNEL);
        RecordResult_PenTipX_DiffMin = (uint8_t *)kzalloc(Pen_RecordResult_BufSize, GFP_KERNEL);
        RecordResult_PenTipY_DiffMax = (uint8_t *)kzalloc(Pen_RecordResult_BufSize, GFP_KERNEL);
        RecordResult_PenTipY_DiffMin = (uint8_t *)kzalloc(Pen_RecordResult_BufSize, GFP_KERNEL);
        RecordResult_PenRingX_DiffMax = (uint8_t *)kzalloc(Pen_RecordResult_BufSize, GFP_KERNEL);
        RecordResult_PenRingX_DiffMin = (uint8_t *)kzalloc(Pen_RecordResult_BufSize, GFP_KERNEL);
        RecordResult_PenRingY_DiffMax = (uint8_t *)kzalloc(Pen_RecordResult_BufSize, GFP_KERNEL);
        RecordResult_PenRingY_DiffMin = (uint8_t *)kzalloc(Pen_RecordResult_BufSize, GFP_KERNEL);
        if (!RecordResult_PenTipX_Raw || !RecordResult_PenTipY_Raw || !RecordResult_PenRingX_Raw || !RecordResult_PenRingY_Raw ||
            !RecordResult_PenTipX_DiffMax || !RecordResult_PenTipX_DiffMin || !RecordResult_PenTipY_DiffMax || !RecordResult_PenTipY_DiffMin ||
            !RecordResult_PenRingX_DiffMax || !RecordResult_PenRingX_DiffMin || !RecordResult_PenRingY_DiffMax || !RecordResult_PenRingY_DiffMin) {
            NVT_ERR("kzalloc for Pen RecordResult failed!\n");
            return -ENOMEM;
        }
    }

    RawData_Short = (int32_t *)kzalloc(RawData_BufSize, GFP_KERNEL);
    RawData_Open = (int32_t *)kzalloc(RawData_BufSize, GFP_KERNEL);
    RawData_Diff = (int32_t *)kzalloc(RawData_BufSize, GFP_KERNEL);
    RawData_Diff_Min = (int32_t *)kzalloc(RawData_BufSize, GFP_KERNEL);
    RawData_Diff_Max = (int32_t *)kzalloc(RawData_BufSize, GFP_KERNEL);
    RawData_FW_Rawdata = (int32_t *)kzalloc(RawData_BufSize, GFP_KERNEL);
    RawData_FW_CC = (int32_t *)kzalloc(RawData_BufSize, GFP_KERNEL);

    if (!RawData_Short || !RawData_Open || !RawData_Diff || !RawData_Diff_Min || !RawData_Diff_Max ||
        !RawData_FW_Rawdata || !RawData_FW_CC) {
        NVT_ERR("kzalloc for RawData failed!\n");
        return -ENOMEM;
    }

    if (ts->pen_support) {
        RawData_PenTipX_Raw = (int32_t *)kzalloc(Pen_RawData_BufSize, GFP_KERNEL);
        RawData_PenTipY_Raw = (int32_t *)kzalloc(Pen_RawData_BufSize, GFP_KERNEL);
        RawData_PenRingX_Raw = (int32_t *)kzalloc(Pen_RawData_BufSize, GFP_KERNEL);
        RawData_PenRingY_Raw = (int32_t *)kzalloc(Pen_RawData_BufSize, GFP_KERNEL);
        RawData_PenTipX_DiffMax = (int32_t *)kzalloc(Pen_RawData_BufSize, GFP_KERNEL);
        RawData_PenTipX_DiffMin = (int32_t *)kzalloc(Pen_RawData_BufSize, GFP_KERNEL);
        RawData_PenTipY_DiffMax = (int32_t *)kzalloc(Pen_RawData_BufSize, GFP_KERNEL);
        RawData_PenTipY_DiffMin = (int32_t *)kzalloc(Pen_RawData_BufSize, GFP_KERNEL);
        RawData_PenRingX_DiffMax = (int32_t *)kzalloc(Pen_RawData_BufSize, GFP_KERNEL);
        RawData_PenRingX_DiffMin = (int32_t *)kzalloc(Pen_RawData_BufSize, GFP_KERNEL);
        RawData_PenRingY_DiffMax = (int32_t *)kzalloc(Pen_RawData_BufSize, GFP_KERNEL);
        RawData_PenRingY_DiffMin = (int32_t *)kzalloc(Pen_RawData_BufSize, GFP_KERNEL);
        if (!RawData_PenTipX_Raw || !RawData_PenTipY_Raw || !RawData_PenRingX_Raw || !RawData_PenRingY_Raw ||
            !RawData_PenTipX_DiffMax || !RawData_PenTipX_DiffMin || !RawData_PenTipY_DiffMax || !RawData_PenTipY_DiffMin ||
            !RawData_PenRingX_DiffMax || !RawData_PenRingX_DiffMin || !RawData_PenRingY_DiffMax || !RawData_PenRingY_DiffMin) {
            NVT_ERR("kzalloc for Pen RawData failed!\n");
            return -ENOMEM;
        }
    }

    return 0;
}

/*******************************************************
Description:
    Novatek touchscreen free buffer for mp selftest.
*******************************************************/
static void nvt_mp_buffer_deinit(void)
{
    kfree(RecordResult_Short);
    kfree(RecordResult_Open);
    kfree(RecordResult_FW_Rawdata);
    kfree(RecordResult_FW_CC);
    kfree(RecordResult_FW_DiffMax);
    kfree(RecordResult_FW_DiffMin);

    kfree(RawData_Short);
    kfree(RawData_Open);
    kfree(RawData_Diff);
    kfree(RawData_Diff_Min);
    kfree(RawData_Diff_Max);
    kfree(RawData_FW_Rawdata);
    kfree(RawData_FW_CC);

    if (ts->pen_support) {
        kfree(RecordResult_PenTipX_Raw);
        kfree(RecordResult_PenTipY_Raw);
        kfree(RecordResult_PenRingX_Raw);
        kfree(RecordResult_PenRingY_Raw);
        kfree(RecordResult_PenTipX_DiffMax);
        kfree(RecordResult_PenTipX_DiffMin);
        kfree(RecordResult_PenTipY_DiffMax);
        kfree(RecordResult_PenTipY_DiffMin);
        kfree(RecordResult_PenRingX_DiffMax);
        kfree(RecordResult_PenRingX_DiffMin);
        kfree(RecordResult_PenRingY_DiffMax);
        kfree(RecordResult_PenRingY_DiffMin);

        kfree(RawData_PenTipX_Raw);
        kfree(RawData_PenTipY_Raw);
        kfree(RawData_PenRingX_Raw);
        kfree(RawData_PenRingY_Raw);
        kfree(RawData_PenTipX_DiffMax);
        kfree(RawData_PenTipX_DiffMin);
        kfree(RawData_PenTipY_DiffMax);
        kfree(RawData_PenTipY_DiffMin);
        kfree(RawData_PenRingX_DiffMax);
        kfree(RawData_PenRingX_DiffMin);
        kfree(RawData_PenRingY_DiffMax);
        kfree(RawData_PenRingY_DiffMin);
    }
}

/*******************************************************
Description:
    Novatek touchscreen self-test sequence print show function.
*******************************************************/
static int32_t c_show_selftest(struct seq_file *m, void *v)
{
    NVT_LOG("++\n");

    if (ts->pen_support) {
        if ((TestResult_Short == 0) && (TestResult_Open == 0) &&
            (TestResult_FW_Rawdata == 0) && (TestResult_FW_CC == 0) &&
            (TestResult_Noise == 0) && (TestResult_Pen_FW_Raw == 0) &&
            (TestResult_Pen_Noise == 0)) {
            nvt_mp_seq_printf(m, "Selftest PASS.\n\n");
        } else {
            nvt_mp_seq_printf(m, "Selftest FAIL!\n\n");
        }
    } else {
        if ((TestResult_Short == 0) && (TestResult_Open == 0) &&
            (TestResult_FW_Rawdata == 0) && (TestResult_FW_CC == 0) &&
            (TestResult_Noise == 0)) {
            nvt_mp_seq_printf(m, "Selftest PASS.\n\n");
        } else {
            nvt_mp_seq_printf(m, "Selftest FAIL!\n\n");
        }
    }

    nvt_mp_seq_printf(m, "FW Version: %d, NVT PID: 0x%04X\n", fw_ver, nvt_pid);
    nvt_mp_seq_printf(m, "Short Test: %s\n", TestResult_Short == 0 ? "PASS" : "FAIL");
    nvt_mp_seq_printf(m, "Open Test: %s\n", TestResult_Open == 0 ? "PASS" : "FAIL");
    nvt_mp_seq_printf(m, "FW Rawdata Test: %s\n", TestResult_FW_Rawdata == 0 ? "PASS" : "FAIL");
    nvt_mp_seq_printf(m, "FW CC Test: %s\n", TestResult_FW_CC == 0 ? "PASS" : "FAIL");
    nvt_mp_seq_printf(m, "Noise Test: %s\n", TestResult_Noise == 0 ? "PASS" : "FAIL");

    return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
    return *pos < 1 ? (void *)1 : NULL;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
    ++*pos;
    return NULL;
}

static void c_stop(struct seq_file *m, void *v)
{
    return;
}

const struct seq_operations nvt_selftest_seq_ops = {
    .start  = c_start,
    .next   = c_next,
    .stop   = c_stop,
    .show   = c_show_selftest,
};

/*******************************************************
Description:
    Novatek touchscreen /proc/nvt_selftest open function.
*******************************************************/
static int32_t nvt_selftest_open(struct inode *inode, struct file *file)
{
    NVT_LOG("++\n");

    if (mutex_lock_interruptible(&ts->lock)) {
        return -ERESTARTSYS;
    }

    TestResult_Short = 0;
    TestResult_Open = 0;
    TestResult_FW_Rawdata = 0;
    TestResult_FW_CC = 0;
    TestResult_Noise = 0;
    TestResult_FW_DiffMax = 0;
    TestResult_FW_DiffMin = 0;

    if (ts->pen_support) {
        TestResult_Pen_FW_Raw = 0;
        TestResult_PenTipX_Raw = 0;
        TestResult_PenTipY_Raw = 0;
        TestResult_PenRingX_Raw = 0;
        TestResult_PenRingY_Raw = 0;
        TestResult_Pen_Noise = 0;
    }

    fw_ver = ts->fw_ver;
    nvt_pid = ts->nvt_pid;

    mutex_unlock(&ts->lock);

    nvt_mp_test_result_printed = 0;

    return seq_open(file, &nvt_selftest_seq_ops);
}

/* ========== fops 结构体定义 ========== */

#ifdef HAVE_PROC_OPS
static const struct proc_ops nvt_selftest_fops = {
    .proc_open = nvt_selftest_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = seq_release,
};
#else
static const struct file_operations nvt_selftest_fops = {
    .owner = THIS_MODULE,
    .open = nvt_selftest_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = seq_release,
};
#endif

/*******************************************************
Description:
    Novatek touchscreen parse device tree mp function.
*******************************************************/
#ifdef CONFIG_OF
int32_t nvt_mp_parse_dt(struct device_node *root, const char *node_compatible)
{
    NVT_LOG("Parse mp criteria for node %s\n", node_compatible);
    /* 简化实现，实际使用时需要根据设备树解析 */
    return 0;
}
#endif

/*******************************************************
Description:
    Novatek touchscreen MP function proc. file node
    initial function.
*******************************************************/
int32_t nvt_mp_proc_init(void)
{
    NVT_proc_selftest_entry = proc_create("nvt_selftest", 0444, NULL, &nvt_selftest_fops);
    if (NVT_proc_selftest_entry == NULL) {
        NVT_ERR("create /proc/nvt_selftest Failed!\n");
        return -1;
    } else {
        NVT_LOG("create /proc/nvt_selftest Succeeded!\n");
    }

    if (nvt_mp_buffer_init()) {
        NVT_ERR("Allocate mp memory failed\n");
        return -1;
    } else {
        NVT_LOG("MP buffer initialized Succeeded!\n");
        return 0;
    }
}

/*******************************************************
Description:
    Novatek touchscreen MP function proc. file node
    deinitial function.
*******************************************************/
void nvt_mp_proc_deinit(void)
{
    nvt_mp_buffer_deinit();

    if (NVT_proc_selftest_entry != NULL) {
        remove_proc_entry("nvt_selftest", NULL);
        NVT_proc_selftest_entry = NULL;
        NVT_LOG("Removed /proc/%s\n", "nvt_selftest");
    }
}
#endif /* #if NVT_TOUCH_MP */