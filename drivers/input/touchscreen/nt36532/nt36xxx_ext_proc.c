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

#include "nt36xxx.h"

#if NVT_TOUCH_EXT_PROC
#define NVT_FW_VERSION "nvt_fw_version"
#define NVT_BASELINE "nvt_baseline"
#define NVT_RAW "nvt_raw"
#define NVT_DIFF "nvt_diff"
#define NVT_PEN_DIFF "nvt_pen_diff"
// 删除： #define NVT_XIAOMI_LOCKDOWN_INFO "tp_lockdown_info"

#define NORMAL_MODE 0x00
#define TEST_MODE_2 0x22
#define HANDSHAKING_HOST_READY 0xBB

#define XDATA_SECTOR_SIZE   256

static uint8_t xdata_tmp[8192] = {0};
static int32_t xdata[4096] = {0};
static int32_t xdata_pen_tip_x[256] = {0};
static int32_t xdata_pen_tip_y[256] = {0};
static int32_t xdata_pen_ring_x[256] = {0};
static int32_t xdata_pen_ring_y[256] = {0};

static struct proc_dir_entry *NVT_proc_fw_version_entry;
static struct proc_dir_entry *NVT_proc_baseline_entry;
static struct proc_dir_entry *NVT_proc_raw_entry;
static struct proc_dir_entry *NVT_proc_diff_entry;
static struct proc_dir_entry *NVT_proc_pen_diff_entry;
// 删除： static struct proc_dir_entry *NVT_proc_xiaomi_lockdown_info_entry;

// 删除： extern int dsi_panel_lockdown_info_read(unsigned char *plockdowninfo);

/* 其余函数保持不变 */
// nvt_change_mode, nvt_set_pen_inband_mode_1, nvt_set_pen_normal_mode,
// nvt_get_fw_pipe, nvt_read_mdata, nvt_get_mdata, nvt_read_get_num_mdata,
// c_fw_version_show, c_show, c_pen_1d_diff_show, c_start, c_next, c_stop,
// nvt_fw_version_open, nvt_baseline_open, nvt_raw_open, nvt_diff_open,
// nvt_pen_diff_open 等函数保持不变

// 删除整个 nvt_xiaomi_lockdown_info_show 函数
// 删除整个 nvt_xiaomi_lockdown_info_open 函数
// 删除 nvt_xiaomi_lockdown_info_fops 结构体

/*******************************************************
Description:
    Novatek touchscreen extra function proc. file node
    initial function.
*******************************************************/
int32_t nvt_extra_proc_init(void)
{
    NVT_proc_fw_version_entry = proc_create(NVT_FW_VERSION, 0444, NULL, &nvt_fw_version_fops);
    if (NVT_proc_fw_version_entry == NULL) {
        NVT_ERR("create proc/%s Failed!\n", NVT_FW_VERSION);
        return -ENOMEM;
    } else {
        NVT_LOG("create proc/%s Succeeded!\n", NVT_FW_VERSION);
    }

    NVT_proc_baseline_entry = proc_create(NVT_BASELINE, 0444, NULL, &nvt_baseline_fops);
    if (NVT_proc_baseline_entry == NULL) {
        NVT_ERR("create proc/%s Failed!\n", NVT_BASELINE);
        return -ENOMEM;
    } else {
        NVT_LOG("create proc/%s Succeeded!\n", NVT_BASELINE);
    }

    NVT_proc_raw_entry = proc_create(NVT_RAW, 0444, NULL, &nvt_raw_fops);
    if (NVT_proc_raw_entry == NULL) {
        NVT_ERR("create proc/%s Failed!\n", NVT_RAW);
        return -ENOMEM;
    } else {
        NVT_LOG("create proc/%s Succeeded!\n", NVT_RAW);
    }

    NVT_proc_diff_entry = proc_create(NVT_DIFF, 0444, NULL, &nvt_diff_fops);
    if (NVT_proc_diff_entry == NULL) {
        NVT_ERR("create proc/%s Failed!\n", NVT_DIFF);
        return -ENOMEM;
    } else {
        NVT_LOG("create proc/%s Succeeded!\n", NVT_DIFF);
    }

    if (ts->pen_support) {
        NVT_proc_pen_diff_entry = proc_create(NVT_PEN_DIFF, 0444, NULL, &nvt_pen_diff_fops);
        if (NVT_proc_pen_diff_entry == NULL) {
            NVT_ERR("create proc/%s Failed!\n", NVT_PEN_DIFF);
            return -ENOMEM;
        } else {
            NVT_LOG("create proc/%s Succeeded!\n", NVT_PEN_DIFF);
        }
    }

    // 删除小米 lockdown_info proc 节点创建

    return 0;
}

/*******************************************************
Description:
    Novatek touchscreen extra function proc. file node
    deinitial function.
*******************************************************/
void nvt_extra_proc_deinit(void)
{
    if (NVT_proc_fw_version_entry != NULL) {
        remove_proc_entry(NVT_FW_VERSION, NULL);
        NVT_proc_fw_version_entry = NULL;
        NVT_LOG("Removed /proc/%s\n", NVT_FW_VERSION);
    }

    if (NVT_proc_baseline_entry != NULL) {
        remove_proc_entry(NVT_BASELINE, NULL);
        NVT_proc_baseline_entry = NULL;
        NVT_LOG("Removed /proc/%s\n", NVT_BASELINE);
    }

    if (NVT_proc_raw_entry != NULL) {
        remove_proc_entry(NVT_RAW, NULL);
        NVT_proc_raw_entry = NULL;
        NVT_LOG("Removed /proc/%s\n", NVT_RAW);
    }

    if (NVT_proc_diff_entry != NULL) {
        remove_proc_entry(NVT_DIFF, NULL);
        NVT_proc_diff_entry = NULL;
        NVT_LOG("Removed /proc/%s\n", NVT_DIFF);
    }

    if (ts->pen_support) {
        if (NVT_proc_pen_diff_entry != NULL) {
            remove_proc_entry(NVT_PEN_DIFF, NULL);
            NVT_proc_pen_diff_entry = NULL;
            NVT_LOG("Removed /proc/%s\n", NVT_PEN_DIFF);
        }
    }

    // 删除小米 lockdown_info proc 节点移除
}
#endif /* NVT_TOUCH_EXT_PROC */