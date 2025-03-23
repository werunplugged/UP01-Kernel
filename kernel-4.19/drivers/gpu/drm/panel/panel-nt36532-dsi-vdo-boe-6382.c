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

#define CONFIG_MTK_PANEL_EXT
#if defined(CONFIG_MTK_PANEL_EXT)
#include "../mediatek/mtk_panel_ext.h"
#include "../mediatek/mtk_log.h"
#include "../mediatek/mtk_drm_graphics_base.h"
#endif

#ifdef CONFIG_MTK_ROUND_CORNER_SUPPORT
#include "../mediatek/mtk_corner_pattern/mtk_data_hw_roundedpattern.h"
#endif

#include "ktz8866.h"

struct lcm {
	struct device *dev;
	struct drm_panel panel;
	struct gpio_desc *pm_enable_gpio;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *panel_id1, *panel_id2;
	struct regulator *lcm_v1_8;

	int panel_id;

	bool prepared;
	bool enabled;

	int error;
};

#define lcm_dcs_write_seq(ctx, seq...) \
	({\
	const u8 d[] = { seq };\
	BUILD_BUG_ON_MSG(ARRAY_SIZE(d) > 64, "DCS sequence too big for stack");\
	lcm_dcs_write(ctx, d, ARRAY_SIZE(d));\
	if (ARRAY_SIZE(d) > 1) {\
		usleep_range(10, 20); }\
	})

#define lcm_dcs_write_seq_static(ctx, seq...) \
	({\
	static const u8 d[] = { seq };\
	lcm_dcs_write(ctx, d, ARRAY_SIZE(d));\
	if (ARRAY_SIZE(d) > 2) {\
		usleep_range(10, 20); }\
	})

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

static inline struct lcm *panel_to_lcm(struct drm_panel *panel)
{
	return container_of(panel, struct lcm, panel);
}

static void lcm_dcs_write(struct lcm *ctx, const void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;
	char *addr;

	if (ctx->error < 0)
		return;

	addr = (char *)data;
	if ((int)*addr < 0xB0)
		ret = mipi_dsi_dcs_write_buffer(dsi, data, len);
	else
		ret = mipi_dsi_generic_write(dsi, data, len);
	if (ret < 0) {
		dev_info(ctx->dev, "error %zd writing seq: %ph\n", ret, data);
		ctx->error = ret;
	}
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

static void lcm_panel_init(struct lcm *ctx)
{
	pr_info("%s+++\n",__func__);

	lcm_regulator_enable(ctx->lcm_v1_8, 1800000);

	gpiod_set_value(ctx->reset_gpio, 1);
	msleep(20);
	gpiod_set_value(ctx->reset_gpio, 0);
	msleep(20);
	gpiod_set_value(ctx->reset_gpio, 1);
	msleep(20);

	if (ctx->panel_id) {
		pr_info("driver for tianma!\n");
		lcm_dcs_write_seq_static(ctx, 0xFF, 0x26);
		lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
		lcm_dcs_write_seq_static(ctx, 0x04, 0x55);

		lcm_dcs_write_seq_static(ctx, 0xFF, 0x2A);
		lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
		lcm_dcs_write_seq_static(ctx, 0xD0, 0x84);
		lcm_dcs_write_seq_static(ctx, 0xCC, 0xA5,0x5A,0x00,0xA5,0x5A,0x00);

		lcm_dcs_write_seq_static(ctx, 0xFF, 0x24);
		lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
		lcm_dcs_write_seq_static(ctx, 0x1C, 0x80);
		lcm_dcs_write_seq_static(ctx, 0x95, 0x09);
		lcm_dcs_write_seq_static(ctx, 0xBC, 0x00,0x00,0x43);

		lcm_dcs_write_seq_static(ctx, 0xFF, 0x23);
		lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
		lcm_dcs_write_seq_static(ctx, 0x00, 0x60);
		lcm_dcs_write_seq_static(ctx, 0x07, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x08, 0x02);
		lcm_dcs_write_seq_static(ctx, 0x09, 0x00);

		lcm_dcs_write_seq_static(ctx, 0xFF, 0x10);
		lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
		lcm_dcs_write_seq_static(ctx, 0x90, 0x13);
		lcm_dcs_write_seq_static(ctx, 0x91, 0x89, 0x28, 0x00, 0x14, 0xD2, 0x00, 0x00,
			0x00, 0x02, 0xA1, 0x00, 0x14, 0x05, 0x7A, 0x01, 0xDA);
		lcm_dcs_write_seq_static(ctx, 0x92, 0x10, 0xE0);

		lcm_dcs_write_seq_static(ctx, 0xFF, 0x10);
		lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
		lcm_dcs_write_seq_static(ctx, 0x35, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x3B, 0x03,0x57,0x1A,0x0A,0x0A,0x00);
		lcm_dcs_write_seq_static(ctx, 0x3B, 0x03,0x57,0x1A,0x0A,0x0A,0x00);
		lcm_dcs_write_seq_static(ctx, 0x51, 0x07, 0xFF);
		lcm_dcs_write_seq_static(ctx, 0x53, 0x2C);
		lcm_dcs_write_seq_static(ctx, 0x55, 0x00);
	} else {
		pr_info("driver for boe!\n");
		lcm_dcs_write_seq_static(ctx, 0xFF, 0x20);
		lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
		lcm_dcs_write_seq_static(ctx, 0x05, 0x81);
		lcm_dcs_write_seq_static(ctx, 0x07, 0x4B);
		lcm_dcs_write_seq_static(ctx, 0x08, 0x23);
		lcm_dcs_write_seq_static(ctx, 0x0F, 0x96);
		lcm_dcs_write_seq_static(ctx, 0x10, 0x46);
		lcm_dcs_write_seq_static(ctx, 0x65, 0xAA);
		lcm_dcs_write_seq_static(ctx, 0x6D, 0xAA);
		lcm_dcs_write_seq_static(ctx, 0x78, 0x93);
		lcm_dcs_write_seq_static(ctx, 0x89, 0x73);
		lcm_dcs_write_seq_static(ctx, 0x8A, 0x73);
		lcm_dcs_write_seq_static(ctx, 0x8D, 0xAF);
		lcm_dcs_write_seq_static(ctx, 0xB0, 0x00, 0x00, 0x00,
			0x1D, 0x00, 0x49, 0x00, 0x6A, 0x00, 0x87, 0x00,
			0x9F, 0x00, 0xB5, 0x00, 0xC8);
		lcm_dcs_write_seq_static(ctx, 0xB1, 0x00, 0xDA, 0x01,
			0x13, 0x01, 0x40, 0x01, 0x83, 0x01, 0xB7, 0x02,
			0x05, 0x02, 0x43, 0x02, 0x45);
		lcm_dcs_write_seq_static(ctx, 0xB2, 0x02, 0x81, 0x02,
			0xC5, 0x02, 0xF0, 0x03, 0x27, 0x03, 0x4A, 0x03,
			0x78, 0x03, 0x86, 0x03, 0x95);
		lcm_dcs_write_seq_static(ctx, 0xB3, 0x03, 0xA5, 0x03,
			0xB5, 0x03, 0xDC, 0x03, 0xF4, 0x03, 0xFE, 0x03,
			0xFF, 0x00, 0x00);
		lcm_dcs_write_seq_static(ctx, 0xB4, 0x00, 0x00, 0x00,
			0x1D, 0x00, 0x49, 0x00, 0x6A, 0x00, 0x87, 0x00,
			0x9F, 0x00, 0xB5, 0x00, 0xC8);
		lcm_dcs_write_seq_static(ctx, 0xB5, 0x00, 0xDA, 0x01,
			0x13, 0x01, 0x40, 0x01, 0x83, 0x01, 0xB7, 0x02,
			0x05, 0x02, 0x43, 0x02, 0x45);
		lcm_dcs_write_seq_static(ctx, 0xB6, 0x02, 0x81, 0x02,
			0xC5, 0x02, 0xF0, 0x03, 0x27, 0x03, 0x4A, 0x03,
			0x78, 0x03, 0x86, 0x03, 0x95);
		lcm_dcs_write_seq_static(ctx, 0xB7, 0x03, 0xA5, 0x03,
			0xB5, 0x03, 0xDC, 0x03, 0xF4, 0x03, 0xFE, 0x03,
			0xFF, 0x00, 0x00);
		lcm_dcs_write_seq_static(ctx, 0xB8, 0x00, 0x00, 0x00,
			0x1D, 0x00, 0x49, 0x00, 0x6A, 0x00, 0x87, 0x00,
			0x9F, 0x00, 0xB5, 0x00, 0xC8);
		lcm_dcs_write_seq_static(ctx, 0xB9, 0x00, 0xDA, 0x01,
			0x13, 0x01, 0x40, 0x01, 0x83, 0x01, 0xB7, 0x02,
			0x05, 0x02, 0x43, 0x02, 0x45);
		lcm_dcs_write_seq_static(ctx, 0xBA, 0x02, 0x81, 0x02,
			0xC5, 0x02, 0xF0, 0x03, 0x27, 0x03, 0x4A, 0x03,
			0x78, 0x03, 0x86, 0x03, 0x95);
		lcm_dcs_write_seq_static(ctx, 0xBB, 0x03, 0xA5, 0x03,
			0xB5, 0x03, 0xDC, 0x03, 0xF4, 0x03, 0xFE, 0x03,
			0xFF, 0x00, 0x00);
		lcm_dcs_write_seq_static(ctx, 0xC6, 0xB8);
		lcm_dcs_write_seq_static(ctx, 0xC7, 0x54);
		lcm_dcs_write_seq_static(ctx, 0xC8, 0x63);
		lcm_dcs_write_seq_static(ctx, 0xC9, 0x42);
		lcm_dcs_write_seq_static(ctx, 0xCA, 0x32);
		lcm_dcs_write_seq_static(ctx, 0xCC, 0x22);
		lcm_dcs_write_seq_static(ctx, 0xCD, 0x64);
		lcm_dcs_write_seq_static(ctx, 0xCE, 0x54);
		lcm_dcs_write_seq_static(ctx, 0xCF, 0xA7);
		lcm_dcs_write_seq_static(ctx, 0xD0, 0xA4);
		lcm_dcs_write_seq_static(ctx, 0xD1, 0xF8);
		lcm_dcs_write_seq_static(ctx, 0xD2, 0xB8);
		lcm_dcs_write_seq_static(ctx, 0xD3, 0x54);
		lcm_dcs_write_seq_static(ctx, 0xD4, 0x63);
		lcm_dcs_write_seq_static(ctx, 0xD5, 0x42);
		lcm_dcs_write_seq_static(ctx, 0xD6, 0x32);
		lcm_dcs_write_seq_static(ctx, 0xD8, 0x22);
		lcm_dcs_write_seq_static(ctx, 0xD9, 0x64);
		lcm_dcs_write_seq_static(ctx, 0xDA, 0x54);
		lcm_dcs_write_seq_static(ctx, 0xDB, 0xA7);
		lcm_dcs_write_seq_static(ctx, 0xDC, 0xA4);
		lcm_dcs_write_seq_static(ctx, 0xDD, 0xF8);
		lcm_dcs_write_seq_static(ctx, 0xDE, 0xB8);
		lcm_dcs_write_seq_static(ctx, 0xDF, 0x54);
		lcm_dcs_write_seq_static(ctx, 0xE0, 0x63);
		lcm_dcs_write_seq_static(ctx, 0xE1, 0x42);
		lcm_dcs_write_seq_static(ctx, 0xE2, 0x32);
		lcm_dcs_write_seq_static(ctx, 0xE4, 0x22);
		lcm_dcs_write_seq_static(ctx, 0xE5, 0x64);
		lcm_dcs_write_seq_static(ctx, 0xE6, 0x54);
		lcm_dcs_write_seq_static(ctx, 0xE7, 0xA7);
		lcm_dcs_write_seq_static(ctx, 0xE8, 0xA4);
		lcm_dcs_write_seq_static(ctx, 0xE9, 0xF8);
		lcm_dcs_write_seq_static(ctx, 0xFF, 0x21);
		lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
		lcm_dcs_write_seq_static(ctx, 0xB0, 0x00, 0x00, 0x00,
			0x1D, 0x00, 0x49, 0x00, 0x6A, 0x00, 0x87, 0x00,
			0x9F, 0x00, 0xB5, 0x00, 0xC8);
		lcm_dcs_write_seq_static(ctx, 0xB1, 0x00, 0xDA, 0x01,
			0x13, 0x01, 0x40, 0x01, 0x83, 0x01, 0xB7, 0x02,
			0x05, 0x02, 0x43, 0x02, 0x45);
		lcm_dcs_write_seq_static(ctx, 0xB2, 0x02, 0x81, 0x02,
			0xC5, 0x02, 0xF0, 0x03, 0x27, 0x03, 0x4A, 0x03,
			0x78, 0x03, 0x86, 0x03, 0x95);
		lcm_dcs_write_seq_static(ctx, 0xB3, 0x03, 0xA5, 0x03,
			0xB5, 0x03, 0xDC, 0x03, 0xF4, 0x03, 0xFE, 0x03,
			0xFF, 0x00, 0x00);
		lcm_dcs_write_seq_static(ctx, 0xB4, 0x00, 0x00, 0x00,
			0x1D, 0x00, 0x49, 0x00, 0x6A, 0x00, 0x87, 0x00,
			0x9F, 0x00, 0xB5, 0x00, 0xC8);
		lcm_dcs_write_seq_static(ctx, 0xB5, 0x00, 0xDA, 0x01,
			0x13, 0x01, 0x40, 0x01, 0x83, 0x01, 0xB7, 0x02,
			0x05, 0x02, 0x43, 0x02, 0x45);
		lcm_dcs_write_seq_static(ctx, 0xB6, 0x02, 0x81, 0x02,
			0xC5, 0x02, 0xF0, 0x03, 0x27, 0x03, 0x4A, 0x03,
			0x78, 0x03, 0x86, 0x03, 0x95);
		lcm_dcs_write_seq_static(ctx, 0xB7, 0x03, 0xA5, 0x03,
			0xB5, 0x03, 0xDC, 0x03, 0xF4, 0x03, 0xFE, 0x03,
			0xFF, 0x00, 0x00);
		lcm_dcs_write_seq_static(ctx, 0xB8, 0x00, 0x00, 0x00,
			0x1D, 0x00, 0x49, 0x00, 0x6A, 0x00, 0x87, 0x00,
			0x9F, 0x00, 0xB5, 0x00, 0xC8);
		lcm_dcs_write_seq_static(ctx, 0xB9, 0x00, 0xDA, 0x01,
			0x13, 0x01, 0x40, 0x01, 0x83, 0x01, 0xB7, 0x02,
			0x05, 0x02, 0x43, 0x02, 0x45);
		lcm_dcs_write_seq_static(ctx, 0xBA, 0x02, 0x81, 0x02,
			0xC5, 0x02, 0xF0, 0x03, 0x27, 0x03, 0x4A, 0x03,
			0x78, 0x03, 0x86, 0x03, 0x95);
		lcm_dcs_write_seq_static(ctx, 0xBB, 0x03, 0xA5, 0x03,
			0xB5, 0x03, 0xDC, 0x03, 0xF4, 0x03, 0xFE, 0x03,
			0xFF, 0x00, 0x00);
		lcm_dcs_write_seq_static(ctx, 0xFF, 0x23);
		lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
		lcm_dcs_write_seq_static(ctx, 0x76, 0x01, 0x02);
		lcm_dcs_write_seq_static(ctx, 0x77, 0x01, 0x02);
		lcm_dcs_write_seq_static(ctx, 0x78, 0x01, 0x02);
		lcm_dcs_write_seq_static(ctx, 0x7A, 0x63, 0x63);
		lcm_dcs_write_seq_static(ctx, 0x7B, 0xB3, 0xB3);
		lcm_dcs_write_seq_static(ctx, 0x7C, 0x2D, 0x32);
		lcm_dcs_write_seq_static(ctx, 0x7E, 0x10, 0x10);
		lcm_dcs_write_seq_static(ctx, 0xFF, 0x24);
		lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
		lcm_dcs_write_seq_static(ctx, 0x00, 0x27, 0x0D, 0x0C,
			0x0F, 0x0e, 0x28, 0x28, 0x26, 0x26, 0x3A, 0x04,
			0x05, 0x23, 0x03, 0x22, 0x29);
		lcm_dcs_write_seq_static(ctx, 0x01, 0x29, 0x08, 0x30,
			0x2E, 0x2C, 0x30, 0x2E, 0x2C);
		lcm_dcs_write_seq_static(ctx, 0x02, 0x27, 0x0D, 0x0C,
			0x0F, 0x0E, 0x28, 0x28, 0x26, 0x26, 0x3A, 0x04,
			0x05, 0x25, 0x03, 0x24, 0x29);
		lcm_dcs_write_seq_static(ctx, 0x03, 0x29, 0x08, 0x30,
			0x2E, 0x2C, 0x30, 0x2E, 0x2C);
		lcm_dcs_write_seq_static(ctx, 0x17, 0x41, 0x25, 0x63,
			0xA7, 0x8B, 0xC9, 0x41, 0x25, 0x63, 0xA7, 0x8B, 0xC9);
		lcm_dcs_write_seq_static(ctx, 0x2F, 0x02);
		lcm_dcs_write_seq_static(ctx, 0x30, 0x01);
		lcm_dcs_write_seq_static(ctx, 0x31, 0x02);
		lcm_dcs_write_seq_static(ctx, 0x32, 0x03);
		lcm_dcs_write_seq_static(ctx, 0x33, 0x04);
		lcm_dcs_write_seq_static(ctx, 0x34, 0x05);
		lcm_dcs_write_seq_static(ctx, 0x35, 0x06);
		lcm_dcs_write_seq_static(ctx, 0x36, 0x07);
		lcm_dcs_write_seq_static(ctx, 0x36, 0x07);
		lcm_dcs_write_seq_static(ctx, 0x37, 0x22);
		lcm_dcs_write_seq_static(ctx, 0x3B, 0xE5);
		lcm_dcs_write_seq_static(ctx, 0x3D, 0x02);
		lcm_dcs_write_seq_static(ctx, 0x3F, 0x0C);
		lcm_dcs_write_seq_static(ctx, 0x47, 0x34);
		lcm_dcs_write_seq_static(ctx, 0x4B, 0x47);
		lcm_dcs_write_seq_static(ctx, 0x4C, 0x01);
		lcm_dcs_write_seq_static(ctx, 0x55, 0x0A, 0x0A);
		lcm_dcs_write_seq_static(ctx, 0x56, 0x04);
		lcm_dcs_write_seq_static(ctx, 0x58, 0x10);
		lcm_dcs_write_seq_static(ctx, 0x59, 0x10);
		lcm_dcs_write_seq_static(ctx, 0x5B, 0xE5);
		lcm_dcs_write_seq_static(ctx, 0x5E, 0x00, 0x04);
		lcm_dcs_write_seq_static(ctx, 0x60, 0x71, 0x70);
		lcm_dcs_write_seq_static(ctx, 0x61, 0x30);
		lcm_dcs_write_seq_static(ctx, 0x72, 0x80);
		lcm_dcs_write_seq_static(ctx, 0x92, 0xF2, 0x01, 0x33);
		lcm_dcs_write_seq_static(ctx, 0x93, 0x1A, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x94, 0x57, 0x00, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x95, 0x09);
		lcm_dcs_write_seq_static(ctx, 0x96, 0x80, 0xBA, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x9A, 0x0B);
		lcm_dcs_write_seq_static(ctx, 0xA5, 0x00);
		lcm_dcs_write_seq_static(ctx, 0xAA, 0x99, 0x99, 0x26);
		lcm_dcs_write_seq_static(ctx, 0xAB, 0x22);
		lcm_dcs_write_seq_static(ctx, 0xC2, 0xC4, 0x00, 0x01);
		lcm_dcs_write_seq_static(ctx, 0xC4, 0x2A);
		lcm_dcs_write_seq_static(ctx, 0xC8, 0x03);
		lcm_dcs_write_seq_static(ctx, 0xC9, 0x08);
		lcm_dcs_write_seq_static(ctx, 0xCA, 0x03);
		lcm_dcs_write_seq_static(ctx, 0xD4, 0x03);
		lcm_dcs_write_seq_static(ctx, 0xD6, 0x46);
		lcm_dcs_write_seq_static(ctx, 0xD7, 0x35);
		lcm_dcs_write_seq_static(ctx, 0xD8, 0x25);
		lcm_dcs_write_seq_static(ctx, 0xDB, 0x04);
		lcm_dcs_write_seq_static(ctx, 0xDD, 0x44);
		lcm_dcs_write_seq_static(ctx, 0xDF, 0x04);
		lcm_dcs_write_seq_static(ctx, 0xE1, 0x04);
		lcm_dcs_write_seq_static(ctx, 0xE3, 0x04);
		lcm_dcs_write_seq_static(ctx, 0xE5, 0x04);
		lcm_dcs_write_seq_static(ctx, 0xE9, 0x04);
		lcm_dcs_write_seq_static(ctx, 0xEB, 0x04);
		lcm_dcs_write_seq_static(ctx, 0xEF, 0x04);
		lcm_dcs_write_seq_static(ctx, 0xF1, 0x23);
		lcm_dcs_write_seq_static(ctx, 0xF2, 0x23);
		lcm_dcs_write_seq_static(ctx, 0xF3, 0x23);
		lcm_dcs_write_seq_static(ctx, 0xF4, 0x22);
		lcm_dcs_write_seq_static(ctx, 0xF5, 0x23);
		lcm_dcs_write_seq_static(ctx, 0xF6, 0x23);
		lcm_dcs_write_seq_static(ctx, 0xF7, 0x34);
		lcm_dcs_write_seq_static(ctx, 0xFF, 0x25);
		lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
		lcm_dcs_write_seq_static(ctx, 0x05, 0x04);
		lcm_dcs_write_seq_static(ctx, 0x13, 0x04);
		lcm_dcs_write_seq_static(ctx, 0x14, 0xCC);
		lcm_dcs_write_seq_static(ctx, 0x20, 0xE5);
		lcm_dcs_write_seq_static(ctx, 0x23, 0x04);
		lcm_dcs_write_seq_static(ctx, 0x24, 0x3F);
		lcm_dcs_write_seq_static(ctx, 0x27, 0xE5);
		lcm_dcs_write_seq_static(ctx, 0x2A, 0x04);
		lcm_dcs_write_seq_static(ctx, 0x2B, 0x3F);
		lcm_dcs_write_seq_static(ctx, 0x34, 0xE5);
		lcm_dcs_write_seq_static(ctx, 0x37, 0x04);
		lcm_dcs_write_seq_static(ctx, 0x38, 0x3F);
		lcm_dcs_write_seq_static(ctx, 0x39, 0x0F);
		lcm_dcs_write_seq_static(ctx, 0x3B, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x3F, 0x20);
		lcm_dcs_write_seq_static(ctx, 0x40, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x42, 0x04);
		lcm_dcs_write_seq_static(ctx, 0x45, 0xE5);
		lcm_dcs_write_seq_static(ctx, 0x47, 0x47);
		lcm_dcs_write_seq_static(ctx, 0x49, 0xE5);
		lcm_dcs_write_seq_static(ctx, 0x4C, 0x04);
		lcm_dcs_write_seq_static(ctx, 0x4D, 0x3F);
		lcm_dcs_write_seq_static(ctx, 0x4E, 0x0F);
		lcm_dcs_write_seq_static(ctx, 0x51, 0xE5);
		lcm_dcs_write_seq_static(ctx, 0x54, 0x04);
		lcm_dcs_write_seq_static(ctx, 0x55, 0x3F);
		lcm_dcs_write_seq_static(ctx, 0x56, 0x0F);
		lcm_dcs_write_seq_static(ctx, 0x5B, 0xA0);
		lcm_dcs_write_seq_static(ctx, 0x5E, 0xE5);
		lcm_dcs_write_seq_static(ctx, 0x60, 0x47);
		lcm_dcs_write_seq_static(ctx, 0x62, 0xE5);
		lcm_dcs_write_seq_static(ctx, 0x65, 0x04);
		lcm_dcs_write_seq_static(ctx, 0x66, 0x3F);
		lcm_dcs_write_seq_static(ctx, 0x67, 0x0F);
		lcm_dcs_write_seq_static(ctx, 0x68, 0x04);
		lcm_dcs_write_seq_static(ctx, 0x6A, 0x01);
		lcm_dcs_write_seq_static(ctx, 0x6B, 0x44);
		lcm_dcs_write_seq_static(ctx, 0x6C, 0x0E);
		lcm_dcs_write_seq_static(ctx, 0x6D, 0x0E);
		lcm_dcs_write_seq_static(ctx, 0x6E, 0x12);
		lcm_dcs_write_seq_static(ctx, 0x6F, 0x12);
		lcm_dcs_write_seq_static(ctx, 0xC6, 0x10);
		lcm_dcs_write_seq_static(ctx, 0xDB, 0x04);
		lcm_dcs_write_seq_static(ctx, 0xDC, 0x44);
		lcm_dcs_write_seq_static(ctx, 0xFF, 0x26);
		lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
		lcm_dcs_write_seq_static(ctx, 0x04, 0x50);
		lcm_dcs_write_seq_static(ctx, 0x0A, 0x02);
		lcm_dcs_write_seq_static(ctx, 0x0C, 0x13);
		lcm_dcs_write_seq_static(ctx, 0x0D, 0x0C);
		lcm_dcs_write_seq_static(ctx, 0x0F, 0x03);
		lcm_dcs_write_seq_static(ctx, 0x13, 0x49);
		lcm_dcs_write_seq_static(ctx, 0x14, 0x5D);
		lcm_dcs_write_seq_static(ctx, 0x16, 0x10);
		lcm_dcs_write_seq_static(ctx, 0x19, 0x0D, 0x12, 0x12, 0x12);
		lcm_dcs_write_seq_static(ctx, 0x1A, 0x58, 0x4E, 0x4E, 0x4E);
		lcm_dcs_write_seq_static(ctx, 0x1B, 0x0C, 0x11, 0x11, 0x11);
		lcm_dcs_write_seq_static(ctx, 0x1C, 0x7C, 0x71, 0x71, 0x71);
		lcm_dcs_write_seq_static(ctx, 0x1E, 0xF2);
		lcm_dcs_write_seq_static(ctx, 0x1F, 0xF2);
		lcm_dcs_write_seq_static(ctx, 0x24, 0x01);
		lcm_dcs_write_seq_static(ctx, 0x25, 0xF2);
		lcm_dcs_write_seq_static(ctx, 0x2A, 0x0D, 0x12, 0x12, 0x12);
		lcm_dcs_write_seq_static(ctx, 0x2B, 0x54, 0x49, 0x49, 0x49);
		lcm_dcs_write_seq_static(ctx, 0x2F, 0x04);
		lcm_dcs_write_seq_static(ctx, 0x30, 0xF2);
		lcm_dcs_write_seq_static(ctx, 0x32, 0xF2);
		lcm_dcs_write_seq_static(ctx, 0x33, 0x89);
		lcm_dcs_write_seq_static(ctx, 0x34, 0x67);
		lcm_dcs_write_seq_static(ctx, 0x35, 0x11);
		lcm_dcs_write_seq_static(ctx, 0x36, 0x11);
		lcm_dcs_write_seq_static(ctx, 0x36, 0x11);
		lcm_dcs_write_seq_static(ctx, 0x37, 0x11);
		lcm_dcs_write_seq_static(ctx, 0x38, 0x01);
		lcm_dcs_write_seq_static(ctx, 0x39, 0x04);
		lcm_dcs_write_seq_static(ctx, 0x3A, 0xF2);
		lcm_dcs_write_seq_static(ctx, 0x97, 0x00, 0x00);
		lcm_dcs_write_seq_static(ctx, 0xA7, 0x01);
		lcm_dcs_write_seq_static(ctx, 0xC9, 0x00);
		lcm_dcs_write_seq_static(ctx, 0xCD, 0x22, 0x21);
		lcm_dcs_write_seq_static(ctx, 0xCE, 0x21, 0x22);
		lcm_dcs_write_seq_static(ctx, 0xCF, 0x01, 0x00, 0x18);
		lcm_dcs_write_seq_static(ctx, 0xD0, 0x00, 0x00);
		lcm_dcs_write_seq_static(ctx, 0xD1, 0x00, 0x00);
		lcm_dcs_write_seq_static(ctx, 0xD2, 0x2D);
		lcm_dcs_write_seq_static(ctx, 0xFF, 0x27);
		lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
		lcm_dcs_write_seq_static(ctx, 0x58, 0x80);
		lcm_dcs_write_seq_static(ctx, 0x59, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x5A, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x5B, 0x45);
		lcm_dcs_write_seq_static(ctx, 0x5C, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x5D, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x5E, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x5F, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x60, 0x00, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x61, 0x00, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x62, 0x00, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x63, 0x00, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x64, 0x00, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x65, 0x00, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x66, 0x00, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x67, 0x00, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x68, 0x00, 0x00);
		lcm_dcs_write_seq_static(ctx, 0xC0, 0x00);
		lcm_dcs_write_seq_static(ctx, 0xC1, 0x00);
		lcm_dcs_write_seq_static(ctx, 0xD1, 0x04);
		lcm_dcs_write_seq_static(ctx, 0xD2, 0x3C);
		lcm_dcs_write_seq_static(ctx, 0xFF, 0x2A);
		lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
		lcm_dcs_write_seq_static(ctx, 0x25, 0x15);
		lcm_dcs_write_seq_static(ctx, 0x27, 0x5F);
		lcm_dcs_write_seq_static(ctx, 0x28, 0x78);
		lcm_dcs_write_seq_static(ctx, 0x30, 0x02);
		lcm_dcs_write_seq_static(ctx, 0x32, 0x52);
		lcm_dcs_write_seq_static(ctx, 0x33, 0x71);
		lcm_dcs_write_seq_static(ctx, 0x35, 0x01);
		lcm_dcs_write_seq_static(ctx, 0x4B, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x4B, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x64, 0x96);
		lcm_dcs_write_seq_static(ctx, 0x67, 0x9E);
		lcm_dcs_write_seq_static(ctx, 0x68, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x69, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x6A, 0x9E);
		lcm_dcs_write_seq_static(ctx, 0x6B, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x6C, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x79, 0x96);
		lcm_dcs_write_seq_static(ctx, 0x7C, 0x96);
		lcm_dcs_write_seq_static(ctx, 0x7F, 0x96);
		lcm_dcs_write_seq_static(ctx, 0x82, 0x96);
		lcm_dcs_write_seq_static(ctx, 0x85, 0x96);
		lcm_dcs_write_seq_static(ctx, 0x88, 0x9E);
		lcm_dcs_write_seq_static(ctx, 0x89, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x8A, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x8B, 0x16);
		lcm_dcs_write_seq_static(ctx, 0x8C, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x8D, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x8E, 0x16);
		lcm_dcs_write_seq_static(ctx, 0x8F, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x90, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x92, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x93, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x94, 0x06);
		lcm_dcs_write_seq_static(ctx, 0x99, 0x91);
		lcm_dcs_write_seq_static(ctx, 0x9A, 0x0A);
		lcm_dcs_write_seq_static(ctx, 0xA2, 0x15);
		lcm_dcs_write_seq_static(ctx, 0xA3, 0x50);
		lcm_dcs_write_seq_static(ctx, 0xA4, 0x15);
		lcm_dcs_write_seq_static(ctx, 0xA5, 0x41);
		lcm_dcs_write_seq_static(ctx, 0xA6, 0x10);
		lcm_dcs_write_seq_static(ctx, 0xA9, 0x21);
		lcm_dcs_write_seq_static(ctx, 0xB3, 0x16, 0x00, 0x00);
		lcm_dcs_write_seq_static(ctx, 0xB7, 0xFF);
		lcm_dcs_write_seq_static(ctx, 0xB8, 0x40);
		lcm_dcs_write_seq_static(ctx, 0xB9, 0x98, 0x00, 0x6F,
			0x00, 0xCB, 0xB7, 0x14, 0x5F, 0x41);
		lcm_dcs_write_seq_static(ctx, 0xBA, 0xAA, 0xAA, 0xA5,
			0xA5, 0xE1, 0xE1, 0xFF, 0xFF, 0xAA);
		lcm_dcs_write_seq_static(ctx, 0xBB, 0xAA, 0xAA, 0xAA,
			0xAA, 0xAA, 0x55, 0xAA, 0x55, 0x38);
		lcm_dcs_write_seq_static(ctx, 0xBC, 0x66, 0x06);
		lcm_dcs_write_seq_static(ctx, 0xC4, 0x12);
		lcm_dcs_write_seq_static(ctx, 0xC5, 0x04);
		lcm_dcs_write_seq_static(ctx, 0xC6, 0x3F);
		lcm_dcs_write_seq_static(ctx, 0xC8, 0x0F);
		lcm_dcs_write_seq_static(ctx, 0xCA, 0x01);
		lcm_dcs_write_seq_static(ctx, 0xCB, 0x10, 0x00, 0x00, 0x00);
		lcm_dcs_write_seq_static(ctx, 0xCC, 0xF5, 0xAA, 0x5F, 0x55, 0x00, 0x00);
		lcm_dcs_write_seq_static(ctx, 0xD0, 0x84);
		lcm_dcs_write_seq_static(ctx, 0xD1, 0x01);
		lcm_dcs_write_seq_static(ctx, 0xF4, 0x91);
		lcm_dcs_write_seq_static(ctx, 0xF5, 0x0A);
		lcm_dcs_write_seq_static(ctx, 0xFF, 0x10);
		lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
		lcm_dcs_write_seq_static(ctx, 0x3B, 0x03, 0x57, 0x1A, 0x0A, 0x0A, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x90, 0x13);
		lcm_dcs_write_seq_static(ctx, 0x91, 0x89, 0x28, 0x00,
			0x14, 0xD2, 0x00, 0x00, 0x00, 0x02, 0xA1, 0x00,
			0x14, 0x05, 0x7A, 0x01, 0xDA);
		lcm_dcs_write_seq_static(ctx, 0x92, 0x10, 0xE0);
		lcm_dcs_write_seq_static(ctx, 0xB2, 0x91);

		lcm_dcs_write_seq_static(ctx, 0xFF, 0x23);
		lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
		lcm_dcs_write_seq_static(ctx, 0x00, 0x60);
		lcm_dcs_write_seq_static(ctx, 0x07, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x08, 0x02);
		lcm_dcs_write_seq_static(ctx, 0x09, 0x00);
		lcm_dcs_write_seq_static(ctx, 0xFF, 0x10);
		lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
		lcm_dcs_write_seq_static(ctx, 0x51, 0x07, 0xFF);
		lcm_dcs_write_seq_static(ctx, 0x53, 0x2c);
		lcm_dcs_write_seq_static(ctx, 0x55, 0x00);
	}
	//bist
	//lcm_dcs_write_seq_static(ctx, 0xFF, 0x25);
	//lcm_dcs_write_seq_static(ctx, 0xEC, 0x01);
	lcm_dcs_write_seq_static(ctx, 0x11);
	msleep(100);
	lcm_dcs_write_seq_static(ctx, 0x29);
	msleep(40);
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

	lcm_dcs_write_seq_static(ctx, 0x28);
	msleep(50);
	lcm_dcs_write_seq_static(ctx, 0x10);
	msleep(120);

	ctx->error = 0;
	ctx->prepared = false;

	msleep(20);

	gpiod_set_value(ctx->reset_gpio, 0);
	msleep(20);

	lcd_set_bias(0);
	lcm_regulator_disable(ctx->lcm_v1_8);

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

#define HFP (120)
#define HSA (10)
#define HBP (286)
#define VFP (26)
#define VSA (2)
#define VBP (85)
#define VAC (1840)
#define HAC (2944)

static const struct drm_display_mode default_mode = {
	.clock = 393725,
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

static int lcm_setbacklight_by_bridge(struct drm_panel *panel, dcs_write_gce cb,
	void *handle, unsigned int level)
{
	int map_level = level * 0x7FF / 1024;

	pr_info("%s: level=%d, map_level=%d\n", __func__, level, map_level);
	lcd_bl_set_led_brightness(map_level);

	return 0;
}

static struct mtk_panel_params ext_params = {
	.pll_clk = 492,
	.ssc_disable = 1,
	.bdg_ssc_disable = 1,
	.cust_esd_check = 0,
	.esd_check_enable = 0,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0a,
		.count = 1,
		.para_list[0] = 0x9c,
	},
	.bdg_lane_swap_en = 0,
	.lane_swap_en = 1,
	.lane_swap[0][MIPITX_PHY_LANE_0] = MIPITX_PHY_LANE_2,
	.lane_swap[0][MIPITX_PHY_LANE_1] = MIPITX_PHY_LANE_0,
	.lane_swap[0][MIPITX_PHY_LANE_2] = MIPITX_PHY_LANE_1,
	.lane_swap[0][MIPITX_PHY_LANE_3] = MIPITX_PHY_LANE_3,
	.lane_swap[0][MIPITX_PHY_LANE_CK] = MIPITX_PHY_LANE_CK,
	.lane_swap[0][MIPITX_PHY_LANE_RX] = MIPITX_PHY_LANE_1,
	.dsc_params = {
		.bdg_dsc_enable = 1,
		.ver = 17,
		.slice_mode = 1,
		.rgb_swap = 0,
		.dsc_cfg = 2,
		.rct_on = 1,
		.bit_per_channel = 8,
		.dsc_line_buf_depth = 9,
		.bp_enable = 1,
		.bit_per_pixel = 128,
		.pic_height = 1840,
		.pic_width = 2944,
		.slice_height = 20,
		.slice_width = 1472,
		.chunk_size = 1472,
		.xmit_delay = 512,
		.dec_delay = 1054,
		.scale_value = 32,
		.increment_interval = 673,
		.decrement_interval = 20,
		.line_bpg_offset = 13,
		.nfl_bpg_offset = 1402,
		.slice_bpg_offset = 474,
		.initial_offset = 6144,
		.final_offset = 4320,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,
		.rc_buf_thresh[0] = 14,
		.rc_buf_thresh[1] = 28,
		.rc_buf_thresh[2] = 42,
		.rc_buf_thresh[3] = 56,
		.rc_buf_thresh[4] = 70,
		.rc_buf_thresh[5] = 84,
		.rc_buf_thresh[6] = 98,
		.rc_buf_thresh[7] = 105,
		.rc_buf_thresh[8] = 112,
		.rc_buf_thresh[9] = 119,
		.rc_buf_thresh[10] = 121,
		.rc_buf_thresh[11] = 123,
		.rc_buf_thresh[12] = 125,
		.rc_buf_thresh[13] = 126,
		.rc_range_parameters[0].range_min_qp = 0,
		.rc_range_parameters[0].range_max_qp = 4,
		.rc_range_parameters[0].range_bpg_offset = 2,
		.rc_range_parameters[1].range_min_qp = 0,
		.rc_range_parameters[1].range_max_qp = 4,
		.rc_range_parameters[1].range_bpg_offset = 0,
		.rc_range_parameters[2].range_min_qp = 1,
		.rc_range_parameters[2].range_max_qp = 5,
		.rc_range_parameters[2].range_bpg_offset = 0,
		.rc_range_parameters[3].range_min_qp = 1,
		.rc_range_parameters[3].range_max_qp = 6,
		.rc_range_parameters[3].range_bpg_offset = -2,
		.rc_range_parameters[4].range_min_qp = 3,
		.rc_range_parameters[4].range_max_qp = 7,
		.rc_range_parameters[4].range_bpg_offset = -4,
		.rc_range_parameters[5].range_min_qp = 3,
		.rc_range_parameters[5].range_max_qp = 7,
		.rc_range_parameters[5].range_bpg_offset = -6,
		.rc_range_parameters[6].range_min_qp = 3,
		.rc_range_parameters[6].range_max_qp = 7,
		.rc_range_parameters[6].range_bpg_offset = -8,
		.rc_range_parameters[7].range_min_qp = 3,
		.rc_range_parameters[7].range_max_qp = 8,
		.rc_range_parameters[7].range_bpg_offset = -8,
		.rc_range_parameters[8].range_min_qp = 3,
		.rc_range_parameters[8].range_max_qp = 9,
		.rc_range_parameters[8].range_bpg_offset = -8,
		.rc_range_parameters[9].range_min_qp = 3,
		.rc_range_parameters[9].range_max_qp = 10,
		.rc_range_parameters[9].range_bpg_offset = -10,
		.rc_range_parameters[10].range_min_qp = 5,
		.rc_range_parameters[10].range_max_qp = 11,
		.rc_range_parameters[10].range_bpg_offset = -10,
		.rc_range_parameters[11].range_min_qp = 5,
		.rc_range_parameters[11].range_max_qp = 12,
		.rc_range_parameters[11].range_bpg_offset = -12,
		.rc_range_parameters[12].range_min_qp = 5,
		.rc_range_parameters[12].range_max_qp = 13,
		.rc_range_parameters[12].range_bpg_offset = -12,
		.rc_range_parameters[13].range_min_qp = 7,
		.rc_range_parameters[13].range_max_qp = 13,
		.rc_range_parameters[13].range_bpg_offset = -12,
		.rc_range_parameters[14].range_min_qp = 13,
		.rc_range_parameters[14].range_max_qp = 15,
		.rc_range_parameters[14].range_bpg_offset = -12
	},
};

static struct mtk_panel_funcs ext_funcs = {
	.reset = panel_ext_reset,
	.set_backlight_bridge = lcm_setbacklight_by_bridge,
	.ata_check = panel_ata_check,
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
	struct device_node *dsi_node, *remote_node = NULL, *endpoint = NULL;

	pr_info("%s +++\n",__func__);

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

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_info(dev, "%s: cannot get reset-gpios %ld\n",
				__func__, PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	gpiod_set_value(ctx->reset_gpio, 1);

	ctx->panel_id1 = devm_gpiod_get_index(dev, "panel_id", 0, GPIOD_IN);
	if (IS_ERR(ctx->panel_id1)) {
		dev_info(dev, "%s: cannot get panel_id1 %ld\n",
				__func__, PTR_ERR(ctx->panel_id1));
		return PTR_ERR(ctx->panel_id1);
	}

	ctx->panel_id2 = devm_gpiod_get_index(dev, "panel_id", 1, GPIOD_IN);
	if (IS_ERR(ctx->panel_id2)) {
		dev_info(dev, "%s: cannot panel_id2 %ld\n",
				__func__, PTR_ERR(ctx->panel_id2));
		return PTR_ERR(ctx->panel_id2);
	}

	if (gpiod_get_value(ctx->panel_id1) && !gpiod_get_value(ctx->panel_id2)) {
		ctx->panel_id = 1;
		pr_info("panel driver for tianma!\n");
	} else {
		ctx->panel_id = 0;
		pr_info("panel driver for boe!\n");
	}

	ctx->lcm_v1_8 = devm_regulator_get(dev, "reg-lcm-v1_8");
	if (IS_ERR(ctx->lcm_v1_8)) {
		dev_info(dev, "%s: cannot get lcm_v1_8 %ld\n", PTR_ERR(ctx->lcm_v1_8));
		return PTR_ERR(ctx->lcm_v1_8);
	}

	ret = lcm_regulator_enable(ctx->lcm_v1_8, 1800000);
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

#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_tch_handle_reg(&ctx->panel);
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
	{ .compatible = "boe,nt36532,6382,vdo", },
	{ }
};

MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.driver = {
		.name = "panel-nt36532-dsi-vdo-boe-tianma-6382",
		.owner = THIS_MODULE,
		.of_match_table = lcm_of_match,
	},
};

module_mipi_dsi_driver(lcm_driver);

MODULE_AUTHOR("Henry Tu <Henry.tu@mediatek.com>");
MODULE_DESCRIPTION("truly nt36532 6382 VDO LCD Panel Driver");

MODULE_LICENSE("GPL v2");
