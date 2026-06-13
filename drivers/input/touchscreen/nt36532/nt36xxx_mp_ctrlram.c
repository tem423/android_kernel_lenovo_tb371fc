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
/* 删除： static struct proc_dir_entry *NVT_proc_aftersales_test_entry = NULL; */
static int8_t nvt_mp_test_result_printed = 0;
static uint8_t fw_ver = 0;
static uint16_t nvt_pid = 0;

extern void nvt_change_mode(uint8_t mode);
extern uint8_t nvt_get_fw_pipe(void);
extern void nvt_read_mdata(uint32_t xdata_addr, uint32_t xdata_btn_addr);
extern void nvt_get_mdata(int32_t *buf, uint8_t *m_x_num, uint8_t *m_y_num);
extern void nvt_read_get_num_mdata(uint32_t xdata_addr, int32_t *buffer, uint32_t num);
int32_t nvt_mp_parse_dt(struct device_node *root, const char *node_compatible);

/* 所有其他函数保持不变... */
/* nvt_mp_buffer_init, nvt_mp_buffer_deinit, nvt_print_data_log_in_one_line,
   nvt_print_result_log_in_one_line, nvt_print_data_array, nvt_print_criteria,
   nvt_save_rawdata_to_csv, nvt_polling_hand_shake_status, nvt_switch_FreqHopEnDis,
   nvt_read_baseline, nvt_read_CC, nvt_read_pen_baseline, nvt_enable_noise_collect,
   nvt_read_fw_noise, nvt_enable_open_test, nvt_enable_short_test, nvt_read_fw_open,
   nvt_read_fw_short, RawDataTest_SinglePoint_Sub, print_selftest_result,
   c_show_selftest, c_start, c_next, c_stop, nvt_selftest_seq_ops, nvt_selftest_open,
   nvt_selftest_fops, nvt_mp_parse_ain, nvt_mp_parse_u32, nvt_mp_parse_array,
   nvt_mp_parse_pen_array, nvt_mp_parse_dt 保持不变 */

/* 删除以下整个函数块： */
/* - nvt_short_test */
/* - nvt_open_test */
/* - tp_selftest_read */
/* - tp_selftest_write */
/* - tp_selftest_open */
/* - tp_selftest_close */
/* - nvt_aftersales_test_ops */

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

    /* 删除 aftersales test proc 节点创建 */
    /* NVT_proc_aftersales_test_entry = proc_create("tp_selftest", 0644, NULL, &nvt_aftersales_test_ops); */

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

    /* 删除 aftersales test proc 节点移除 */
    /* if (NVT_proc_aftersales_test_entry != NULL) {
        remove_proc_entry("tp_selftest", NULL);
        NVT_proc_aftersales_test_entry = NULL;
        NVT_LOG("Removed /proc/%s\n", "tp_selftest");
    } */
}
#endif /* #if NVT_TOUCH_MP */