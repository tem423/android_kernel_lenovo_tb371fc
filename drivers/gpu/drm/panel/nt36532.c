// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024
 * Driver for nt36532 tianma 3k video mode dsi panel
 *
 * Based on DTS node: qcom,mdss_dsi_panel_spinel_tianma_nt36532_dsc_3k_video
 *
 * Physical resolution: 2944x1840 (dual DSI, each 1472x1840)
 * DSC: Enabled, slice width 736, slice height 20
 * Backlight: KTZ8866 (I2C)
 * Brightness range: 0-2047 (11-bit)
 *
 * Supported refresh rates: 60Hz, 90Hz, 120Hz, 144Hz (DFPS)
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/string.h>

#include <drm/drm_panel.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>

#include <linux/platform_data/ktz8866.h>

/* ===== 手动定义 DSC 结构 (4.19内核不存在 drm_dsc.h) ===== */
struct drm_dsc_config {
    u8 version;
    u8 slice_height;
    u16 slice_width;
    u8 bits_per_component;
    u8 bits_per_pixel;
    bool block_pred_enable;
};

/* ===== Display Timing (from DTS) ===== */
#define HFP 201
#define HSA 12
#define HBP 60
#define VFP 26
#define VSA 2
#define VBP 182
#define VACT 1840
#define HACT 1472

/* ===== Physical Size (mm) ===== */
#define PHYSICAL_WIDTH_MM   274
#define PHYSICAL_HEIGHT_MM  171

/* ============================================================
 * Display Modes (120Hz and 144Hz)
 * ============================================================ */

/* 120Hz 模式 (主要/默认模式) */
static const struct drm_display_mode default_mode_120hz = {
    .clock = 367993,
    .hdisplay = 2944,
    .hsync_start = 2944 + HFP,
    .hsync_end = 2944 + HFP + HSA,
    .htotal = 2944 + HFP + HSA + HBP,
    .vdisplay = VACT,
    .vsync_start = VACT + VFP,
    .vsync_end = VACT + VFP + VSA,
    .vtotal = VACT + VFP + VSA + VBP,
    .vrefresh = 120,
    .type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

/* 144Hz 模式 (高性能模式) */
static const struct drm_display_mode default_mode_144hz = {
    .clock = 441592,  /* 367993 * 144/120 */
    .hdisplay = 2944,
    .hsync_start = 2944 + HFP,
    .hsync_end = 2944 + HFP + HSA,
    .htotal = 2944 + HFP + HSA + HBP,
    .vdisplay = VACT,
    .vsync_start = VACT + VFP,
    .vsync_end = VACT + VFP + VSA,
    .vtotal = VACT + VFP + VSA + VBP,
    .vrefresh = 144,
    .type = DRM_MODE_TYPE_DRIVER,
};

/* ============================================================
 * Panel Data Structure
 * ============================================================ */

struct panel_data {
    struct drm_panel panel;
    struct mipi_dsi_device *dsi;
    struct device *dev;

    struct gpio_desc *reset_gpio;
    struct gpio_desc *te_gpio;

    struct regulator *vdd;
    struct regulator *v1_8;

    struct backlight_device *backlight;

    /* DFPS 支持 (动态刷新率切换) */
    bool dfps_supported;
    u32 dfps_rates[4];  /* 60, 90, 120, 144 */
    u32 dfps_count;

    bool prepared;
    bool enabled;
};

static inline struct panel_data *to_panel_data(struct drm_panel *panel)
{
    return container_of(panel, struct panel_data, panel);
}

/* ===== DSC Configuration ===== */
static const struct drm_dsc_config dsc_cfg = {
    .version = 0x11,
    .slice_height = 20,
    .slice_width = 736,
    .bits_per_component = 8,
    .bits_per_pixel = 8,
    .block_pred_enable = true,
};

/* ============================================================
 * Panel Commands
 * ============================================================ */

static const u8 panel_on_cmds[] = {
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x20,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xfb, 0x01,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x32, 0x72,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x23,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xfb, 0x01,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x75, 0x03,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x76, 0x07,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x7a, 0xcd,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xbc, 0x04,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x26,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xfb, 0x01,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x04, 0x19, 0x1c, 0x1d, 0x16,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x04, 0x1a, 0xdb, 0xb1, 0xa4,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x04, 0x2a, 0x1c, 0x1d, 0x16,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x04, 0x2b, 0xd3, 0xa9, 0x9c,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x04, 0x99, 0x15, 0x1e, 0x04,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x04, 0x9a, 0x30, 0x94, 0x3b,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x04, 0x9d, 0x15, 0x1e, 0x04,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x04, 0x9e, 0x2a, 0x8c, 0x00,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x10,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xfb, 0x01,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x07, 0x3b, 0x03, 0xb8, 0x1a, 0x0a, 0x0a, 0x00,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x27,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xfb, 0x01,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x13, 0x06,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xd0, 0x31,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xd1, 0x84,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xd2, 0x38,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x25,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xfb, 0x01,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x0f, 0x20,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x23,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xfb, 0x01,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x04, 0xbc, 0x04, 0x00, 0x3c,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x10,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xfb, 0x01,
    0x05, 0x01, 0x00, 0x00, 0x78, 0x00, 0x02, 0x11, 0x00,
    0x05, 0x01, 0x00, 0x00, 0x64, 0x00, 0x02, 0x29, 0x00,
};

static const u8 panel_off_cmds[] = {
    0x05, 0x01, 0x00, 0x00, 0x0f, 0x00, 0x02, 0x28, 0x00,
    0x05, 0x01, 0x00, 0x00, 0x3c, 0x00, 0x02, 0x10, 0x00,
};

/* ===== 144Hz 时序切换命令 ===== */
static const u8 timing_switch_144hz_cmds[] = {
    /* 进入 Page 0x10 */
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x10,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xfb, 0x01,

    /* 调整 VFP 适配 144Hz (天马面板安全值) */
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x30, 0x00,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x31, 0x00,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x32, 0x00,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x33, 0x00,

    /* 回到 Page 0x00 */
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x00,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xfb, 0x01,
};

/* ============================================================
 * 命令发送函数
 * ============================================================ */

static int panel_send_cmds(struct panel_data *ctx, const u8 *cmds, size_t len)
{
    struct mipi_dsi_device *dsi = ctx->dsi;
    size_t i = 0;
    int ret;

    while (i < len) {
        u8 type = cmds[i];
        u8 delay = cmds[i + 2];
        u8 payload_len = cmds[i + 3];
        const u8 *payload = &cmds[i + 4];

        if (type == 0x05) {
            ret = mipi_dsi_dcs_write(dsi, payload[0], payload + 1, payload_len - 1);
        } else if (type == 0x15) {
            ret = mipi_dsi_generic_write(dsi, payload, payload_len);
        } else if (type == 0x39) {
            ret = mipi_dsi_dcs_write(dsi, payload[0], payload + 1, payload_len - 1);
        } else {
            dev_err(ctx->dev, "unknown command type 0x%02x\n", type);
            return -EINVAL;
        }

        if (ret < 0) {
            dev_err(ctx->dev, "cmd send failed: %d\n", ret);
            return ret;
        }

        i += 4 + payload_len;
        if (delay)
            msleep(delay);
    }

    return 0;
}

/* ============================================================
 * 背光控制
 * ============================================================ */

static int panel_update_backlight(struct backlight_device *bl)
{
    struct panel_data *ctx = bl_get_data(bl);
    int brightness = bl->props.brightness;

    ktz8866_set_backlight_level(brightness);
    return 0;
}

static const struct backlight_ops panel_bl_ops = {
    .update_status = panel_update_backlight,
};

/* ============================================================
 * DRM Panel 操作函数
 * ============================================================ */

static int panel_prepare(struct drm_panel *panel)
{
    struct panel_data *ctx = to_panel_data(panel);
    int ret;

    dev_info(ctx->dev, "%s+++\n", __func__);

    if (ctx->prepared)
        return 0;

    if (!ktz8866_is_ready()) {
        dev_warn(ctx->dev, "KTZ8866 not ready, defer...\n");
        return -EPROBE_DEFER;
    }

    /* VDD 电源 */
    if (ctx->vdd) {
        ret = regulator_set_voltage(ctx->vdd, 1800000, 1800000);
        if (ret < 0)
            dev_warn(ctx->dev, "vdd set voltage fail: %d\n", ret);
        ret = regulator_enable(ctx->vdd);
        if (ret < 0) {
            dev_err(ctx->dev, "vdd enable failed\n");
            return ret;
        }
    }

    /* 1.8V 电源 */
    if (ctx->v1_8) {
        ret = regulator_set_voltage(ctx->v1_8, 1800000, 1800000);
        if (ret < 0)
            dev_warn(ctx->dev, "v1_8 set voltage fail: %d\n", ret);
        ret = regulator_enable(ctx->v1_8);
        if (ret < 0) {
            dev_err(ctx->dev, "v1_8 enable failed\n");
            if (ctx->vdd)
                regulator_disable(ctx->vdd);
            return ret;
        }
    }

    msleep(20);
    ktz8866_enable_bias(1);
    msleep(10);

    /* Panel reset sequence (from DTS) */
    gpiod_set_value(ctx->reset_gpio, 1);
    msleep(10);
    gpiod_set_value(ctx->reset_gpio, 0);
    msleep(10);
    gpiod_set_value(ctx->reset_gpio, 1);
    msleep(10);
    gpiod_set_value(ctx->reset_gpio, 0);
    msleep(10);
    gpiod_set_value(ctx->reset_gpio, 1);
    msleep(12);

    /* 发送初始化命令 */
    ret = panel_send_cmds(ctx, panel_on_cmds, sizeof(panel_on_cmds));
    if (ret < 0) {
        dev_err(ctx->dev, "init commands failed\n");
        goto err_power_off;
    }

    /* 144Hz 时序切换 (DFPS 支持) */
    if (ctx->dfps_supported) {
        ret = panel_send_cmds(ctx, timing_switch_144hz_cmds,
                              sizeof(timing_switch_144hz_cmds));
        if (ret < 0) {
            dev_warn(ctx->dev, "144Hz timing set failed, using 120Hz\n");
        } else {
            dev_info(ctx->dev, "144Hz timing enabled successfully\n");
        }
    }

    ctx->prepared = true;
    dev_info(ctx->dev, "%s---\n", __func__);
    return 0;

err_power_off:
    ktz8866_enable_bias(0);
    if (ctx->v1_8)
        regulator_disable(ctx->v1_8);
    if (ctx->vdd)
        regulator_disable(ctx->vdd);
    return ret;
}

static int panel_unprepare(struct drm_panel *panel)
{
    struct panel_data *ctx = to_panel_data(panel);

    dev_info(ctx->dev, "%s+++\n", __func__);

    if (!ctx->prepared)
        return 0;

    ktz8866_set_backlight_level(0);
    panel_send_cmds(ctx, panel_off_cmds, sizeof(panel_off_cmds));
    ktz8866_enable_bias(0);
    gpiod_set_value(ctx->reset_gpio, 0);
    msleep(20);

    if (ctx->v1_8)
        regulator_disable(ctx->v1_8);
    if (ctx->vdd)
        regulator_disable(ctx->vdd);

    ctx->prepared = false;
    dev_info(ctx->dev, "%s---\n", __func__);
    return 0;
}

static int panel_enable(struct drm_panel *panel)
{
    struct panel_data *ctx = to_panel_data(panel);

    dev_info(ctx->dev, "%s+++\n", __func__);

    if (ctx->enabled)
        return 0;

    if (ctx->backlight) {
        ctx->backlight->props.power = 1;
        backlight_update_status(ctx->backlight);
    }

    ctx->enabled = true;
    dev_info(ctx->dev, "%s---\n", __func__);
    return 0;
}

static int panel_disable(struct drm_panel *panel)
{
    struct panel_data *ctx = to_panel_data(panel);

    dev_info(ctx->dev, "%s+++\n", __func__);

    if (!ctx->enabled)
        return 0;

    if (ctx->backlight) {
        ctx->backlight->props.power = 4;
        backlight_update_status(ctx->backlight);
    }

    ctx->enabled = false;
    dev_info(ctx->dev, "%s---\n", __func__);
    return 0;
}

static int panel_get_modes(struct drm_panel *panel)
{
    struct panel_data *ctx = to_panel_data(panel);
    struct drm_display_mode *mode;

    dev_info(ctx->dev, "%s+++\n", __func__);

    /* 120Hz 模式 (主要模式) */
    mode = drm_mode_duplicate(panel->drm, &default_mode_120hz);
    if (!mode) {
        dev_err(ctx->dev, "failed to add 120Hz mode\n");
        return -ENOMEM;
    }
    drm_mode_set_name(mode);
    drm_mode_probed_add(panel->connector, mode);

    /* 144Hz 模式 (高性能模式) */
    mode = drm_mode_duplicate(panel->drm, &default_mode_144hz);
    if (!mode) {
        dev_err(ctx->dev, "failed to add 144Hz mode\n");
        return -ENOMEM;
    }
    drm_mode_set_name(mode);
    drm_mode_probed_add(panel->connector, mode);

    panel->connector->display_info.width_mm = PHYSICAL_WIDTH_MM;
    panel->connector->display_info.height_mm = PHYSICAL_HEIGHT_MM;

    dev_info(ctx->dev, "%s--- added 2 modes (120Hz, 144Hz)\n", __func__);
    return 2;
}

/* ============================================================
 * DRM Panel Funcs
 * ============================================================ */

static const struct drm_panel_funcs panel_funcs = {
    .prepare = panel_prepare,
    .unprepare = panel_unprepare,
    .enable = panel_enable,
    .disable = panel_disable,
    .get_modes = panel_get_modes,
};

/* ============================================================
 * Probe / Remove
 * ============================================================ */

static int panel_probe(struct mipi_dsi_device *dsi)
{
    struct device *dev = &dsi->dev;
    struct panel_data *ctx;
    const char *panel_name;
    struct backlight_device *bl;
    int ret;

    dev_info(dev, "%s+++\n", __func__);

    /* 1. 获取 panel name 并匹配 */
    ret = of_property_read_string(dev->of_node, "qcom,mdss-dsi-panel-name", &panel_name);
    if (ret < 0) {
        dev_info(dev, "no qcom,mdss-dsi-panel-name, skipping\n");
        return -ENODEV;
    }

    if (strcmp(panel_name, "nt36532 tianma 3k video mode dsi panel") != 0) {
        dev_info(dev, "panel name mismatch: %s, skipping\n", panel_name);
        return -ENODEV;
    }

    dev_info(dev, "panel matched: %s\n", panel_name);

    /* 2. 分配上下文 */
    ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
    if (!ctx)
        return -ENOMEM;

    ctx->dev = dev;
    ctx->dsi = dsi;
    mipi_dsi_set_drvdata(dsi, ctx);

    /* 3. DSI 配置 */
    dsi->lanes = 4;
    dsi->format = MIPI_DSI_FMT_RGB888;
    dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST;

    /* 4. GPIO */
    ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
    if (IS_ERR(ctx->reset_gpio)) {
        ret = PTR_ERR(ctx->reset_gpio);
        dev_err(dev, "failed to get reset gpio: %d\n", ret);
        return ret;
    }

    ctx->te_gpio = devm_gpiod_get(dev, "te", GPIOD_IN);
    if (IS_ERR(ctx->te_gpio)) {
        ret = PTR_ERR(ctx->te_gpio);
        dev_err(dev, "failed to get te gpio: %d\n", ret);
        return ret;
    }

    /* 5. Regulator */
    ctx->vdd = devm_regulator_get_optional(dev, "vdd");
    if (IS_ERR(ctx->vdd)) {
        if (PTR_ERR(ctx->vdd) == -EPROBE_DEFER)
            return -EPROBE_DEFER;
        ctx->vdd = NULL;
        dev_info(dev, "vdd regulator not found\n");
    }

    ctx->v1_8 = devm_regulator_get_optional(dev, "v1-8");
    if (IS_ERR(ctx->v1_8)) {
        if (PTR_ERR(ctx->v1_8) == -EPROBE_DEFER)
            return -EPROBE_DEFER;
        ctx->v1_8 = NULL;
        dev_info(dev, "v1_8 regulator not found\n");
    }

    /* 6. Backlight */
    bl = devm_backlight_device_register(dev, "nt36532-backlight", dev, ctx,
                                        &panel_bl_ops, NULL);
    if (IS_ERR(bl)) {
        ret = PTR_ERR(bl);
        dev_err(dev, "failed to register backlight: %d\n", ret);
        return ret;
    }
    bl->props.max_brightness = 2047;
    bl->props.brightness = 100;
    bl->props.type = BACKLIGHT_RAW;
    ctx->backlight = bl;

    /* 7. 初始化 DFPS 支持 (60/90/120/144Hz) */
    ctx->dfps_supported = true;
    ctx->dfps_rates[0] = 60;
    ctx->dfps_rates[1] = 90;
    ctx->dfps_rates[2] = 120;
    ctx->dfps_rates[3] = 144;
    ctx->dfps_count = 4;
    dev_info(dev, "DFPS supported: 60/90/120/144Hz\n");

    /* 8. Register panel (4.19 内核只有一个参数) */
    drm_panel_init(&ctx->panel);
    ctx->panel.dev = dev;
    ctx->panel.funcs = &panel_funcs;

    ret = drm_panel_add(&ctx->panel);
    if (ret < 0) {
        dev_err(dev, "failed to add panel: %d\n", ret);
        return ret;
    }

    ret = mipi_dsi_attach(dsi);
    if (ret < 0) {
        dev_err(dev, "failed to attach dsi: %d\n", ret);
        drm_panel_remove(&ctx->panel);
        return ret;
    }

    dev_info(dev, "%s--- success\n", __func__);
    return 0;
}

static int panel_remove(struct mipi_dsi_device *dsi)
{
    struct panel_data *ctx = mipi_dsi_get_drvdata(dsi);

    mipi_dsi_detach(dsi);
    drm_panel_remove(&ctx->panel);
    return 0;
}

static const struct of_device_id panel_of_match[] = {
    { .compatible = "tianma,nt36532-tianma-video" },
    { },
};
MODULE_DEVICE_TABLE(of, panel_of_match);

static struct mipi_dsi_driver panel_driver = {
    .probe = panel_probe,
    .remove = panel_remove,
    .driver = {
        .name = "panel-nt36532-tianma",
        .of_match_table = panel_of_match,
    },
};

module_mipi_dsi_driver(panel_driver);

MODULE_AUTHOR("Migration from MTK to QCOM");
MODULE_DESCRIPTION("nt36532 tianma 3k video mode DSI panel driver with 144Hz support");
MODULE_LICENSE("GPL v2");