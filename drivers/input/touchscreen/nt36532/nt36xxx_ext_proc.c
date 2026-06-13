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

/*******************************************************
Description:
    Novatek touchscreen change mode function.
*******************************************************/
void nvt_change_mode(uint8_t mode)
{
    uint8_t buf[8] = {0};

    nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD);
    buf[0] = EVENT_MAP_HOST_CMD;
    buf[1] = mode;
    CTP_SPI_WRITE(ts->client, buf, 2);

    if (mode == NORMAL_MODE) {
        usleep_range(20000, 20000);
        buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
        buf[1] = HANDSHAKING_HOST_READY;
        CTP_SPI_WRITE(ts->client, buf, 2);
        usleep_range(20000, 20000);
    }
}

int32_t nvt_set_pen_inband_mode_1(uint8_t freq_idx, uint8_t x_term)
{
    uint8_t buf[8] = {0};
    int32_t i = 0;
    const int32_t retry = 5;

    nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD);
    buf[0] = EVENT_MAP_HOST_CMD;
    buf[1] = 0xC1;
    buf[2] = 0x02;
    buf[3] = freq_idx;
    buf[4] = x_term;
    CTP_SPI_WRITE(ts->client, buf, 5);

    for (i = 0; i < retry; i++) {
        buf[0] = EVENT_MAP_HOST_CMD;
        buf[1] = 0xFF;
        CTP_SPI_READ(ts->client, buf, 2);
        if (buf[1] == 0x00)
            break;
        usleep_range(10000, 10000);
    }

    if (i >= retry) {
        NVT_ERR("failed, i=%d, buf[1]=0x%02X\n", i, buf[1]);
        return -1;
    }
    return 0;
}

int32_t nvt_set_pen_normal_mode(void)
{
    uint8_t buf[8] = {0};
    int32_t i = 0;
    const int32_t retry = 5;

    nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD);
    buf[0] = EVENT_MAP_HOST_CMD;
    buf[1] = 0xC1;
    buf[2] = 0x04;
    CTP_SPI_WRITE(ts->client, buf, 3);

    for (i = 0; i < retry; i++) {
        buf[0] = EVENT_MAP_HOST_CMD;
        buf[1] = 0xFF;
        CTP_SPI_READ(ts->client, buf, 2);
        if (buf[1] == 0x00)
            break;
        usleep_range(10000, 10000);
    }

    if (i >= retry) {
        NVT_ERR("failed, i=%d, buf[1]=0x%02X\n", i, buf[1]);
        return -1;
    }
    return 0;
}

uint8_t nvt_get_fw_pipe(void)
{
    uint8_t buf[8] = {0};

    nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE);
    buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
    buf[1] = 0x00;
    CTP_SPI_READ(ts->client, buf, 2);

    return (buf[1] & 0x01);
}

void nvt_read_mdata(uint32_t xdata_addr, uint32_t xdata_btn_addr)
{
    int32_t transfer_len = 0;
    int32_t i = 0;
    int32_t j = 0;
    int32_t k = 0;
    uint8_t buf[BUS_TRANSFER_LENGTH + 2] = {0};
    uint32_t head_addr = 0;
    int32_t dummy_len = 0;
    int32_t data_len = 0;
    int32_t residual_len = 0;

    if (BUS_TRANSFER_LENGTH <= XDATA_SECTOR_SIZE)
        transfer_len = BUS_TRANSFER_LENGTH;
    else
        transfer_len = XDATA_SECTOR_SIZE;

    head_addr = xdata_addr - (xdata_addr % XDATA_SECTOR_SIZE);
    dummy_len = xdata_addr - head_addr;
    data_len = ts->x_num * ts->y_num * 2;
    residual_len = (head_addr + dummy_len + data_len) % XDATA_SECTOR_SIZE;

    for (i = 0; i < ((dummy_len + data_len) / XDATA_SECTOR_SIZE); i++) {
        nvt_set_page(head_addr + XDATA_SECTOR_SIZE * i);
        for (j = 0; j < (XDATA_SECTOR_SIZE / transfer_len); j++) {
            buf[0] = transfer_len * j;
            CTP_SPI_READ(ts->client, buf, transfer_len + 1);
            for (k = 0; k < transfer_len; k++) {
                xdata_tmp[XDATA_SECTOR_SIZE * i + transfer_len * j + k] = buf[k + 1];
            }
        }
    }

    if (residual_len != 0) {
        nvt_set_page(xdata_addr + data_len - residual_len);
        for (j = 0; j < (residual_len / transfer_len + 1); j++) {
            buf[0] = transfer_len * j;
            CTP_SPI_READ(ts->client, buf, transfer_len + 1);
            for (k = 0; k < transfer_len; k++) {
                xdata_tmp[(dummy_len + data_len - residual_len) + transfer_len * j + k] = buf[k + 1];
            }
        }
    }

    for (i = 0; i < (data_len / 2); i++) {
        xdata[i] = (int16_t)(xdata_tmp[dummy_len + i * 2] + 256 * xdata_tmp[dummy_len + i * 2 + 1]);
    }

    nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
}

void nvt_get_mdata(int32_t *buf, uint8_t *m_x_num, uint8_t *m_y_num)
{
    *m_x_num = ts->x_num;
    *m_y_num = ts->y_num;
    memcpy(buf, xdata, ((ts->x_num * ts->y_num + TOUCH_KEY_NUM) * sizeof(int32_t)));
}

void nvt_read_get_num_mdata(uint32_t xdata_addr, int32_t *buffer, uint32_t num)
{
    int32_t transfer_len = 0;
    int32_t i = 0;
    int32_t j = 0;
    int32_t k = 0;
    uint8_t buf[BUS_TRANSFER_LENGTH + 2] = {0};
    uint32_t head_addr = 0;
    int32_t dummy_len = 0;
    int32_t data_len = 0;
    int32_t residual_len = 0;

    if (BUS_TRANSFER_LENGTH <= XDATA_SECTOR_SIZE)
        transfer_len = BUS_TRANSFER_LENGTH;
    else
        transfer_len = XDATA_SECTOR_SIZE;

    head_addr = xdata_addr - (xdata_addr % XDATA_SECTOR_SIZE);
    dummy_len = xdata_addr - head_addr;
    data_len = num * 2;
    residual_len = (head_addr + dummy_len + data_len) % XDATA_SECTOR_SIZE;

    for (i = 0; i < ((dummy_len + data_len) / XDATA_SECTOR_SIZE); i++) {
        nvt_set_page(head_addr + XDATA_SECTOR_SIZE * i);
        for (j = 0; j < (XDATA_SECTOR_SIZE / transfer_len); j++) {
            buf[0] = transfer_len * j;
            CTP_SPI_READ(ts->client, buf, transfer_len + 1);
            for (k = 0; k < transfer_len; k++) {
                xdata_tmp[XDATA_SECTOR_SIZE * i + transfer_len * j + k] = buf[k + 1];
            }
        }
    }

    if (residual_len != 0) {
        nvt_set_page(xdata_addr + data_len - residual_len);
        for (j = 0; j < (residual_len / transfer_len + 1); j++) {
            buf[0] = transfer_len * j;
            CTP_SPI_READ(ts->client, buf, transfer_len + 1);
            for (k = 0; k < transfer_len; k++) {
                xdata_tmp[(dummy_len + data_len - residual_len) + transfer_len * j + k] = buf[k + 1];
            }
        }
    }

    for (i = 0; i < (data_len / 2); i++) {
        buffer[i] = (int16_t)(xdata_tmp[dummy_len + i * 2] + 256 * xdata_tmp[dummy_len + i * 2 + 1]);
    }

    nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
}

static int32_t c_fw_version_show(struct seq_file *m, void *v)
{
    seq_printf(m, "fw_ver=%d, x_num=%d, y_num=%d, button_num=%d\n", ts->fw_ver, ts->x_num, ts->y_num, ts->max_button_num);
    return 0;
}

static int32_t c_show(struct seq_file *m, void *v)
{
    int32_t i = 0;
    int32_t j = 0;

    for (i = 0; i < ts->y_num; i++) {
        for (j = 0; j < ts->x_num; j++) {
            seq_printf(m, "%5d, ", xdata[i * ts->x_num + j]);
        }
        seq_puts(m, "\n");
    }

    seq_printf(m, "\n\n");
    return 0;
}

static int32_t c_pen_1d_diff_show(struct seq_file *m, void *v)
{
    int32_t i = 0;

    seq_printf(m, "Tip X:\n");
    for (i = 0; i < ts->x_num; i++) {
        seq_printf(m, "%5d, ", xdata_pen_tip_x[i]);
    }
    seq_puts(m, "\n");
    seq_printf(m, "Tip Y:\n");
    for (i = 0; i < ts->y_num; i++) {
        seq_printf(m, "%5d, ", xdata_pen_tip_y[i]);
    }
    seq_puts(m, "\n");
    seq_printf(m, "Ring X:\n");
    for (i = 0; i < ts->x_num; i++) {
        seq_printf(m, "%5d, ", xdata_pen_ring_x[i]);
    }
    seq_puts(m, "\n");
    seq_printf(m, "Ring Y:\n");
    for (i = 0; i < ts->y_num; i++) {
        seq_printf(m, "%5d, ", xdata_pen_ring_y[i]);
    }
    seq_puts(m, "\n");

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

const struct seq_operations nvt_fw_version_seq_ops = {
    .start  = c_start,
    .next   = c_next,
    .stop   = c_stop,
    .show   = c_fw_version_show,
};

const struct seq_operations nvt_seq_ops = {
    .start  = c_start,
    .next   = c_next,
    .stop   = c_stop,
    .show   = c_show,
};

const struct seq_operations nvt_pen_diff_seq_ops = {
    .start  = c_start,
    .next   = c_next,
    .stop   = c_stop,
    .show   = c_pen_1d_diff_show,
};

/*******************************************************
Description:
    Novatek touchscreen /proc/nvt_fw_version open function.
*******************************************************/
static int32_t nvt_fw_version_open(struct inode *inode, struct file *file)
{
    if (mutex_lock_interruptible(&ts->lock)) {
        return -ERESTARTSYS;
    }

    NVT_LOG("++\n");

#if NVT_TOUCH_ESD_PROTECT
    nvt_esd_check_enable(false);
#endif

    if (nvt_get_fw_info()) {
        mutex_unlock(&ts->lock);
        return -EAGAIN;
    }

    mutex_unlock(&ts->lock);
    NVT_LOG("--\n");

    return seq_open(file, &nvt_fw_version_seq_ops);
}

/*******************************************************
Description:
    Novatek touchscreen /proc/nvt_baseline open function.
*******************************************************/
static int32_t nvt_baseline_open(struct inode *inode, struct file *file)
{
    if (mutex_lock_interruptible(&ts->lock)) {
        return -ERESTARTSYS;
    }

    NVT_LOG("++\n");

#if NVT_TOUCH_ESD_PROTECT
    nvt_esd_check_enable(false);
#endif

    if (nvt_clear_fw_status()) {
        mutex_unlock(&ts->lock);
        return -EAGAIN;
    }

    nvt_change_mode(TEST_MODE_2);

    if (nvt_check_fw_status()) {
        mutex_unlock(&ts->lock);
        return -EAGAIN;
    }

    if (nvt_get_fw_info()) {
        mutex_unlock(&ts->lock);
        return -EAGAIN;
    }

    nvt_read_mdata(ts->mmap->BASELINE_ADDR, ts->mmap->BASELINE_BTN_ADDR);
    nvt_change_mode(NORMAL_MODE);

    mutex_unlock(&ts->lock);
    NVT_LOG("--\n");

    return seq_open(file, &nvt_seq_ops);
}

/*******************************************************
Description:
    Novatek touchscreen /proc/nvt_raw open function.
*******************************************************/
static int32_t nvt_raw_open(struct inode *inode, struct file *file)
{
    if (mutex_lock_interruptible(&ts->lock)) {
        return -ERESTARTSYS;
    }

    NVT_LOG("++\n");

#if NVT_TOUCH_ESD_PROTECT
    nvt_esd_check_enable(false);
#endif

    if (nvt_clear_fw_status()) {
        mutex_unlock(&ts->lock);
        return -EAGAIN;
    }

    nvt_change_mode(TEST_MODE_2);

    if (nvt_check_fw_status()) {
        mutex_unlock(&ts->lock);
        return -EAGAIN;
    }

    if (nvt_get_fw_info()) {
        mutex_unlock(&ts->lock);
        return -EAGAIN;
    }

    if (nvt_get_fw_pipe() == 0)
        nvt_read_mdata(ts->mmap->RAW_PIPE0_ADDR, ts->mmap->RAW_BTN_PIPE0_ADDR);
    else
        nvt_read_mdata(ts->mmap->RAW_PIPE1_ADDR, ts->mmap->RAW_BTN_PIPE1_ADDR);

    nvt_change_mode(NORMAL_MODE);

    mutex_unlock(&ts->lock);
    NVT_LOG("--\n");

    return seq_open(file, &nvt_seq_ops);
}

/*******************************************************
Description:
    Novatek touchscreen /proc/nvt_diff open function.
*******************************************************/
static int32_t nvt_diff_open(struct inode *inode, struct file *file)
{
    if (mutex_lock_interruptible(&ts->lock)) {
        return -ERESTARTSYS;
    }

    NVT_LOG("++\n");

#if NVT_TOUCH_ESD_PROTECT
    nvt_esd_check_enable(false);
#endif

    if (nvt_clear_fw_status()) {
        mutex_unlock(&ts->lock);
        return -EAGAIN;
    }

    nvt_change_mode(TEST_MODE_2);

    if (nvt_check_fw_status()) {
        mutex_unlock(&ts->lock);
        return -EAGAIN;
    }

    if (nvt_get_fw_info()) {
        mutex_unlock(&ts->lock);
        return -EAGAIN;
    }

    if (nvt_get_fw_pipe() == 0)
        nvt_read_mdata(ts->mmap->DIFF_PIPE0_ADDR, ts->mmap->DIFF_BTN_PIPE0_ADDR);
    else
        nvt_read_mdata(ts->mmap->DIFF_PIPE1_ADDR, ts->mmap->DIFF_BTN_PIPE1_ADDR);

    nvt_change_mode(NORMAL_MODE);

    mutex_unlock(&ts->lock);
    NVT_LOG("--\n");

    return seq_open(file, &nvt_seq_ops);
}

/*******************************************************
Description:
    Novatek touchscreen /proc/nvt_pen_diff open function.
*******************************************************/
static int32_t nvt_pen_diff_open(struct inode *inode, struct file *file)
{
    if (mutex_lock_interruptible(&ts->lock)) {
        return -ERESTARTSYS;
    }

    NVT_LOG("++\n");

#if NVT_TOUCH_ESD_PROTECT
    nvt_esd_check_enable(false);
#endif

    if (nvt_set_pen_inband_mode_1(0xFF, 0x00)) {
        mutex_unlock(&ts->lock);
        return -EAGAIN;
    }

    if (nvt_check_fw_reset_state(RESET_STATE_NORMAL_RUN)) {
        mutex_unlock(&ts->lock);
        return -EAGAIN;
    }

    if (nvt_clear_fw_status()) {
        mutex_unlock(&ts->lock);
        return -EAGAIN;
    }

    nvt_change_mode(TEST_MODE_2);

    if (nvt_check_fw_status()) {
        mutex_unlock(&ts->lock);
        return -EAGAIN;
    }

    if (nvt_get_fw_info()) {
        mutex_unlock(&ts->lock);
        return -EAGAIN;
    }

    nvt_read_get_num_mdata(ts->mmap->PEN_1D_DIFF_TIP_X_ADDR, xdata_pen_tip_x, ts->x_num);
    nvt_read_get_num_mdata(ts->mmap->PEN_1D_DIFF_TIP_Y_ADDR, xdata_pen_tip_y, ts->y_num);
    nvt_read_get_num_mdata(ts->mmap->PEN_1D_DIFF_RING_X_ADDR, xdata_pen_ring_x, ts->x_num);
    nvt_read_get_num_mdata(ts->mmap->PEN_1D_DIFF_RING_Y_ADDR, xdata_pen_ring_y, ts->y_num);

    nvt_change_mode(NORMAL_MODE);
    nvt_set_pen_normal_mode();
    nvt_check_fw_reset_state(RESET_STATE_NORMAL_RUN);

    mutex_unlock(&ts->lock);
    NVT_LOG("--\n");

    return seq_open(file, &nvt_pen_diff_seq_ops);
}

/* ========== fops 结构体定义 ========== */

#ifdef HAVE_PROC_OPS
static const struct proc_ops nvt_fw_version_fops = {
    .proc_open = nvt_fw_version_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = seq_release,
};

static const struct proc_ops nvt_baseline_fops = {
    .proc_open = nvt_baseline_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = seq_release,
};

static const struct proc_ops nvt_raw_fops = {
    .proc_open = nvt_raw_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = seq_release,
};

static const struct proc_ops nvt_diff_fops = {
    .proc_open = nvt_diff_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = seq_release,
};

static const struct proc_ops nvt_pen_diff_fops = {
    .proc_open = nvt_pen_diff_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = seq_release,
};
#else
static const struct file_operations nvt_fw_version_fops = {
    .owner = THIS_MODULE,
    .open = nvt_fw_version_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = seq_release,
};

static const struct file_operations nvt_baseline_fops = {
    .owner = THIS_MODULE,
    .open = nvt_baseline_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = seq_release,
};

static const struct file_operations nvt_raw_fops = {
    .owner = THIS_MODULE,
    .open = nvt_raw_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = seq_release,
};

static const struct file_operations nvt_diff_fops = {
    .owner = THIS_MODULE,
    .open = nvt_diff_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = seq_release,
};

static const struct file_operations nvt_pen_diff_fops = {
    .owner = THIS_MODULE,
    .open = nvt_pen_diff_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = seq_release,
};
#endif

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
}
#endif /* NVT_TOUCH_EXT_PROC */