// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024
 * Driver for nt36532 tianma 3k video mode dsi panel
 *
 * Based on DTS node: qcom,mdss_dsi_panel_spinel_tianma_nt36532_dsc_3k_video
 * Compatible: "tianma,nt36532-tianma-video"
 *
 * Physical resolution: 2944x1840 (dual DSI, each 1472x1840)
 * DSC: Enabled, slice width 736, slice height 20
 * Backlight: KTZ8866 (I2C)
 * Brightness range: 0-2047 (11-bit)
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/err.h>

#include <drm/drm_panel.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_dsc.h>

#include <linux/platform_data/ktz8866.h>

/* ===== Display Timing (from DTS) ===== */
#define HFP 201   /* 0xC9 */
#define HSA 12    /* 0x0C */
#define HBP 60    /* 0x3C */
#define VFP 26    /* 0x1A */
#define VSA 2     /* 0x02 */
#define VBP 182   /* 0xB6 */
#define VACT 1840 /* 0x730 */
#define HACT 1472 /* 0x5C0, per DSI, total = 2944 */

/* ===== Physical Size (mm) ===== */
#define PHYSICAL_WIDTH_MM   274  /* 0x112 */
#define PHYSICAL_HEIGHT_MM  171  /* 0xAB */

/* ===== Panel Data Structure ===== */
struct panel_data {
    struct drm_panel panel;
    struct mipi_dsi_device *dsi;
    struct device *dev;

    /* GPIOs */
    struct gpio_desc *reset_gpio;
    struct gpio_desc *te_gpio;

    /* Regulators */
    struct regulator *vdd;
    struct regulator *v1_8;

    /* Backlight */
    struct backlight_device *backlight;

    bool prepared;
    bool enabled;
};

static inline struct panel_data *to_panel_data(struct drm_panel *panel)
{
    return container_of(panel, struct panel_data, panel);
}

/* ===== DSC Configuration (from DTS) ===== */
static const struct drm_dsc_config dsc_cfg = {
    .version = 0x11,
    .slice_height = 20,          /* 0x14 */
    .slice_width = 736,          /* 0x2E0 */
    .bits_per_component = 8,
    .bits_per_pixel = 8,
    .block_pred_enable = true,
};

/* ===== Display Mode ===== */
static const struct drm_display_mode default_mode = {
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

/* ============================================================
 * Panel Commands (from DTS qcom,mdss-dsi-on-command)
 * ============================================================ */

static const u8 panel_on_cmds[] = {
    /* Page 0x20 */
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x20,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xfb, 0x01,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x32, 0x72,

    /* Page 0x23 */
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x23,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xfb, 0x01,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x75, 0x03,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x76, 0x07,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x7a, 0xcd,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xbc, 0x04,

    /* Page 0x26 - Gamma/DGC */
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

    /* Page 0x10 */
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x10,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xfb, 0x01,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x07, 0x3b, 0x03, 0xb8, 0x1a, 0x0a, 0x0a, 0x00,

    /* Page 0x27 */
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x27,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xfb, 0x01,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x13, 0x06,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xd0, 0x31,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xd1, 0x84,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xd2, 0x38,

    /* Page 0x25 */
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x25,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xfb, 0x01,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x0f, 0x20,

    /* Page 0x23 - TE setup */
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x23,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xfb, 0x01,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x04, 0xbc, 0x04, 0x00, 0x3c,

    /* Page 0x10 */
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x10,
    0x39, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0xfb, 0x01,

    /* Sleep Out */
    0x05, 0x01, 0x00, 0x00, 0x78, 0x00, 0x02, 0x11, 0x00,
    /* Display On */
    0x05, 0x01, 0x00, 0x00, 0x64, 0x00, 0x02, 0x29, 0x00,
};

static const u8 panel_off_cmds[] = {
    0x05, 0x01, 0x00, 0x00, 0x0f, 0x00, 0x02, 0x28, 0x00,
    0x05, 0x01, 0x00, 0x00, 0x3c, 0x00, 0x02, 0x10, 0x00,
};

/* ============================================================
 * Command Sending Helper
 * ============================================================ */

static int panel_send_cmds(struct panel_data *ctx, const u8 *cmds, size_t len)
{
    struct mipi_dsi_device *dsi = ctx->dsi;
    size_t i = 0;
    int ret;

    while (i < len) {
        u8 type = cmds[i];
        u8 flags = cmds[i + 1];
        u8 delay = cmds[i + 2];
        u8 payload_len = cmds[i + 3];
        const u8 *payload = &cmds[i + 4];

        if (type == 0x05) {
            /* DCS Short Write */
            ret = mipi_dsi_dcs_write(dsi, payload[0], payload + 1, payload_len - 1);
        } else if (type == 0x15) {
            /* Generic Short Write */
            ret = mipi_dsi_generic_write(dsi, payload, payload_len);
        } else if (type == 0x39) {
            /* DCS Long Write */
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
 * Backlight Control
 * ============================================================ */

static int panel_set_backlight(struct drm_panel *panel, u32 brightness)
{
    struct panel_data *ctx = to_panel_data(panel);

    brightness = clamp(brightness, 0u, 2047u);

    /* 调用 KTZ8866 简化接口 */
    ktz8866_set_backlight_level(brightness);

    dev_dbg(ctx->dev, "set brightness: %d\n", brightness);
    return 0;
}

/* ============================================================
 * DRM Panel Operations
 * ============================================================ */

static int panel_prepare(struct drm_panel *panel)
{
    struct panel_data *ctx = to_panel_data(panel);
    int ret;

    dev_info(ctx->dev, "%s+++\n", __func__);

    if (ctx->prepared)
        return 0;

    /* 1. Check KTZ8866 driver ready */
    if (!ktz8866_is_ready()) {
        dev_warn(ctx->dev, "KTZ8866 not ready, defer...\n");
        return -EPROBE_DEFER;
    }

    /* 2. Enable regulators */
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

    /* 3. Enable bias (GPIO controlled by KTZ8866 driver) */
    ktz8866_enable_bias(1);
    msleep(10);

    /* 4. Panel reset sequence (from DTS) */
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

    /* 5. Send init commands */
    ret = panel_send_cmds(ctx, panel_on_cmds, sizeof(panel_on_cmds));
    if (ret < 0) {
        dev_err(ctx->dev, "init commands failed\n");
        goto err_power_off;
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

    /* 1. Turn off backlight */
    ktz8866_set_backlight_level(0);

    /* 2. Send off commands */
    panel_send_cmds(ctx, panel_off_cmds, sizeof(panel_off_cmds));

    /* 3. Disable bias */
    ktz8866_enable_bias(0);

    /* 4. Reset low */
    gpiod_set_value(ctx->reset_gpio, 0);
    msleep(20);

    /* 5. Disable regulators */
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
        ctx->backlight->props.power = FB_BLANK_UNBLANK;
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
        ctx->backlight->props.power = FB_BLANK_POWERDOWN;
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

    mode = drm_mode_duplicate(panel->drm, &default_mode);
    if (!mode) {
        dev_err(ctx->dev, "failed to add mode\n");
        return -ENOMEM;
    }

    drm_mode_set_name(mode);
    drm_mode_probed_add(panel->connector, mode);

    panel->connector->display_info.width_mm = PHYSICAL_WIDTH_MM;
    panel->connector->display_info.height_mm = PHYSICAL_HEIGHT_MM;

    dev_info(ctx->dev, "%s---\n", __func__);
    return 1;
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
    .set_backlight = panel_set_backlight,
};

/* ============================================================
 * Probe / Remove
 * ============================================================ */

static int panel_probe(struct mipi_dsi_device *dsi)
{
    struct device *dev = &dsi->dev;
    struct panel_data *ctx;
    int ret;

    dev_info(dev, "%s+++\n", __func__);

    ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
    if (!ctx)
        return -ENOMEM;

    ctx->dev = dev;
    ctx->dsi = dsi;

    mipi_dsi_set_drvdata(dsi, ctx);

    /* DSI config */
    dsi->lanes = 4;
    dsi->format = MIPI_DSI_FMT_RGB888;
    dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST;

    /* Get GPIOs (from DTS: reset=gpio69, te=gpio66) */
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

    /* Regulators */
    ctx->vdd = devm_regulator_get_optional(dev, "vdd");
    if (IS_ERR(ctx->vdd)) {
        if (PTR_ERR(ctx->vdd) == -EPROBE_DEFER) {
            dev_info(dev, "vdd regulator not ready, defer...\n");
            return -EPROBE_DEFER;
        }
        ctx->vdd = NULL;
        dev_info(dev, "vdd regulator not found, skip\n");
    }

    ctx->v1_8 = devm_regulator_get_optional(dev, "v1-8");
    if (IS_ERR(ctx->v1_8)) {
        if (PTR_ERR(ctx->v1_8) == -EPROBE_DEFER) {
            dev_info(dev, "v1_8 regulator not ready, defer...\n");
            return -EPROBE_DEFER;
        }
        ctx->v1_8 = NULL;
        dev_info(dev, "v1_8 regulator not found, skip\n");
    }

    /* Backlight */
    ctx->backlight = devm_of_find_backlight(dev);
    if (IS_ERR(ctx->backlight)) {
        if (PTR_ERR(ctx->backlight) == -EPROBE_DEFER) {
            dev_info(dev, "backlight not ready, defer...\n");
            return -EPROBE_DEFER;
        }
        ctx->backlight = NULL;
        dev_info(dev, "backlight not found, skip\n");
    }

    /* Register panel */
    drm_panel_init(&ctx->panel, dev, &panel_funcs, DRM_MODE_CONNECTOR_DSI);
    ctx->panel.dsc = &dsc_cfg;

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
    { }
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
MODULE_DESCRIPTION("nt36532 tianma 3k video mode DSI panel driver");
MODULE_LICENSE("GPL v2");