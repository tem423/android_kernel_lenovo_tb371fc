// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/backlight.h>
#include <drm/drmP.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/hqsysfs.h>

/*Spruce code for OSPURCET-2314 by gaoxue4 at 2023/3/3 start*/
#include "../../../input/touchscreen/NT36532/nt36xxx.h"
/*Spruce code for OSPURCET-2314 by gaoxue4 at 2023/3/3 end*/

#define CONFIG_MTK_PANEL_EXT
#if defined(CONFIG_MTK_PANEL_EXT)
#include "../mediatek/mtk_panel_ext.h"
#include "../mediatek/mtk_log.h"
#include "../mediatek/mtk_drm_graphics_base.h"
#endif

/*Spruce code for OSPURCET-1272 by zhuhao6 at 2023/2/21 start*/
#define PHYSICAL_WIDTH			171009
#define PHYSICAL_HEIGHT			273615
/*Spruce code for OSPURCET-1272 by zhuhao6 at 2023/2/21 end*/

extern void lcd_set_bias(int enable);
extern int lcd_bias_set_led_brightness(int value);
extern int lcd_bl_set_led_brightness(int value);

/*Spruce code for OSPURCET-1810 by gaoxue4 at 2023/2/23 start*/
extern int32_t nvt_update_firmware(char *firmware_name);
extern bool nvt_esd_flag;
/*Spruce code for OSPURCET-1810 by gaoxue4 at 2023/2/23 end*/

/*Spruce code for OSPURCET-112 by zenghui4 at 2023/1/5 start*/
extern bool nvt_gesture_flag;
/*Spruce code for OSPURCET-112 by zenghui4 at 2023/1/5 end*/

#include "ite6113.h"

static struct it6112 it6112_dev;
static struct init_table_info table_info;

struct lcm {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct gpio_desc *pm_enable_gpio;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *bias_pos, *bias_neg;
	struct gpio_desc *it6112_1v0_en, *it6112_1v8_en;
	/*Spruce code for OSPURCET-612 by zhangkx10 at 2023/1/19 start*/
	struct regulator *v1_8, *v1_0, *vdd;
	/*Spruce code for OSPURCET-612 by zhangkx10 at 2023/1/19 end*/
	struct it6112 *it6112_client;

	bool prepared;
	bool enabled;

	int error;
};

#define lcm_dcs_write_seq(ctx, seq...) \
	({\
	const u8 d[] = { seq };\
	BUILD_BUG_ON_MSG(ARRAY_SIZE(d) > 64, "DCS sequence too big for stack");\
	lcm_dcs_write(ctx, d, ARRAY_SIZE(d));\
	})

#define lcm_dcs_write_seq_static(ctx, seq...) \
	({\
	static const u8 d[] = { seq };\
	lcm_dcs_write(ctx, d, ARRAY_SIZE(d));\
	})

static inline struct lcm *panel_to_lcm(struct drm_panel *panel)
{
	return container_of(panel, struct lcm, panel);
}

#ifdef PANEL_SUPPORT_READBACK
static int lcm_dcs_read(struct lcm *ctx, u8 cmd, void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;

	if (ctx->error < 0)
		return 0;

	ret = mipi_dsi_dcs_read(dsi, cmd, data, len);
	if (ret < 0) {
		dev_info(ctx->dev, "error %d reading dcs seq:(%#x)\n", ret, cmd);
		ctx->error = ret;
	}

	return ret;
}

static void lcm_panel_get_data(struct lcm *ctx)
{
	u8 buffer[3] = {0};
	static int ret;

	if (ret == 0) {
		ret = lcm_dcs_read(ctx,  0x0A, buffer, 1);
		dev_info(ctx->dev, "return %d data(0x%08x) to dsi engine\n",
				ret, buffer[0] | (buffer[1] << 8));
	}
}
#endif

/*Spruce code for OSPURCET-612 by zhangkx10 at 2023/1/19 start*/
static int lcm_vdd_enable(struct lcm *ctx)
{
	int ret = 0;
	unsigned int vol = 0;

	if (!ctx->vdd) {
		dev_info(ctx->dev, "vdd connot find\n");
		return -1;
	}

	ret = regulator_set_voltage(ctx->vdd, 1800000, 1800000);
	if (ret) {
		dev_info(ctx->dev, "vdd set voltage fail\n");
		return ret;
	}

	vol = regulator_get_voltage(ctx->vdd);
	if (vol == 1800000)
		dev_info(ctx->dev, "vdd check vol=1800000 pass!\n");
	else
		dev_info(ctx->dev, "vdd check vol=1800000 fail!\n");

	ret = regulator_enable(ctx->vdd);
	if (ret)
		dev_info(ctx->dev, "vdd enable fail\n");

	return ret;
}

static int lcm_vdd_disable(struct lcm *ctx)
{
	int ret = 0;
	int isenable = 0;

	if (!ctx->vdd) {
		dev_info(ctx->dev, "vdd connot find\n");
		return -1;
	}

	isenable = regulator_is_enabled(ctx->vdd);
	if (isenable) {
		ret = regulator_disable(ctx->vdd);
		if (ret)
			dev_info(ctx->dev, "vdd disable fail\n");
	}

	return ret;
}
/*Spruce code for OSPURCET-612 by zhangkx10 at 2023/1/19 end*/

static int lcm_v1_8_enable(struct lcm *ctx)
{
	int ret = 0;
	unsigned int vol = 0;

	if (!ctx->v1_8) {
		dev_info(ctx->dev, "v1_8 connot find\n");
		return -1;
	}

	ret = regulator_set_voltage(ctx->v1_8, 1800000, 1800000);
	if (ret) {
		dev_info(ctx->dev, "v1_8 set voltage fail\n");
		return ret;
	}

	vol = regulator_get_voltage(ctx->v1_8);
	if (vol == 1800000)
		dev_info(ctx->dev, "check vol=1800000 pass!\n");
	else
		dev_info(ctx->dev, "check vol=1800000 fail!\n");

	ret = regulator_enable(ctx->v1_8);
	if (ret)
		dev_info(ctx->dev, "v1_8 enable fail\n");

	return ret;
}

static int lcm_v1_8_disable(struct lcm *ctx)
{
	int ret = 0;
	int isenable = 0;

	if (!ctx->v1_8) {
		dev_info(ctx->dev, "v1_8 connot find\n");
		return -1;
	}

	isenable = regulator_is_enabled(ctx->v1_8);
	if (isenable) {
		ret = regulator_disable(ctx->v1_8);
		if (ret)
			dev_info(ctx->dev, "v1_8 disable fail\n");
	}

	return ret;
}

static struct dcs_setting_entry lcm_init_table[] = {
	{REGW0, LP_CMD_LPDT, 0x23, 2, {0xFF, 0x20}},
	{REGW1, LP_CMD_LPDT, 0x23, 2, {0xFB, 0x01}},
	{REGW2, LP_CMD_LPDT, 0x15, 2, {0x08, 0x23}},
	{REGW3, LP_CMD_LPDT, 0x15, 2, {0x10, 0x46}},
	{REGW4, LP_CMD_LPDT, 0x15, 2, {0x44, 0x01}},
	{REGW5, LP_CMD_LPDT, 0x15, 2, {0x65, 0xAA}},
	{REGW6, LP_CMD_LPDT, 0x15, 2, {0x6D, 0xAA}},
	{REGW7, LP_CMD_LPDT, 0x15, 2, {0x78, 0x93}},
	{REGW8, LP_CMD_LPDT, 0x15, 2, {0x89, 0x73}},
	{REGW9, LP_CMD_LPDT, 0x15, 2, {0x8A, 0x73}},
	{REGW10, LP_CMD_LPDT, 0x15, 2, {0x8D, 0xAF}},
	{REGW11, LP_CMD_LPDT, 0x15, 2, {0x95, 0xEB}},
	{REGW12, LP_CMD_LPDT, 0x15, 2, {0x96, 0xEB}},
	{REGW13, LP_CMD_LPDT, 0x29, 17, {0xB0, 0x00, 0x00, 0x00, 0x29, 0x00, 0x5F, 0x00, 0x87, 0x00, 0xA6, 0x00, 0xC1, 0x00, 0xD8, 0x00, 0xEC}},
	{REGW14, LP_CMD_LPDT, 0x29, 17, {0xB1, 0x00, 0xFE, 0x01, 0x38, 0x01, 0x63, 0x01, 0xA3, 0x01, 0xD3, 0x02, 0x1B, 0x02, 0x54, 0x02, 0x55}},
	{REGW15, LP_CMD_LPDT, 0x29, 17, {0xB2, 0x02, 0x8C, 0x02, 0xC9, 0x02, 0xF1, 0x03, 0x24, 0x03, 0x45, 0x03, 0x71, 0x03, 0x7F, 0x03, 0x8E}},
	{REGW16, LP_CMD_LPDT, 0x29, 15, {0xB3, 0x03, 0x9E, 0x03, 0xAE, 0x03, 0xD0, 0x03, 0xF0, 0x03, 0xFE, 0x03, 0xFF, 0x00, 0x00}},
	{REGW17, LP_CMD_LPDT, 0x29, 17, {0xB4, 0x00, 0x00, 0x00, 0x29, 0x00, 0x5F, 0x00, 0x87, 0x00, 0xA6, 0x00, 0xC1, 0x00, 0xD8, 0x00, 0xEC}},
	{REGW18, LP_CMD_LPDT, 0x29, 17, {0xB5, 0x00, 0xFE, 0x01, 0x38, 0x01, 0x63, 0x01, 0xA3, 0x01, 0xD3, 0x02, 0x1B, 0x02, 0x54, 0x02, 0x55}},
	{REGW19, LP_CMD_LPDT, 0x29, 17, {0xB6, 0x02, 0x8C, 0x02, 0xC9, 0x02, 0xF1, 0x03, 0x24, 0x03, 0x45, 0x03, 0x71, 0x03, 0x7F, 0x03, 0x8E}},
	{REGW20, LP_CMD_LPDT, 0x29, 15, {0xB7, 0x03, 0x9E, 0x03, 0xAE, 0x03, 0xD0, 0x03, 0xF0, 0x03, 0xFE, 0x03, 0xFF, 0x00, 0x00}},
	{REGW21, LP_CMD_LPDT, 0x29, 17, {0xB8, 0x00, 0x00, 0x00, 0x29, 0x00, 0x5F, 0x00, 0x87, 0x00, 0xA6, 0x00, 0xC1, 0x00, 0xD8, 0x00, 0xEC}},
	{REGW22, LP_CMD_LPDT, 0x29, 17, {0xB9, 0x00, 0xFE, 0x01, 0x38, 0x01, 0x63, 0x01, 0xA3, 0x01, 0xD3, 0x02, 0x1B, 0x02, 0x54, 0x02, 0x55}},
	{REGW23, LP_CMD_LPDT, 0x29, 17, {0xBA, 0x02, 0x8C, 0x02, 0xC9, 0x02, 0xF1, 0x03, 0x24, 0x03, 0x45, 0x03, 0x71, 0x03, 0x7F, 0x03, 0x8E}},
	{REGW24, LP_CMD_LPDT, 0x29, 17, {0xBB, 0x03, 0x9E, 0x03, 0xAE, 0x03, 0xD0, 0x03, 0xF0, 0x03, 0xFE, 0x03, 0xFF, 0x00, 0x00}},
	{REGW25, LP_CMD_LPDT, 0x23, 2, {0xC6, 0xD8}},
	{REGW26, LP_CMD_LPDT, 0x23, 2, {0xC7, 0x54}},
	{REGW27, LP_CMD_LPDT, 0x23, 2, {0xC8, 0x53}},
	{REGW28, LP_CMD_LPDT, 0x23, 2, {0xC9, 0x42}},
	{REGW29, LP_CMD_LPDT, 0x23, 2, {0xCA, 0x31}},
	{REGW30, LP_CMD_LPDT, 0x23, 2, {0xCC, 0x22}},
	{REGW31, LP_CMD_LPDT, 0x23, 2, {0xCD, 0x53}},
	{REGW32, LP_CMD_LPDT, 0x23, 2, {0xCE, 0x54}},
	{REGW33, LP_CMD_LPDT, 0x23, 2, {0xCF, 0xA6}},
	{REGW34, LP_CMD_LPDT, 0x23, 2, {0xD0, 0xA4}},
	{REGW35, LP_CMD_LPDT, 0x23, 2, {0xD1, 0xF8}},
	{REGW36, LP_CMD_LPDT, 0x23, 2, {0xD2, 0xD8}},
	{REGW37, LP_CMD_LPDT, 0x23, 2, {0xD3, 0x54}},
	{REGW38, LP_CMD_LPDT, 0x23, 2, {0xD4, 0x53}},
	{REGW39, LP_CMD_LPDT, 0x23, 2, {0xD5, 0x42}},
	{REGW40, LP_CMD_LPDT, 0x23, 2, {0xD6, 0x31}},
	{REGW41, LP_CMD_LPDT, 0x23, 2, {0xD8, 0x22}},
	{REGW42, LP_CMD_LPDT, 0x23, 2, {0xD9, 0x53}},
	{REGW43, LP_CMD_LPDT, 0x23, 2, {0xDA, 0x54}},
	{REGW44, LP_CMD_LPDT, 0x23, 2, {0xDB, 0xA6}},
	{REGW45, LP_CMD_LPDT, 0x23, 2, {0xDC, 0xA4}},
	{REGW46, LP_CMD_LPDT, 0x23, 2, {0xDD, 0xF8}},
	{REGW47, LP_CMD_LPDT, 0x23, 2, {0xDE, 0xD8}},
	{REGW48, LP_CMD_LPDT, 0x23, 2, {0xDF, 0x54}},
	{REGW49, LP_CMD_LPDT, 0x23, 2, {0xE0, 0x53}},
	{REGW50, LP_CMD_LPDT, 0x23, 2, {0xE1, 0x42}},
	{REGW51, LP_CMD_LPDT, 0x23, 2, {0xE2, 0x31}},
	{REGW52, LP_CMD_LPDT, 0x23, 2, {0xE4, 0x22}},
	{REGW53, LP_CMD_LPDT, 0x23, 2, {0xE5, 0x53}},
	{REGW54, LP_CMD_LPDT, 0x23, 2, {0xE6, 0x54}},
	{REGW55, LP_CMD_LPDT, 0x23, 2, {0xE7, 0xA6}},
	{REGW56, LP_CMD_LPDT, 0x23, 2, {0xE8, 0xA4}},
	{REGW57, LP_CMD_LPDT, 0x23, 2, {0xE9, 0xF8}},

	{REGW58, LP_CMD_LPDT, 0x23, 2, {0xFF, 0x21}},
	{REGW59, LP_CMD_LPDT, 0x23, 2, {0xFB, 0x01}},
	{REGW60, LP_CMD_LPDT, 0x29, 17, {0xB0, 0x00, 0x00, 0x00, 0x29, 0x00, 0x5F, 0x00, 0x87, 0x00, 0xA6, 0x00, 0xC1, 0x00, 0xD8, 0x00, 0xEC}},
	{REGW61, LP_CMD_LPDT, 0x29, 17, {0xB1, 0x00, 0xFE, 0x01, 0x38, 0x01, 0x63, 0x01, 0xA3, 0x01, 0xD3, 0x02, 0x1B, 0x02, 0x54, 0x02, 0x55}},
	{REGW62, LP_CMD_LPDT, 0x29, 17, {0xB2, 0x02, 0x8C, 0x02, 0xC9, 0x02, 0xF1, 0x03, 0x24, 0x03, 0x45, 0x03, 0x71, 0x03, 0x7F, 0x03, 0x8E}},
	{REGW63, LP_CMD_LPDT, 0x29, 15, {0xB3, 0x03, 0x9E, 0x03, 0xAE, 0x03, 0xD0, 0x03, 0xF0, 0x03, 0xFE, 0x03, 0xFF, 0x00, 0x00}},
	{REGW64, LP_CMD_LPDT, 0x29, 17, {0xB4, 0x00, 0x00, 0x00, 0x29, 0x00, 0x5F, 0x00, 0x87, 0x00, 0xA6, 0x00, 0xC1, 0x00, 0xD8, 0x00, 0xEC}},
	{REGW65, LP_CMD_LPDT, 0x29, 17, {0xB5, 0x00, 0xFE, 0x01, 0x38, 0x01, 0x63, 0x01, 0xA3, 0x01, 0xD3, 0x02, 0x1B, 0x02, 0x54, 0x02, 0x55}},
	{REGW66, LP_CMD_LPDT, 0x29, 17, {0xB6, 0x02, 0x8C, 0x02, 0xC9, 0x02, 0xF1, 0x03, 0x24, 0x03, 0x45, 0x03, 0x71, 0x03, 0x7F, 0x03, 0x8E}},
	{REGW67, LP_CMD_LPDT, 0x29, 15, {0xB7, 0x03, 0x9E, 0x03, 0xAE, 0x03, 0xD0, 0x03, 0xF0, 0x03, 0xFE, 0x03, 0xFF, 0x00, 0x00}},
	{REGW68, LP_CMD_LPDT, 0x29, 17, {0xB8, 0x00, 0x00, 0x00, 0x29, 0x00, 0x5F, 0x00, 0x87, 0x00, 0xA6, 0x00, 0xC1, 0x00, 0xD8, 0x00, 0xEC}},
	{REGW69, LP_CMD_LPDT, 0x29, 17, {0xB9, 0x00, 0xFE, 0x01, 0x38, 0x01, 0x63, 0x01, 0xA3, 0x01, 0xD3, 0x02, 0x1B, 0x02, 0x54, 0x02, 0x55}},
	{REGW70, LP_CMD_LPDT, 0x29, 17, {0xBA, 0x02, 0x8C, 0x02, 0xC9, 0x02, 0xF1, 0x03, 0x24, 0x03, 0x45, 0x03, 0x71, 0x03, 0x7F, 0x03, 0x8E}},
	{REGW71, LP_CMD_LPDT, 0x29, 15, {0xBB, 0x03, 0x9E, 0x03, 0xAE, 0x03, 0xD0, 0x03, 0xF0, 0x03, 0xFE, 0x03, 0xFF, 0x00, 0x00}},

	{REGW72, LP_CMD_LPDT, 0x23, 2, {0xFF, 0x23}},
	{REGW73, LP_CMD_LPDT, 0x23, 2, {0xFB, 0x01}},
	{REGW74, LP_CMD_LPDT, 0x39, 3, {0x76, 0x01, 0x02}},
	{REGW75, LP_CMD_LPDT, 0x39, 3, {0x77, 0x01, 0x02}},
	{REGW76, LP_CMD_LPDT, 0x39, 3, {0x78, 0x01, 0x02}},
	{REGW77, LP_CMD_LPDT, 0x39, 3, {0x7A, 0x63, 0x63}},
	{REGW78, LP_CMD_LPDT, 0x39, 3, {0x7B, 0xB3, 0xB3}},
	{REGW79, LP_CMD_LPDT, 0x39, 3, {0x7C, 0x2D, 0x32}},
	{REGW80, LP_CMD_LPDT, 0x39, 3, {0x7E, 0x10, 0x10}},

	{REGW81, LP_CMD_LPDT, 0x23, 2, {0xFF, 0x24}},
	{REGW82, LP_CMD_LPDT, 0x23, 2, {0xFB, 0x01}},
	{REGW83, LP_CMD_LPDT, 0x39, 17, {0x00, 0x27, 0x0D, 0x0C, 0x0F, 0x0E, 0x28, 0x28, 0x26, 0x26, 0x3A, 0x04, 0x05, 0x23, 0x03, 0x22, 0x29}},
	{REGW84, LP_CMD_LPDT, 0x39, 9, {0x01, 0x29, 0x08, 0x30, 0x2E, 0x2C, 0x30, 0x2E, 0x2C}},
	{REGW85, LP_CMD_LPDT, 0x39, 17, {0x02, 0x27, 0x0D, 0x0C, 0x0F, 0x0E, 0x28, 0x28, 0x26, 0x26, 0x3A, 0x04, 0x05, 0x25, 0x03, 0x24, 0x29}},
	{REGW86, LP_CMD_LPDT, 0x39, 9, {0x03, 0x29, 0x08, 0x30, 0x2E, 0x2C, 0x30, 0x2E, 0x2C}},
	{REGW87, LP_CMD_LPDT, 0x39, 13, {0x17, 0x41, 0x25, 0x63, 0xA7, 0x8B, 0xC9, 0x41, 0x25, 0x63, 0xA7, 0x8B, 0xC9}},
	{REGW88, LP_CMD_LPDT, 0x15, 2, {0x1C, 0x80}},
	{REGW89, LP_CMD_LPDT, 0x15, 2, {0x2F, 0x02}},
	{REGW90, LP_CMD_LPDT, 0x15, 2, {0x30, 0x01}},
	{REGW91, LP_CMD_LPDT, 0x15, 2, {0x31, 0x02}},
	{REGW92, LP_CMD_LPDT, 0x15, 2, {0x32, 0x03}},
	{REGW93, LP_CMD_LPDT, 0x15, 2, {0x33, 0x04}},
	{REGW94, LP_CMD_LPDT, 0x15, 2, {0x34, 0x05}},
	{REGW95, LP_CMD_LPDT, 0x15, 2, {0x35, 0x06}},
	{REGW96, LP_CMD_LPDT, 0x15, 2, {0x36, 0x07}},
	{REGW97, LP_CMD_LPDT, 0x15, 2, {0x37, 0x22}},
	{REGW98, LP_CMD_LPDT, 0x15, 2, {0x3A, 0x05}},
	{REGW99, LP_CMD_LPDT, 0x15, 2, {0x3B, 0xDB}},
	{REGW100, LP_CMD_LPDT, 0x15, 2, {0x3D, 0x02}},
	{REGW101, LP_CMD_LPDT, 0x15, 2, {0x3F, 0x0C}},
	{REGW102, LP_CMD_LPDT, 0x15, 2, {0x47, 0x34}},
	{REGW103, LP_CMD_LPDT, 0x15, 2, {0x4B, 0x47}},
	{REGW104, LP_CMD_LPDT, 0x15, 2, {0x4C, 0x01}},
	{REGW105, LP_CMD_LPDT, 0x39, 3, {0x55, 0x0A, 0x0A}},
	{REGW106, LP_CMD_LPDT, 0x15, 2, {0x56, 0x04}},
	{REGW107, LP_CMD_LPDT, 0x15, 2, {0x58, 0x10}},
	{REGW108, LP_CMD_LPDT, 0x15, 2, {0x59, 0x10}},
	{REGW109, LP_CMD_LPDT, 0x15, 2, {0x5A, 0x05}},
	{REGW110, LP_CMD_LPDT, 0x15, 2, {0x5B, 0xD6}},
	{REGW111, LP_CMD_LPDT, 0x39, 3, {0x60, 0x71, 0x70}},
	{REGW112, LP_CMD_LPDT, 0x15, 2, {0x61, 0x30}},
	{REGW113, LP_CMD_LPDT, 0x15, 2, {0x72, 0x80}},
	{REGW114, LP_CMD_LPDT, 0x39, 4, {0x92, 0xE0, 0x01, 0x31}},
	{REGW115, LP_CMD_LPDT, 0x39, 3, {0x93, 0x1A, 0x00}},
	{REGW116, LP_CMD_LPDT, 0x39, 4, {0x94, 0x60, 0x00, 0x00}},
	{REGW117, LP_CMD_LPDT, 0x15, 2, {0x95, 0x09}},
	{REGW118, LP_CMD_LPDT, 0x39, 4, {0x96, 0x80, 0xBA, 0x00}},
	{REGW119, LP_CMD_LPDT, 0x15, 2, {0x9A, 0x0B}},
	{REGW120, LP_CMD_LPDT, 0x15, 2, {0xA5, 0x00}},
	{REGW121, LP_CMD_LPDT, 0x39, 4, {0xAA, 0x98, 0x98, 0x26}},
	{REGW122, LP_CMD_LPDT, 0x15, 2, {0xAB, 0x22}},
	{REGW123, LP_CMD_LPDT, 0x29, 4, {0xC2, 0xC4, 0x00, 0x01}},
	{REGW124, LP_CMD_LPDT, 0x23, 2, {0xC4, 0x2A}},
	{REGW125, LP_CMD_LPDT, 0x23, 2, {0xC8, 0x03}},
	{REGW126, LP_CMD_LPDT, 0x23, 2, {0xC9, 0x08}},
	{REGW127, LP_CMD_LPDT, 0x23, 2, {0xCA, 0x03}},
	{REGW128, LP_CMD_LPDT, 0x23, 2, {0xD4, 0x03}},
	{REGW129, LP_CMD_LPDT, 0x23, 2, {0xD6, 0x46}},
	{REGW130, LP_CMD_LPDT, 0x23, 2, {0xD7, 0x35}},
	{REGW131, LP_CMD_LPDT, 0x23, 2, {0xD8, 0x25}},
	{REGW132, LP_CMD_LPDT, 0x23, 2, {0xDB, 0x04}},
	{REGW133, LP_CMD_LPDT, 0x23, 2, {0xDD, 0x44}},
	{REGW134, LP_CMD_LPDT, 0x23, 2, {0xDF, 0x04}},
	{REGW135, LP_CMD_LPDT, 0x23, 2, {0xE1, 0x04}},
	{REGW136, LP_CMD_LPDT, 0x23, 2, {0xE3, 0x04}},
	{REGW137, LP_CMD_LPDT, 0x23, 2, {0xE5, 0x04}},
	{REGW138, LP_CMD_LPDT, 0x23, 2, {0xE9, 0x04}},
	{REGW139, LP_CMD_LPDT, 0x23, 2, {0xEB, 0x04}},
	{REGW140, LP_CMD_LPDT, 0x23, 2, {0xEF, 0x04}},
	{REGW141, LP_CMD_LPDT, 0x23, 2, {0xF1, 0x23}},
	{REGW142, LP_CMD_LPDT, 0x23, 2, {0xF2, 0x23}},
	{REGW143, LP_CMD_LPDT, 0x23, 2, {0xF3, 0x23}},
	{REGW144, LP_CMD_LPDT, 0x23, 2, {0xF4, 0x22}},
	{REGW145, LP_CMD_LPDT, 0x23, 2, {0xF5, 0x23}},
	{REGW146, LP_CMD_LPDT, 0x23, 2, {0xF6, 0x23}},
	{REGW147, LP_CMD_LPDT, 0x23, 2, {0xF7, 0x34}},

	{REGW148, LP_CMD_LPDT, 0x23, 2, {0xFF, 0x25}},
	{REGW149, LP_CMD_LPDT, 0x23, 2, {0xFB, 0x01}},
	{REGW150, LP_CMD_LPDT, 0x15, 2, {0x05, 0x04}},
	{REGW151, LP_CMD_LPDT, 0x15, 2, {0x05, 0x00}},
	{REGW152, LP_CMD_LPDT, 0x15, 2, {0x13, 0x04}},
	{REGW153, LP_CMD_LPDT, 0x15, 2, {0x14, 0xC7}},
	{REGW154, LP_CMD_LPDT, 0x15, 2, {0x1F, 0x05}},
	{REGW155, LP_CMD_LPDT, 0x15, 2, {0x20, 0xD6}},
	{REGW156, LP_CMD_LPDT, 0x15, 2, {0x23, 0x04}},
	{REGW157, LP_CMD_LPDT, 0x15, 2, {0x24, 0x3F}},
	{REGW158, LP_CMD_LPDT, 0x15, 2, {0x26, 0x05}},
	{REGW159, LP_CMD_LPDT, 0x15, 2, {0x27, 0xD6}},
	{REGW160, LP_CMD_LPDT, 0x15, 2, {0x2A, 0x04}},
	{REGW161, LP_CMD_LPDT, 0x15, 2, {0x2B, 0x3F}},
	{REGW162, LP_CMD_LPDT, 0x15, 2, {0x33, 0x05}},
	{REGW163, LP_CMD_LPDT, 0x15, 2, {0x34, 0xD6}},
	{REGW164, LP_CMD_LPDT, 0x15, 2, {0x37, 0x04}},
	{REGW165, LP_CMD_LPDT, 0x15, 2, {0x38, 0x3F}},
	{REGW166, LP_CMD_LPDT, 0x15, 2, {0x39, 0x0A}},
	{REGW167, LP_CMD_LPDT, 0x15, 2, {0x3B, 0x00}},
	{REGW168, LP_CMD_LPDT, 0x15, 2, {0x3F, 0x20}},
	{REGW169, LP_CMD_LPDT, 0x15, 2, {0x40, 0x00}},
	{REGW170, LP_CMD_LPDT, 0x15, 2, {0x42, 0x04}},
	{REGW171, LP_CMD_LPDT, 0x15, 2, {0x44, 0x05}},
	{REGW172, LP_CMD_LPDT, 0x15, 2, {0x45, 0xDB}},
	{REGW173, LP_CMD_LPDT, 0x15, 2, {0x47, 0x47}},
	{REGW174, LP_CMD_LPDT, 0x15, 2, {0x48, 0x05}},
	{REGW175, LP_CMD_LPDT, 0x15, 2, {0x49, 0xD6}},
	{REGW176, LP_CMD_LPDT, 0x15, 2, {0x4C, 0x04}},
	{REGW177, LP_CMD_LPDT, 0x15, 2, {0x4D, 0x3F}},
	{REGW178, LP_CMD_LPDT, 0x15, 2, {0x4E, 0x0A}},
	{REGW189, LP_CMD_LPDT, 0x15, 2, {0x50, 0x05}},
	{REGW180, LP_CMD_LPDT, 0x15, 2, {0x51, 0xD6}},
	{REGW181, LP_CMD_LPDT, 0x15, 2, {0x54, 0x04}},
	{REGW182, LP_CMD_LPDT, 0x15, 2, {0x55, 0x3F}},
	{REGW183, LP_CMD_LPDT, 0x15, 2, {0x56, 0x0A}},
	{REGW184, LP_CMD_LPDT, 0x15, 2, {0x5B, 0x80}},
	{REGW185, LP_CMD_LPDT, 0x15, 2, {0x5D, 0x05}},
	{REGW186, LP_CMD_LPDT, 0x15, 2, {0x5E, 0xDB}},
	{REGW187, LP_CMD_LPDT, 0x15, 2, {0x60, 0x47}},
	{REGW188, LP_CMD_LPDT, 0x15, 2, {0x61, 0x05}},
	{REGW189, LP_CMD_LPDT, 0x15, 2, {0x62, 0xD6}},
	{REGW190, LP_CMD_LPDT, 0x15, 2, {0x65, 0x04}},
	{REGW191, LP_CMD_LPDT, 0x15, 2, {0x66, 0x3F}},
	{REGW192, LP_CMD_LPDT, 0x15, 2, {0x67, 0x0A}},
	{REGW193, LP_CMD_LPDT, 0x15, 2, {0x68, 0x04}},
	{REGW194, LP_CMD_LPDT, 0x15, 2, {0x6B, 0x44}},
	{REGW195, LP_CMD_LPDT, 0x15, 2, {0x6C, 0x0E}},
	{REGW196, LP_CMD_LPDT, 0x15, 2, {0x6D, 0x0E}},
	{REGW197, LP_CMD_LPDT, 0x15, 2, {0x6E, 0x12}},
	{REGW198, LP_CMD_LPDT, 0x15, 2, {0x6F, 0x12}},
	{REGW199, LP_CMD_LPDT, 0x15, 2, {0x8D, 0xAA}},
	{REGW200, LP_CMD_LPDT, 0x23, 2, {0xC6, 0x10}},
	{REGW201, LP_CMD_LPDT, 0x23, 2, {0xDB, 0x04}},
	{REGW202, LP_CMD_LPDT, 0x23, 2, {0xDC, 0x3F}},

	{REGW203, LP_CMD_LPDT, 0x23, 2, {0xFF, 0x26}},
	{REGW204, LP_CMD_LPDT, 0x23, 2, {0xFB, 0x01}},
	{REGW205, LP_CMD_LPDT, 0x15, 2, {0x04, 0x3A}},
	{REGW206, LP_CMD_LPDT, 0x15, 2, {0x0A, 0x03}},
	{REGW207, LP_CMD_LPDT, 0x15, 2, {0x0C, 0x10}},
	{REGW208, LP_CMD_LPDT, 0x15, 2, {0x0D, 0x08}},
	{REGW209, LP_CMD_LPDT, 0x15, 2, {0x0F, 0x03}},
	{REGW210, LP_CMD_LPDT, 0x15, 2, {0x13, 0x60}},
	{REGW211, LP_CMD_LPDT, 0x15, 2, {0x14, 0x6D}},
	{REGW212, LP_CMD_LPDT, 0x15, 2, {0x16, 0x10}},
	{REGW213, LP_CMD_LPDT, 0x39, 5, {0x19, 0x19, 0x1D, 0x1D, 0x1D}},
	{REGW214, LP_CMD_LPDT, 0x39, 5, {0x1A, 0xCA, 0xDB, 0xDB, 0xDB}},
	{REGW215, LP_CMD_LPDT, 0x39, 5, {0x1B, 0x18, 0x1C, 0x1C, 0x1C}},
	{REGW216, LP_CMD_LPDT, 0x39, 5, {0x1C, 0xEB, 0xFB, 0xFB, 0xFB}},
	{REGW217, LP_CMD_LPDT, 0x15, 2, {0x1D, 0x81}},
	{REGW218, LP_CMD_LPDT, 0x15, 2, {0x1E, 0x24}},
	{REGW219, LP_CMD_LPDT, 0x15, 2, {0x1F, 0xE0}},
	{REGW220, LP_CMD_LPDT, 0x15, 2, {0x24, 0x01}},
	{REGW221, LP_CMD_LPDT, 0x15, 2, {0x25, 0xB5}},
	{REGW222, LP_CMD_LPDT, 0x39, 5, {0x2A, 0x19, 0x1D, 0x1D, 0x1D}},
	{REGW223, LP_CMD_LPDT, 0x39, 5, {0x2B, 0xC3, 0xD3, 0xD3, 0xD3}},
	{REGW224, LP_CMD_LPDT, 0x15, 2, {0x2F, 0x04}},
	{REGW225, LP_CMD_LPDT, 0x15, 2, {0x30, 0xE0}},
	{REGW226, LP_CMD_LPDT, 0x15, 2, {0x32, 0xE0}},
	{REGW227, LP_CMD_LPDT, 0x15, 2, {0x33, 0x89}},
	{REGW228, LP_CMD_LPDT, 0x15, 2, {0x34, 0x67}},
	{REGW229, LP_CMD_LPDT, 0x15, 2, {0x35, 0x11}},
	{REGW230, LP_CMD_LPDT, 0x15, 2, {0x36, 0x11}},
	{REGW231, LP_CMD_LPDT, 0x15, 2, {0x37, 0x11}},
	{REGW232, LP_CMD_LPDT, 0x15, 2, {0x38, 0x01}},
	{REGW233, LP_CMD_LPDT, 0x15, 2, {0x39, 0x04}},
	{REGW234, LP_CMD_LPDT, 0x15, 2, {0x3A, 0xE0}},
	{REGW235, LP_CMD_LPDT, 0x39, 5, {0x3D, 0x00, 0x80, 0x28, 0x20}},
	{REGW236, LP_CMD_LPDT, 0x39, 3, {0x97, 0x00, 0x00}},
	{REGW237, LP_CMD_LPDT, 0x23, 2, {0xC9, 0x00}},
	{REGW238, LP_CMD_LPDT, 0x29, 3, {0xCD, 0x23, 0x30}},
	{REGW239, LP_CMD_LPDT, 0x29, 3, {0xCE, 0x22, 0x31}},
	{REGW240, LP_CMD_LPDT, 0x29, 4, {0xCF, 0x01, 0x00, 0x18}},
	{REGW241, LP_CMD_LPDT, 0x29, 3, {0xD0, 0x00, 0x00}},
	{REGW242, LP_CMD_LPDT, 0x29, 3, {0xD1, 0x00, 0x00}},
	{REGW243, LP_CMD_LPDT, 0x23, 2, {0xD2, 0x2D}},

	{REGW244, LP_CMD_LPDT, 0x23, 2, {0xFF, 0x27}},
	{REGW245, LP_CMD_LPDT, 0x23, 2, {0xFB, 0x01}},
	{REGW246, LP_CMD_LPDT, 0x15, 2, {0x58, 0x80}},
	{REGW247, LP_CMD_LPDT, 0x15, 2, {0x59, 0x00}},
	{REGW248, LP_CMD_LPDT, 0x15, 2, {0x5A, 0x00}},
	{REGW249, LP_CMD_LPDT, 0x15, 2, {0x5B, 0x55}},
	{REGW250, LP_CMD_LPDT, 0x15, 2, {0x5C, 0x00}},
	{REGW251, LP_CMD_LPDT, 0x15, 2, {0x5D, 0x00}},
	{REGW252, LP_CMD_LPDT, 0x15, 2, {0x5E, 0x00}},
	{REGW253, LP_CMD_LPDT, 0x15, 2, {0x5F, 0x00}},
	{REGW254, LP_CMD_LPDT, 0x39, 3, {0x60, 0x00, 0x00}},
	{REGW255, LP_CMD_LPDT, 0x39, 3, {0x61, 0x00, 0x00}},
	{REGW256, LP_CMD_LPDT, 0x39, 3, {0x62, 0x00, 0x00}},
	{REGW257, LP_CMD_LPDT, 0x39, 3, {0x63, 0x00, 0x00}},
	{REGW258, LP_CMD_LPDT, 0x39, 3, {0x64, 0x00, 0x00}},
	{REGW259, LP_CMD_LPDT, 0x39, 3, {0x65, 0x00, 0x00}},
	{REGW260, LP_CMD_LPDT, 0x39, 3, {0x66, 0x00, 0x00}},
	{REGW261, LP_CMD_LPDT, 0x39, 3, {0x67, 0x00, 0x00}},
	{REGW262, LP_CMD_LPDT, 0x39, 3, {0x68, 0x00, 0x00}},
	{REGW263, LP_CMD_LPDT, 0x15, 2, {0x69, 0x80}},
	{REGW264, LP_CMD_LPDT, 0x15, 2, {0x76, 0x80}},
	{REGW265, LP_CMD_LPDT, 0x39, 3, {0x77, 0x2C, 0x00}},
	{REGW266, LP_CMD_LPDT, 0x23, 2, {0xC0, 0x00}},
	{REGW267, LP_CMD_LPDT, 0x23, 2, {0xC1, 0x00}},
	{REGW268, LP_CMD_LPDT, 0x23, 2, {0xD1, 0x04}},
	{REGW269, LP_CMD_LPDT, 0x23, 2, {0xD2, 0x3C}},

	{REGW270, LP_CMD_LPDT, 0x23, 2, {0xFF, 0x2A}},
	{REGW271, LP_CMD_LPDT, 0x23, 2, {0xFB, 0x01}},
	{REGW272, LP_CMD_LPDT, 0x15, 2, {0x25, 0x15}},
	{REGW273, LP_CMD_LPDT, 0x15, 2, {0x27, 0x5F}},
	{REGW274, LP_CMD_LPDT, 0x15, 2, {0x28, 0x78}},
	{REGW275, LP_CMD_LPDT, 0x15, 2, {0x30, 0x02}},
	{REGW276, LP_CMD_LPDT, 0x15, 2, {0x32, 0x50}},
	{REGW277, LP_CMD_LPDT, 0x15, 2, {0x33, 0x70}},
	{REGW278, LP_CMD_LPDT, 0x15, 2, {0x35, 0x01}},
	{REGW279, LP_CMD_LPDT, 0x39, 14, {0x4B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
	{REGW280, LP_CMD_LPDT, 0x15, 2, {0x64, 0x96}},
	{REGW281, LP_CMD_LPDT, 0x15, 2, {0x67, 0x9E}},
	{REGW282, LP_CMD_LPDT, 0x15, 2, {0x68, 0x00}},
	{REGW283, LP_CMD_LPDT, 0x15, 2, {0x69, 0x00}},
	{REGW284, LP_CMD_LPDT, 0x15, 2, {0x6A, 0x9E}},
	{REGW285, LP_CMD_LPDT, 0x15, 2, {0x6B, 0x00}},
	{REGW286, LP_CMD_LPDT, 0x15, 2, {0x6C, 0x00}},
	{REGW287, LP_CMD_LPDT, 0x15, 2, {0x79, 0x96}},
	{REGW288, LP_CMD_LPDT, 0x15, 2, {0x7C, 0x96}},
	{REGW289, LP_CMD_LPDT, 0x15, 2, {0x7F, 0x96}},
	{REGW290, LP_CMD_LPDT, 0x15, 2, {0x82, 0x96}},
	{REGW291, LP_CMD_LPDT, 0x15, 2, {0x85, 0x96}},
	{REGW292, LP_CMD_LPDT, 0x15, 2, {0x88, 0x9E}},
	{REGW293, LP_CMD_LPDT, 0x15, 2, {0x89, 0x00}},
	{REGW294, LP_CMD_LPDT, 0x15, 2, {0x8A, 0x00}},
	{REGW295, LP_CMD_LPDT, 0x15, 2, {0x8B, 0x16}},
	{REGW296, LP_CMD_LPDT, 0x15, 2, {0x8C, 0x00}},
	{REGW297, LP_CMD_LPDT, 0x15, 2, {0x8D, 0x00}},
	{REGW298, LP_CMD_LPDT, 0x15, 2, {0x8E, 0x16}},
	{REGW299, LP_CMD_LPDT, 0x15, 2, {0x8F, 0x00}},
	{REGW300, LP_CMD_LPDT, 0x15, 2, {0x90, 0x00}},
	{REGW301, LP_CMD_LPDT, 0x15, 2, {0x92, 0x00}},
	{REGW302, LP_CMD_LPDT, 0x15, 2, {0x93, 0x00}},
	{REGW303, LP_CMD_LPDT, 0x15, 2, {0x94, 0x06}},
	{REGW304, LP_CMD_LPDT, 0x15, 2, {0x99, 0x91}},
	{REGW305, LP_CMD_LPDT, 0x15, 2, {0x9A, 0x0A}},
	{REGW306, LP_CMD_LPDT, 0x15, 2, {0xA2, 0x15}},
	{REGW307, LP_CMD_LPDT, 0x15, 2, {0xA3, 0x50}},
	{REGW308, LP_CMD_LPDT, 0x15, 2, {0xA4, 0x15}},
	{REGW309, LP_CMD_LPDT, 0x15, 2, {0xA5, 0x41}},
	{REGW310, LP_CMD_LPDT, 0x15, 2, {0xA6, 0x10}},
	{REGW311, LP_CMD_LPDT, 0x15, 2, {0xA9, 0x21}},
	{REGW312, LP_CMD_LPDT, 0x29, 4, {0xB3, 0x16, 0x00, 0x00}},
	{REGW313, LP_CMD_LPDT, 0x23, 2, {0xB7, 0xFF}},
	{REGW314, LP_CMD_LPDT, 0x23, 2, {0xB8, 0x40}},
	{REGW315, LP_CMD_LPDT, 0x29, 10, {0xB9, 0x98, 0x00, 0x6F, 0x00, 0xCB, 0xB7, 0x14, 0x5F, 0x41}},
	{REGW316, LP_CMD_LPDT, 0x29, 9, {0xBA, 0xAA, 0xAA, 0xA5, 0xA5, 0xE1, 0xE1, 0xFF, 0xFF}},
	{REGW317, LP_CMD_LPDT, 0x29, 13, {0xBB, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0x55, 0xAA, 0x55, 0x38, 0xC7, 0x38, 0xC7, 0xFF, 0xFF, 0xFF, 0xFF}},
	{REGW318, LP_CMD_LPDT, 0x29, 3, {0xBC, 0x66, 0x06}},
	{REGW319, LP_CMD_LPDT, 0x23, 2, {0xC4, 0x12}},
	{REGW320, LP_CMD_LPDT, 0x23, 2, {0xC5, 0x04}},
	{REGW321, LP_CMD_LPDT, 0x23, 2, {0xC6, 0x3F}},
	{REGW322, LP_CMD_LPDT, 0x23, 2, {0xC8, 0x0A}},
	{REGW323, LP_CMD_LPDT, 0x23, 2, {0xCA, 0x01}},
	{REGW324, LP_CMD_LPDT, 0x29, 5, {0xCB, 0x10, 0x00, 0x00, 0x00}},
	{REGW325, LP_CMD_LPDT, 0x29, 7, {0xCC, 0xF5, 0xAA, 0x5F, 0x55, 0x00, 0x00}},
	{REGW326, LP_CMD_LPDT, 0x23, 2, {0xD0, 0x04}},
	{REGW327, LP_CMD_LPDT, 0x23, 2, {0xD1, 0x01}},
	{REGW328, LP_CMD_LPDT, 0x23, 2, {0xF4, 0x91}},
	{REGW329, LP_CMD_LPDT, 0x23, 2, {0xF5, 0x0A}},

	/*Spruce code for OSPURCET-1219 by zhangkx10 at 2023/2/20 start*/
	{REGW330, LP_CMD_LPDT, 0x23, 2, {0xFF, 0x27}},
	{REGW331, LP_CMD_LPDT, 0x23, 2, {0xFB, 0x01}},
	{REGW332, LP_CMD_LPDT, 0x23, 2, {0xD0, 0x31}},
	{REGW333, LP_CMD_LPDT, 0x23, 2, {0xD1, 0x84}},
	{REGW334, LP_CMD_LPDT, 0x23, 2, {0xD2, 0x38}},

	{REGW335, LP_CMD_LPDT, 0x23, 2, {0xFF, 0x10}},
	{REGW336, LP_CMD_LPDT, 0x23, 2, {0xFB, 0x01}},
	{REGW337, LP_CMD_LPDT, 0x39, 7, {0x3B, 0x03, 0x60, 0x1A, 0x0A, 0x0A, 0x00}},
	{REGW338, LP_CMD_LPDT, 0x15, 2, {0x90, 0x10}},
	{REGW339, LP_CMD_LPDT, 0x39, 17, {0x91, 0x89, 0x28, 0x00, 0x14, 0xD2, 0x00, 0x00, 0x00, 0x02, 0xA1, 0x00, 0x14, 0x05, 0x7A, 0x01, 0xDA}},
	{REGW340, LP_CMD_LPDT, 0x39, 3, {0x92, 0x10, 0xE0}},
	{REGW341, LP_CMD_LPDT, 0x23, 2, {0xB2, 0x91}},

	{REGW342, LP_CMD_LPDT, 0x05, 1, {0x11}},
	{DELAY, 120, 0, 0, {0}},
	{REGW343, LP_CMD_LPDT, 0x05, 1, {0x29}},
	{DELAY, 20, 0, 0, {0}},
	/*Spruce code for OSPURCET-1219 by zhangkx10 at 2023/2/20 end*/
};


static void push_table(struct drm_panel *panel, const struct dcs_setting_entry *init_table, int _size,
		enum dcs_cmd_name start, int count)
{
	unsigned int i;
	struct lcm *ctx = panel_to_lcm(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	for(i = start; i < start + count; i++) {
		if (init_table[i].cmd_name == DELAY) {
			msleep(init_table[i].cmd);
			continue;
		} else {
			if (init_table[i].data_id == 0x05
				|| init_table[i].data_id == 0x15
				|| init_table[i].data_id == 0x39) {
				mipi_dsi_dcs_write_buffer(dsi,
					init_table[i].para_list, init_table[i].count);
			} else {
				mipi_dsi_generic_write(dsi,
					init_table[i].para_list, init_table[i].count);
			}
		}
	}
}

static void lcm_panel_init(struct lcm *ctx)
{
	pr_info("%s+++\n",__func__);
/*	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_info(ctx->dev, "%s: cannot get reset_gpio %ld\n",
				__func__, PTR_ERR(ctx->reset_gpio));
		return;
	} */

	gpiod_set_value(ctx->it6112_1v0_en, 1);
	// gpiod_set_value(ctx->it6112_1v8_en, 1);
	udelay(200);
	lcm_v1_8_enable(ctx);
	gpiod_set_value(ctx->reset_gpio, 1);
	udelay(6 * 1000);
	gpiod_set_value(ctx->reset_gpio, 0);
	udelay(6 * 1000);
	gpiod_set_value(ctx->reset_gpio, 1);
	udelay(6 * 1000);
	gpiod_set_value(ctx->reset_gpio, 0);
	udelay(6 * 1000);
	gpiod_set_value(ctx->reset_gpio, 1);
	udelay(12 * 1000);
	// devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	chip_init(ctx->it6112_client);

	pr_info("%s---\n",__func__);
}

static int lcm_disable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	pr_info("%s+++\n",__func__);

	if (!ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = false;

	pr_info("%s---\n",__func__);

	return 0;
}

static int lcm_unprepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	pr_info("%s +++\n", __func__);

	if (!ctx->prepared)
		return 0;

	/*Spruce code for OSPURCET-1280 by zhangkx10 at 2023/2/16 start*/
	ite_poll_enable(0);
	/*Spruce code for OSPURCET-1280 by zhangkx10 at 2023/2/16 end*/

	device_power_off(ctx->it6112_client);

	ctx->error = 0;
	ctx->prepared = false;

/*	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_info(ctx->dev, "%s: cannot get reset_gpio %ld\n",
				__func__, PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	} */
	//devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	// gpiod_set_value(ctx->it6112_1v8_en, 0);
	lcm_v1_8_disable(ctx);
	udelay(4 * 1000);
	gpiod_set_value(ctx->it6112_1v0_en, 0);
	/*Spruce code for OSPURCET-2880 by zhangkx10 at 2023/3/3 start*/
	if((!nvt_gesture_flag) && (!nvt_esd_flag))
	{
		pr_info("[esd] nvt_esd_flag = %d, nvt_gesture_flag = %d \n", nvt_esd_flag, nvt_gesture_flag);
	/*Spruce code for OSPURCET-2880 by zhangkx10 at 2023/3/3 end*/
		/*Spruce code for OSPURCET-612 by zhangkx10 at 2023/1/19 start*/
		gpiod_set_value(ctx->reset_gpio, 0);

		udelay(2000);

		ctx->bias_neg = devm_gpiod_get_index(ctx->dev,
				"bias", 1, GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->bias_neg)) {
			dev_info(ctx->dev, "%s: cannot get bias_neg %ld\n",
					__func__, PTR_ERR(ctx->bias_neg));
			return PTR_ERR(ctx->bias_neg);
		}
		gpiod_set_value(ctx->bias_neg, 0);
		devm_gpiod_put(ctx->dev, ctx->bias_neg);

		udelay(2000);

		ctx->bias_pos = devm_gpiod_get_index(ctx->dev,
				"bias", 0, GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->bias_pos)) {
			dev_info(ctx->dev, "%s: cannot get bias_pos %ld\n",
					__func__, PTR_ERR(ctx->bias_pos));
			return PTR_ERR(ctx->bias_pos);
		}
		gpiod_set_value(ctx->bias_pos, 0);
		devm_gpiod_put(ctx->dev, ctx->bias_pos);

		gpiod_set_value(ctx->pm_enable_gpio, 0);

		lcm_vdd_disable(ctx);
          	udelay(2 * 1000);
	}
	if(!nvt_gesture_flag)
		lcm_vdd_disable(ctx);
	/*Spruce code for OSPURCET-612 by zhangkx10 at 2023/1/19 end*/

	pr_info("%s ---\n", __func__);
	return 0;
}

static int lcm_prepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int ret;

	pr_info("%s\n", __func__);
	if (ctx->prepared)
		return 0;

	/*Spruce code for OSPURCET-612 by zhangkx10 at 2023/1/19 start*/
	lcm_vdd_enable(ctx);
	/*Spruce code for OSPURCET-612 by zhangkx10 at 2023/1/19 end*/

	/*Spruce code for OSPURCET-2880 by zhangkx10 at 2023/3/3 start*/
	if(nvt_esd_flag == 0) {
		pr_info("[esd] nvt_esd_flag = %d\n", nvt_esd_flag);
		gpiod_set_value(ctx->pm_enable_gpio, 1);
		udelay(2 * 1000);
		lcd_set_bias(1);
	}
	/*Spruce code for OSPURCET-2880 by zhangkx10 at 2023/3/3 end*/

	ctx->bias_pos = devm_gpiod_get_index(ctx->dev,
			"bias", 0, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_pos)) {
		dev_info(ctx->dev, "%s: cannot get bias_pos %ld\n",
				__func__, PTR_ERR(ctx->bias_pos));
		return PTR_ERR(ctx->bias_pos);
	}
	gpiod_set_value(ctx->bias_pos, 1);
	devm_gpiod_put(ctx->dev, ctx->bias_pos);

	udelay(2000);

	ctx->bias_neg = devm_gpiod_get_index(ctx->dev,
			"bias", 1, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_neg)) {
		dev_info(ctx->dev, "%s: cannot get bias_neg %ld\n",
				__func__, PTR_ERR(ctx->bias_neg));
		return PTR_ERR(ctx->bias_neg);
	}
	gpiod_set_value(ctx->bias_neg, 1);
	devm_gpiod_put(ctx->dev, ctx->bias_neg);

	lcm_panel_init(ctx);

	/*Spruce code for OSPURCET-1280 by zhangkx10 at 2023/2/16 start*/
	ite_poll_enable(1);
	/*Spruce code for OSPURCET-1280 by zhangkx10 at 2023/2/16 end*/

	ret = ctx->error;
	if (ret < 0) {
		pr_info("error! return\n");
		lcm_unprepare(panel);
	}

	ctx->prepared = true;

#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_tch_rst(panel);
#endif
#ifdef PANEL_SUPPORT_READBACK
	lcm_panel_get_data(ctx);
#endif

	/*Spruce code for OSPURCET-2314 by gaoxue4 at 2023/3/3 start*/
	if (nvt_esd_flag == 1) {
		mutex_lock(&ts->lock);
		nvt_update_firmware("novatek_ts_boe_fw.bin");
		mutex_unlock(&ts->lock);
	}
	/*Spruce code for OSPURCET-2314 by gaoxue4 at 2023/3/3 end*/

	pr_info("%s---\n", __func__);

	return ret;
}

static int lcm_enable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	pr_info("%s+++\n", __func__);

	if (ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = true;

	pr_info("%s---\n", __func__);

	return 0;
}
/*Spruce code for OSPURCET-3111 by zhuhao6 at 2023/3/6 start*/
#define HFP (106)
#define HSA (10)
#define HBP (105)
#define VFP (26)
#define VSA (2)
#define VBP (94)
#define VAC (1840)
#define HAC (2944)

static const struct drm_display_mode default_mode = {
	.clock = 367993,
	.hdisplay = HAC,
	.hsync_start = HAC + HFP,
	.hsync_end = HAC + HFP + HSA,
	.htotal = HAC + HFP + HSA + HBP,
	.vdisplay = VAC,
	.vsync_start = VAC + VFP,
	.vsync_end = VAC + VFP + VSA,
	.vtotal = VAC + VFP + VSA + VBP,
	.vrefresh = 60,
};

#if defined(CONFIG_MTK_PANEL_EXT)
static int panel_ext_reset(struct drm_panel *panel, int on)
{
	struct lcm *ctx = panel_to_lcm(panel);
/*
	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_info(ctx->dev, "%s: cannot get reset_gpio %ld\n",
				__func__, PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	} */
	gpiod_set_value(ctx->reset_gpio, on);
	// devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	return 0;
}

static int panel_ata_check(struct drm_panel *panel)
{
	return 0;
}

static struct mtk_panel_params ext_params = {
	.pll_clk = 1176,
/*Spruce code for OSPURCET-3111 by zhuhao6 at 2023/3/6 end*/
	.ssc_disable = 1,
/*Spruce code for OSPURCET-1219 by zhuhao at 2023/2/9 start*/
	.cust_esd_check = 1,
	.esd_check_enable = 1,
/*Spruce code for OSPURCET-1219 by zhuhao at 2023/2/9 end*/
	.lcm_esd_check_table[0] = {
		.cmd = 0x0a,
		.count = 1,
		.para_list[0] = 0x9c,
	},
/*Spruce code for OSPURCET-1272 by zhuhao6 at 2023/2/21 start*/
	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
/*Spruce code for OSPURCET-1272 by zhuhao6 at 2023/2/21 end*/
};

static int lcm_setbacklight_by_bridge(struct drm_panel *panel, dcs_write_gce cb,
	void *handle, unsigned int level)
{
	/*Spruce code for OSPURCET-1551 by zhuhao6 at 2023/3/15 start*/
	pr_err("ktz boe set brightness: %d\n", level);
	lcd_bias_set_led_brightness(level);
	lcd_bl_set_led_brightness(level);
	/*Spruce code for OSPURCET-1551 by zhuhao6 at 2023/3/15 end*/
	//it6112_set_backlight(ctx->it6112_client, map_level);

	return 0;
}

static struct mtk_panel_funcs ext_funcs = {
	.reset = panel_ext_reset,
	.ata_check = panel_ata_check,
	.set_backlight_bridge = lcm_setbacklight_by_bridge,
};
#endif

struct panel_desc {
	const struct drm_display_mode *modes;
	unsigned int num_modes;

	unsigned int bpc;

	struct {
		unsigned int width;
		unsigned int height;
	} size;

	struct {
		unsigned int prepare;
		unsigned int enable;
		unsigned int disable;
		unsigned int unprepare;
	} delay;
};

static int lcm_get_modes(struct drm_panel *panel)
{
	struct drm_display_mode *mode;

	pr_info("%s+++\n", __func__);

	mode = drm_mode_duplicate(panel->drm, &default_mode);
	if (!mode) {
		dev_info(panel->drm->dev, "failed to add mode %ux%ux@%u\n",
				default_mode.hdisplay, default_mode.vdisplay,
				default_mode.vrefresh);
		return -ENOMEM;
	}

	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(panel->connector, mode);

	panel->connector->display_info.width_mm = 64;
	panel->connector->display_info.height_mm = 129;

	pr_info("%s---\n", __func__);

	return 1;
}

static const struct drm_panel_funcs lcm_drm_funcs = {
	.disable = lcm_disable,
	.unprepare = lcm_unprepare,
	.prepare = lcm_prepare,
	.enable = lcm_enable,
	.get_modes = lcm_get_modes,
};

static int lcm_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct lcm *ctx;
//	struct device_node *backlight;
	int ret;
	struct device_node *dsi_node, *remote_node = NULL, *endpoint = NULL;
	char *name = "panel-nt36532-dsi-vdo-boe";

	pr_info("%s start!\n", __func__);

	dsi_node = of_get_parent(dev->of_node);
	if (dsi_node) {
		endpoint = of_graph_get_next_endpoint(dsi_node, NULL);
		if (endpoint) {
			remote_node = of_graph_get_remote_port_parent(endpoint);
			if (!remote_node) {
				pr_info("No panel connected,skip probe lcm\n");
				return -ENODEV;
			}
			pr_info("device node name:%s\n", remote_node->name);
		}
	}
	if (remote_node != dev->of_node) {
		pr_info("%s+ skip probe due to not current lcm\n", __func__);
		return -ENODEV;
	}

	ctx = devm_kzalloc(dev, sizeof(struct lcm), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE;

	if(init_config(&it6112_dev)) {
		pr_info("ite6113 driver not ready, wait....\n");
		return -EPROBE_DEFER;
	}

	/*Spruce code for OSPURCET-1428 by zhangkx10 at 2023/3/8 start*/
	if(!chip_identify_power_on(&it6112_dev)) {
		pr_info("error, ite6113 is not find!\n");
		return -EPROBE_DEFER;
	} else {
		ite_poll_init(&it6112_dev);
	}
	/*Spruce code for OSPURCET-1428 by zhangkx10 at 2023/3/8 end*/

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_info(dev, "%s: cannot get reset-gpios %ld\n",
				__func__, PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	gpiod_set_value(ctx->reset_gpio, 1);
	// devm_gpiod_put(dev, ctx->reset_gpio);

/*Spruce code for OSPURCET-1219 by zhuhao at 2023/2/9 start*/
	/*Spruce code for OSPURCET-1219 by zhangkx10 at 2023/2/21 start*/
	ext_params.te_gpio = devm_gpiod_get(dev, "te", GPIOD_IN);
	/*Spruce code for OSPURCET-1219 by zhangkx10 at 2023/2/21 end*/
	if (IS_ERR(ext_params.te_gpio)) {
		dev_info(dev, "%s: cannot get reset-gpios %ld\n",
				__func__, PTR_ERR(ext_params.te_gpio));
		return PTR_ERR(ext_params.te_gpio);
	}
/*Spruce code for OSPURCET-1219 by zhuhao at 2023/2/9 end*/

	ctx->pm_enable_gpio = devm_gpiod_get(dev, "pm-enable", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->pm_enable_gpio)) {
		dev_info(dev, "%s: cannot get reset-gpios %ld\n",
				__func__, PTR_ERR(ctx->pm_enable_gpio));
		return PTR_ERR(ctx->pm_enable_gpio);
	}
	//gpiod_set_value(ctx->pm_enable_gpio, 1);

	ctx->bias_pos = devm_gpiod_get_index(dev, "bias", 0, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_pos)) {
		dev_info(dev, "%s: cannot get bias-pos 0 %ld\n",
				__func__, PTR_ERR(ctx->bias_pos));
		return PTR_ERR(ctx->bias_pos);
	}
	devm_gpiod_put(dev, ctx->bias_pos);

	ctx->bias_neg = devm_gpiod_get_index(dev, "bias", 1, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_neg)) {
		dev_info(dev, "%s: cannot get bias-neg 1 %ld\n",
				__func__, PTR_ERR(ctx->bias_neg));
		return PTR_ERR(ctx->bias_neg);
	}
	devm_gpiod_put(dev, ctx->bias_neg);
	ctx->it6112_1v0_en= devm_gpiod_get(dev, "it6112_1v0_en", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->it6112_1v0_en)) {
		dev_info(dev, "%s: cannot get it6112_1v0_en %ld\n",
				__func__, PTR_ERR(ctx->it6112_1v0_en));
		return PTR_ERR(ctx->it6112_1v0_en);
	}

	ctx->it6112_1v8_en= devm_gpiod_get(dev, "it6112_1v8_en", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->it6112_1v8_en)) {
		dev_info(dev, "%s: cannot get it6112_1v8_en %ld\n",
				__func__, PTR_ERR(ctx->it6112_1v8_en));
		return PTR_ERR(ctx->it6112_1v8_en);
	}

	ctx->v1_8 = devm_regulator_get(dev, "reg-v1_8");
	if (IS_ERR(ctx->v1_8)) {
		dev_info(dev, "%s: cannot get v1_8 %ld\n", PTR_ERR(ctx->v1_8));
		return PTR_ERR(ctx->v1_8);
	}

	/*Spruce code for OSPURCET-612 by zhangkx10 at 2023/1/19 start*/
	ctx->vdd = devm_regulator_get(dev, "reg-vdd");
	if (IS_ERR(ctx->vdd)) {
		dev_info(dev, "%s: cannot get vdd %ld\n", PTR_ERR(ctx->vdd));
		return PTR_ERR(ctx->vdd);
	}
	ret = lcm_vdd_enable(ctx);
	if (ret < 0) {
		dev_info(dev, "lcm power VDD enable fail\n");
		return ret;
	}
	/*Spruce code for OSPURCET-612 by zhangkx10 at 2023/1/19 end*/

//	ctx->v1_0 = devm_regulator_get(dev, "reg-v1_0");
//	if (IS_ERR(ctx->v1_0)) {
//		dev_info(dev, "%s: cannot get v1_0 %ld\n", PTR_ERR(ctx->v1_0));
//		return PTR_ERR(ctx->v1_0);
//	}

	// 6360 ldo need open in kernel
	ret = lcm_v1_8_enable(ctx);

	if (ret < 0) {
		dev_info(dev, "lcm power enable fail\n");
		return ret;
	}

	ctx->prepared = true;
	ctx->enabled = true;

	drm_panel_init(&ctx->panel);
	ctx->panel.dev = dev;
	ctx->panel.funcs = &lcm_drm_funcs;

	ret = drm_panel_add(&ctx->panel);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&ctx->panel);

	table_info.init_cmd_table = &lcm_init_table[0];
	table_info.count = ARRAY_SIZE(lcm_init_table);
	it6112_dev.init_table = &table_info;
	it6112_dev.push_table = push_table;
	it6112_dev.panel = &ctx->panel;
	ctx->it6112_client = &it6112_dev;
#if defined(CONFIG_MTK_PANEL_EXT)
	ret = mtk_panel_ext_create(dev, &ext_params, &ext_funcs, &ctx->panel);
	if (ret < 0)
		return ret;
#endif

	hq_regiser_hw_info(HWID_LCM, name);

	pr_info("%s end\n", __func__);

	return ret;
}

static int lcm_remove(struct mipi_dsi_device *dsi)
{
	struct lcm *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	return 0;
}

static const struct of_device_id lcm_of_match[] = {
	{ .compatible = "boe,nt36532,vdo", },
	{ }
};

MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.driver = {
		.name = "panel-nt36532-dsi-vdo-boe",
		.owner = THIS_MODULE,
		.of_match_table = lcm_of_match,
	},
};

module_mipi_dsi_driver(lcm_driver);

MODULE_AUTHOR("Ning Feng <Ning.Feng@mediatek.com>");
MODULE_DESCRIPTION("truly nt36532 VDO LCD Panel Driver");
MODULE_LICENSE("GPL v2");

