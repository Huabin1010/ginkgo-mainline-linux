// SPDX-License-Identifier: GPL-2.0-only
/*
 * Huaxing (CSOT) FT8719 video-mode panel for Xiaomi Redmi Note 8 (ginkgo).
 *
 * Init sequence and timings from downstream
 * dsi-panel-ft8719-huaxing-fhd-video.dtsi. DCS is sent in enable() after
 * the MSM DSI host is up — same split as the ginkgo Tianma NT36672A driver.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>

static const char * const regulator_names[] = {
	"vddio",
	"vddpos",
	"vddneg",
};

static const unsigned long regulator_enable_loads[] = {
	62000,
	100000,
	100000,
};

struct ft8719_cmd {
	u8 len;
	u8 wait_ms;
	u8 data[16];
};

struct huaxing_ft8719 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data supplies[ARRAY_SIZE(regulator_names)];
	struct gpio_desc *reset_gpio;
};

static inline struct huaxing_ft8719 *to_huaxing_ft8719(struct drm_panel *panel)
{
	return container_of(panel, struct huaxing_ft8719, panel);
}

/* Manufacturer / gamma pages, before sleep-out. */
static const struct ft8719_cmd on_cmds_1[] = {
	{ .len = 2,  .data = { 0x00, 0x00 } },
	{ .len = 4,  .data = { 0xff, 0x87, 0x19, 0x01 } },
	{ .len = 2,  .data = { 0x00, 0x80 } },
	{ .len = 3,  .data = { 0xff, 0x87, 0x19 } },
	{ .len = 2,  .data = { 0x00, 0x80 } },
	{ .len = 13, .data = { 0xca, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
			       0x80, 0x80, 0x80, 0x80, 0x80 } },
	{ .len = 2,  .data = { 0x00, 0x90 } },
	{ .len = 10, .data = { 0xca, 0xfe, 0xff, 0x66, 0xf6, 0xff, 0x66, 0xfb,
			       0xff, 0x32 } },
	{ .len = 2,  .data = { 0x00, 0xb5 } },
	{ .len = 2,  .data = { 0xca, 0x06 } },
	{ .len = 2,  .data = { 0x00, 0xb2 } },
	{ .len = 2,  .data = { 0xca, 0x0c } },
};

/* DCS brightness after display-on. Downstream 0x51=0xB8, 0x53=0x24, CABC off. */
static const struct ft8719_cmd on_cmds_2[] = {
	{ .len = 2, .data = { MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0xb8 } },
	{ .len = 2, .data = { MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x24 } },
	{ .len = 2, .data = { MIPI_DCS_WRITE_POWER_SAVE, 0x00 } },
};

static const struct ft8719_cmd off_cmds[] = {
	{ .len = 2, .wait_ms = 20,  .data = { MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x24 } },
	{ .len = 2, .wait_ms = 20,  .data = { MIPI_DCS_SET_DISPLAY_OFF, 0x00 } },
	{ .len = 2, .wait_ms = 120, .data = { MIPI_DCS_ENTER_SLEEP_MODE, 0x00 } },
	{ .len = 2, .data = { 0x00, 0x00 } },
	{ .len = 5, .data = { 0xf7, 0x5a, 0xa5, 0x95, 0x27 } },
};

static int huaxing_ft8719_send_cmds(struct huaxing_ft8719 *ctx,
				    const struct ft8719_cmd *cmds, unsigned int n)
{
	unsigned int i;
	int err;

	for (i = 0; i < n; i++) {
		err = mipi_dsi_dcs_write_buffer(ctx->dsi, cmds[i].data, cmds[i].len);
		if (err < 0)
			return err;
		if (cmds[i].wait_ms)
			msleep(cmds[i].wait_ms);
	}

	return 0;
}

static int huaxing_ft8719_prepare(struct drm_panel *panel)
{
	struct huaxing_ft8719 *ctx = to_huaxing_ft8719(panel);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0)
		return ret;

	msleep(20);

	/* Downstream: <1 10>, <0 10>, <1 10> on a raw active-low reset GPIO. */
	gpiod_set_value(ctx->reset_gpio, 0);
	msleep(10);
	gpiod_set_value(ctx->reset_gpio, 1);
	msleep(10);
	gpiod_set_value(ctx->reset_gpio, 0);
	msleep(10);

	return 0;
}

static int huaxing_ft8719_enable(struct drm_panel *panel)
{
	struct huaxing_ft8719 *ctx = to_huaxing_ft8719(panel);
	u8 power_mode = 0;
	int err;

	err = huaxing_ft8719_send_cmds(ctx, on_cmds_1, ARRAY_SIZE(on_cmds_1));
	if (err < 0) {
		dev_err(panel->dev, "failed to send DCS init: %d\n", err);
		return err;
	}

	err = mipi_dsi_dcs_exit_sleep_mode(ctx->dsi);
	if (err < 0) {
		dev_err(panel->dev, "exit sleep failed: %d\n", err);
		return err;
	}
	msleep(120);

	err = mipi_dsi_dcs_set_display_on(ctx->dsi);
	if (err < 0) {
		dev_err(panel->dev, "display on failed: %d\n", err);
		return err;
	}
	msleep(20);

	err = huaxing_ft8719_send_cmds(ctx, on_cmds_2, ARRAY_SIZE(on_cmds_2));
	if (err < 0) {
		dev_err(panel->dev, "failed to send brightness DCS: %d\n", err);
		return err;
	}

	err = mipi_dsi_dcs_get_power_mode(ctx->dsi, &power_mode);
	if (err < 0)
		dev_info(panel->dev, "power mode readback failed: %d\n", err);
	else
		dev_info(panel->dev, "power mode readback: %#x\n", power_mode);

	dev_info(panel->dev, "panel init complete (huaxing ft8719)\n");
	return 0;
}

static int huaxing_ft8719_disable(struct drm_panel *panel)
{
	struct huaxing_ft8719 *ctx = to_huaxing_ft8719(panel);
	int err;

	err = mipi_dsi_dcs_set_display_off(ctx->dsi);
	if (err < 0)
		dev_err(panel->dev, "set_display_off failed: %d\n", err);

	return err;
}

static int huaxing_ft8719_unprepare(struct drm_panel *panel)
{
	struct huaxing_ft8719 *ctx = to_huaxing_ft8719(panel);
	int ret;

	huaxing_ft8719_send_cmds(ctx, off_cmds, ARRAY_SIZE(off_cmds));

	gpiod_set_value(ctx->reset_gpio, 1);

	ret = regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret)
		dev_err(panel->dev, "regulator_bulk_disable failed %d\n", ret);

	return ret;
}

/*
 * Downstream: HFP=72 HSW=4 HBP=80, VFP=112 VSW=4 VBP=12, 1080x2340@60.
 * VFP is much larger than Tianma (10); leave DPU prefetch off anyway.
 */
static const struct drm_display_mode huaxing_ft8719_mode = {
	.clock = (1080 + 72 + 4 + 80) * (2340 + 112 + 4 + 12) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 72,
	.hsync_end = 1080 + 72 + 4,
	.htotal = 1080 + 72 + 4 + 80,
	.vdisplay = 2340,
	.vsync_start = 2340 + 112,
	.vsync_end = 2340 + 112 + 4,
	.vtotal = 2340 + 112 + 4 + 12,
	.width_mm = 67,
	.height_mm = 145,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static int huaxing_ft8719_get_modes(struct drm_panel *panel,
				    struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &huaxing_ft8719_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs huaxing_ft8719_funcs = {
	.prepare = huaxing_ft8719_prepare,
	.enable = huaxing_ft8719_enable,
	.disable = huaxing_ft8719_disable,
	.unprepare = huaxing_ft8719_unprepare,
	.get_modes = huaxing_ft8719_get_modes,
};

static int huaxing_ft8719_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct huaxing_ft8719 *ctx;
	int i, ret;

	ctx = devm_drm_panel_alloc(dev, struct huaxing_ft8719, panel,
				   &huaxing_ft8719_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	for (i = 0; i < ARRAY_SIZE(ctx->supplies); i++) {
		ctx->supplies[i].supply = regulator_names[i];
		ctx->supplies[i].init_load_uA = regulator_enable_loads[i];
	}

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to get regulators\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "failed to get reset gpio\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	/*
	 * Downstream: non_burst_sync_event, no force-clock-lane-hs.
	 * MIPI_DSI_CLOCK_NON_CONTINUOUS keeps LANE_CTRL bit28 clear.
	 */
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_VIDEO
			| MIPI_DSI_CLOCK_NON_CONTINUOUS;

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret == -ENODEV)
		ret = 0;
	else if (ret)
		return dev_err_probe(dev, ret, "failed to get backlight\n");

	ctx->panel.prepare_prev_first = true;
	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void huaxing_ft8719_remove(struct mipi_dsi_device *dsi)
{
	struct huaxing_ft8719 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id huaxing_ft8719_of_match[] = {
	{ .compatible = "huaxing,ginkgo-ft8719" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, huaxing_ft8719_of_match);

static struct mipi_dsi_driver huaxing_ft8719_driver = {
	.probe = huaxing_ft8719_probe,
	.remove = huaxing_ft8719_remove,
	.driver = {
		.name = "panel-huaxing-ft8719",
		.of_match_table = huaxing_ft8719_of_match,
	},
};
module_mipi_dsi_driver(huaxing_ft8719_driver);

MODULE_AUTHOR("Huabin Huang");
MODULE_DESCRIPTION("Huaxing FT8719 MIPI-DSI panel (ginkgo)");
MODULE_LICENSE("GPL");
