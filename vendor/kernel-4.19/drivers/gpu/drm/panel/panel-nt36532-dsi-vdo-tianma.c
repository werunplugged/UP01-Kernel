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
#include <linux/delay.h>

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>

#define CONFIG_MTK_PANEL_EXT
#if defined(CONFIG_MTK_PANEL_EXT)
#include "../mediatek/mtk_panel_ext.h"
#include "../mediatek/mtk_log.h"
#include "../mediatek/mtk_drm_graphics_base.h"
#endif

#include "ite6113.h"
#include "ktz8866.h"

struct it6112 it6112_dev;
struct init_table_info table_info;

struct lcm {
	struct device *dev;
	struct drm_panel panel;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *led_en_gpio;
	struct regulator *it6113_v1_8, *lcm_v1_8;
	struct gpio_desc *it6113_v1_8_en, *it6113_v1_0_en;
	struct it6112 *it6112_client;

	bool prepared;
	bool enabled;

	int error;
};

static inline struct lcm *panel_to_lcm(struct drm_panel *panel)
{
	return container_of(panel, struct lcm, panel);
}

static int lcm_regulator_enable(struct regulator *reg, unsigned int vol)
{
	int ret = 0;

	if (!reg) {
		pr_info("regulator connot find\n");
		return -1;
	}

	ret = regulator_set_voltage(reg, vol, vol);
	if (ret) {
		pr_info("regulator set voltage fail\n");
		return ret;
	}

	if (vol == regulator_get_voltage(reg))
		pr_info("check vol pass!\n");
	else
		pr_info("check vol fail!\n");

	ret = regulator_enable(reg);
	if (ret)
		pr_info("regulator enable fail\n");

	return ret;
}

static int lcm_regulator_disable(struct regulator *reg)
{
	if (!reg) {
		pr_info("regulator connot find\n");
		return -1;
	}

	if (regulator_is_enabled(reg))
		if (regulator_disable(reg))
			pr_info("regulator disable fail\n");

	return 0;
}

static struct dcs_setting_entry lcm_init_table_tianma[] = {
	{REGW0, LP_CMD_LPDT, 0x23, 2, {0xFF, 0x10}},
	{REGW1, LP_CMD_LPDT, 0x23, 2, {0xFB, 0x01}},
	{REGW2, LP_CMD_LPDT, 0x39, 7, {0x3B, 0x03, 0x60, 0x1A, 0x0A, 0x0A, 0x00}},
	{REGW3, LP_CMD_LPDT, 0x05, 1, {0x11}},
	{DELAY, 120, 0, 0, {0}},
	{REGW4, LP_CMD_LPDT, 0x05, 1, {0x29}},
	{DELAY, 20, 0, 0, {0}},
};

void push_table(struct drm_panel *panel, const struct dcs_setting_entry *init_table, int _size,
		enum dcs_cmd_name start, int count)
{
	unsigned int i;
	int ret;
	struct lcm *ctx = panel_to_lcm(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);

	for(i = start; i < start + count; i++) {
		if (init_table[i].cmd_name == DELAY) {
			mdelay(init_table[i].cmd);
			continue;
		} else {
			if (init_table[i].data_id == 0x05
				|| init_table[i].data_id == 0x15
				|| init_table[i].data_id == 0x39)
				ret = mipi_dsi_dcs_write_buffer(dsi,
					init_table[i].para_list, init_table[i].count);
			else
				ret = mipi_dsi_generic_write(dsi,
					init_table[i].para_list, init_table[i].count);
			if (ret < 0) {
				pr_info("dsi write error! send 0x%x=0x%x\n",
					init_table[i].para_list[0], ret);
				ctx->error = ret;
			}
		}
	}
}

static void lcm_panel_init(struct lcm *ctx)
{
	pr_info("%s+++\n",__func__);

	gpiod_set_value(ctx->it6113_v1_0_en, 1);
	gpiod_set_value(ctx->it6113_v1_8_en, 1);

	lcm_regulator_enable(ctx->it6113_v1_8, 1800000);
	lcm_regulator_enable(ctx->lcm_v1_8, 1800000);

	gpiod_set_value(ctx->reset_gpio, 1);
	mdelay(5);
	gpiod_set_value(ctx->reset_gpio, 0);
	mdelay(5);
	gpiod_set_value(ctx->reset_gpio, 1);
	mdelay(15);

	chip_init(ctx->it6112_client);

	pr_info("%s---\n",__func__);
}

static int lcm_disable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	pr_info("%s+++\n",__func__);

	if (!ctx->enabled)
		return 0;

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

	device_power_off(ctx->it6112_client);

	ctx->error = 0;
	ctx->prepared = false;

	gpiod_set_value(ctx->reset_gpio, 0);
	mdelay(15);

	lcd_set_bias(0);
	lcm_regulator_disable(ctx->it6113_v1_8);
	lcm_regulator_disable(ctx->lcm_v1_8);
	gpiod_set_value(ctx->it6113_v1_0_en, 0);
	gpiod_set_value(ctx->it6113_v1_8_en, 0);
	gpiod_set_value(ctx->led_en_gpio, 0);
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

	gpiod_set_value(ctx->led_en_gpio, 1);
	lcd_set_bias(1);

	lcm_panel_init(ctx);

	ret = ctx->error;
	if (ret < 0) {
		pr_info("error! return\n");
		lcm_unprepare(panel);
	}

	ctx->prepared = true;

#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_tch_rst(panel);
#endif

	pr_info("%s---\n", __func__);

	return ret;
}

static int lcm_enable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	pr_info("%s+++\n", __func__);

	if (ctx->enabled)
		return 0;

	ctx->enabled = true;

	pr_info("%s---\n", __func__);

	return 0;
}

#define HFP (86)
#define HSA (10)
#define HBP (86)
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

	gpiod_set_value(ctx->reset_gpio, on);

	return 0;
}

static int panel_ata_check(struct drm_panel *panel)
{
	return 0;
}

static struct mtk_panel_params ext_params = {
	.pll_clk = 1150,
	.ssc_disable = 1,
	.cust_esd_check = 0,
	.esd_check_enable = 0,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0a,
		.count = 1,
		.para_list[0] = 0x9c,
	},
};

static int lcm_setbacklight_by_bridge(struct drm_panel *panel, dcs_write_gce cb,
	void *handle, unsigned int level)
{
//	struct lcm *ctx = panel_to_lcm(panel);
	int map_level = level * 0x7FF / 1024;

	pr_info("%s: level=%d, map_level=%d\n", __func__, level, map_level);
//	it6112_set_backlight(ctx->it6112_client, map_level);
	lcd_bl_set_led_brightness(map_level);

	return 0;
}

static int lcm_read_panel(struct drm_panel *panel, u8 *buff)
{
	struct lcm *ctx = panel_to_lcm(panel);

	it6112_read_ddic_reg(ctx->it6112_client, buff);
	return 0;
}

static struct mtk_panel_funcs ext_funcs = {
	.reset = panel_ext_reset,
	.ata_check = panel_ata_check,
	.set_backlight_bridge = lcm_setbacklight_by_bridge,
	.read_panel = lcm_read_panel,
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
	int ret;

	pr_info("%s +++\n",__func__);

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

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_info(dev, "%s: cannot get reset-gpios %ld\n",
				__func__, PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	gpiod_set_value(ctx->reset_gpio, 1);

	ctx->led_en_gpio = devm_gpiod_get(dev, "led-en", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->led_en_gpio)) {
		dev_info(dev, "%s: cannot get led_en_gpio %ld\n",
				__func__, PTR_ERR(ctx->led_en_gpio));
		return PTR_ERR(ctx->led_en_gpio);
	}
	gpiod_set_value(ctx->led_en_gpio, 1);

	ctx->it6113_v1_0_en = devm_gpiod_get(dev, "it6113-1v0", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->it6113_v1_0_en)) {
		dev_info(dev, "%s: cannot get it6113_v1_0_en-gpios %ld\n",
				__func__, PTR_ERR(ctx->it6113_v1_0_en));
		return PTR_ERR(ctx->it6113_v1_0_en);
	}
	gpiod_set_value(ctx->it6113_v1_0_en, 1);

	ctx->it6113_v1_8_en = devm_gpiod_get(dev, "it6113-1v8", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->it6113_v1_8_en)) {
		dev_info(dev, "%s: cannot get it6113_v1_8_en-gpios %ld\n",
				__func__, PTR_ERR(ctx->it6113_v1_8_en));
		return PTR_ERR(ctx->it6113_v1_8_en);
	}
	gpiod_set_value(ctx->it6113_v1_8_en, 1);

	ctx->it6113_v1_8 = devm_regulator_get(dev, "reg-6113-v1_8");
	if (IS_ERR(ctx->it6113_v1_8)) {
		dev_info(dev, "%s: cannot get it6113_v1_8 %ld\n", PTR_ERR(ctx->it6113_v1_8));
		return PTR_ERR(ctx->it6113_v1_8);
	}

	ctx->lcm_v1_8 = devm_regulator_get(dev, "reg-lcm-v1_8");
	if (IS_ERR(ctx->lcm_v1_8)) {
		dev_info(dev, "%s: cannot get lcm_v1_8 %ld\n", PTR_ERR(ctx->lcm_v1_8));
		return PTR_ERR(ctx->lcm_v1_8);
	}

	ret = lcm_regulator_enable(ctx->it6113_v1_8, 1800000);
	mdelay(5);
	ret |= lcm_regulator_enable(ctx->lcm_v1_8, 1800000);
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

	table_info.init_cmd_table = &lcm_init_table_tianma[0];
	table_info.count = ARRAY_SIZE(lcm_init_table_tianma);

	it6112_dev.init_table = &table_info;
	it6112_dev.push_table = push_table;
	it6112_dev.panel = &ctx->panel;
	ctx->it6112_client = &it6112_dev;

#if defined(CONFIG_MTK_PANEL_EXT)
	ret = mtk_panel_ext_create(dev, &ext_params, &ext_funcs, &ctx->panel);
	if (ret < 0)
		return ret;
#endif

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
	{ .compatible = "tianma,nt36532,vdo,TL127VVMS01", },
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

MODULE_AUTHOR("henry tu <henry.tu@mediatek.com>");
MODULE_DESCRIPTION("nova nt36532 VDO LCD Panel Driver");
MODULE_LICENSE("GPL v2");
