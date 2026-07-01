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
/*Spruce code for OSPURCET-1219 by zhuhao at 2023/2/9 start*/
	/*Spruce code for OSPURCET-1219 by zhangkx10 at 2023/2/20 start*/
	{REGW0, LP_CMD_LPDT, 0x23, 2, {0xFF, 0x27}},
	{REGW1, LP_CMD_LPDT, 0x23, 2, {0xFB, 0x01}},
	{REGW2, LP_CMD_LPDT, 0x23, 2, {0xD0, 0x31}},
	{REGW3, LP_CMD_LPDT, 0x23, 2, {0xD1, 0x84}},
	{REGW4, LP_CMD_LPDT, 0x23, 2, {0xD2, 0x38}},
	/*Spruce code for OSPURCET-1219 by zhangkx10 at 2023/2/20 end*/

	{REGW5, LP_CMD_LPDT, 0x23, 2, {0xFF, 0x10}},
	{REGW6, LP_CMD_LPDT, 0x23, 2, {0xFB, 0x01}},
/*Spruce code for OSPURCET-1434 by zhuhao6 at 2023/2/20 start*/
	{REGW7, LP_CMD_LPDT, 0x39, 7, {0x3B, 0x03, 0x60, 0x1A, 0x0A, 0x0A, 0x00}},
/*Spruce code for OSPURCET-1434 by zhuhao6 at 2023/2/20 end*/

	{REGW8, LP_CMD_LPDT, 0x05, 1, {0x11}},
	{DELAY, 120, 0, 0, {0}},
	{REGW9, LP_CMD_LPDT, 0x05, 1, {0x29}},
	{DELAY, 20, 0, 0, {0}},
/*Spruce code for OSPURCET-1219 by zhuhao at 2023/2/9 end*/

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
		nvt_update_firmware("novatek_ts_tianma_fw.bin");
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
	pr_err("ktz tianma set brightness: %d\n",level);
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
	char *name = "panel-nt36532-dsi-vdo-tianma";

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
	{ .compatible = "tianma,nt36532,vdo", },
	{ }
};

MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.driver = {
		.name = "panel-nt36532-dsi-vdo-tianma",
		.owner = THIS_MODULE,
		.of_match_table = lcm_of_match,
	},
};

module_mipi_dsi_driver(lcm_driver);

MODULE_AUTHOR("Ning Feng <Ning.Feng@mediatek.com>");
MODULE_DESCRIPTION("truly nt36532 VDO LCD Panel Driver");
MODULE_LICENSE("GPL v2");

