// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

/*****************************************************************************
 *
 * Filename:
 * ---------
 *     kd070fhfid015_dsi_1200x1920.c
 *
 * Project:
 * --------
 *     ALPS
 *
 * Description:
 * ------------
 *     Source code of LCM driver
 *
 *
 *------------------------------------------------------------------------------
 * Upper this line, this part is controlled by CC/CQ. DO NOT MODIFY!!
 *============================================================================
 ****************************************************************************/
#define LCM_NAME        "kd070"
#define PANEL_DRV_NAME  "kd070fhfid015_dsi_1200x1920"
#define PANEL_PLAT_NAME "kd070-disp"

// ---------------------------------------------------------------------------

#ifdef BUILD_LK
#include <string.h>
#include <mt_gpio.h>
#include <platform/mt_pmic.h>
#include <platform/mt_gpio.h>
#include <platform/mt_i2c.h>
//#include <cust_gpio_usage.h>
#include <cust_leds.h>
#include <cust_display.h>
//#include <platform/primary_display.h>
#include <boot_mode.h>
#elif defined(BUILD_UBOOT)
#include <asm/arch/mt_gpio.h>
#error "BUILD_UBOOT not implemented yet!"
#else
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/wait.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/input.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/module.h>
#endif

//#define ST7701S_ESD_CHECK

// ---------------------------------------------------------------------------

#include "ddp_hal.h"
#ifdef BUILD_LK
#include "ddp_dsi.h"
#endif
#include "lcm_drv.h"

// ---------------------------------------------------------------------------

#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#endif

#ifndef DIV_ROUND_CLOSEST
#define DIV_ROUND_CLOSEST(n, d) (((n) + ((d) >> 1)) / (d))
#endif

// ---------------------------------------------------------------------------
//  Local Constants
// ---------------------------------------------------------------------------

#define FRAME_WIDTH  (1200)
#define FRAME_HEIGHT (1920)
#define LCM_PHYSICAL_WIDTH (94500)
#define LCM_PHYSICAL_HEIGHT (151200)

/**
 * density = sqrt(width * width + height * height) / diagonal;
 * diagonal: unit inch
 * LCM_DENSITY = sqrt(1200*1200+1920*1920)/7 = 323;
 */
#define LCM_DENSITY				(320)

// ---------------------------------------------------------------------------
//  Local Variables
// ---------------------------------------------------------------------------
#ifdef GPIO_LCM_RST
#define GPIO_LCD_RST          (45 | 0x80000000)
#else
#define GPIO_LCD_RST          (45 | 0x80000000)
#endif

#ifdef GPIO_LCM_BL_EN
#define GPIO_LCD_BL_EN      (44 | 0x80000000)
#else
#define GPIO_LCD_BL_EN      (44 | 0x80000000)
#endif

#ifdef GPIO_LCM_PWR_EN
#define GPIO_LCD_PWR_EN      (153 | 0x80000000)
#else
#define GPIO_LCD_PWR_EN      (153 | 0x80000000)
#endif

#ifdef BUILD_LK
extern void DSI_clk_HS_mode(DISP_MODULE_ENUM module, void *cmdq, bool enter);
#else
extern void DSI_clk_HS_mode(enum DISP_MODULE_ENUM module, void *cmdq, bool enter);
#endif

#ifdef BUILD_LK
#define SET_RESET_PIN(v)	lcm_set_gpio_output(GPIO_LCD_RST, v) //(lcm_util.set_reset_pin((v)))
#else
#define SET_RESET_PIN(v)	gpio_direction_output(gpio_rst, v)
#endif

#ifdef BUILD_LK
#define SET_DCDC_PIN(v)	lcm_set_gpio_output(GPIO_LCD_PWR_EN, v)
#else
#define SET_DCDC_PIN(v)	gpio_direction_output(gpio_dcdc, v)
#endif

#ifdef BUILD_LK
#define SET_BL_PIN(v)	lcm_set_gpio_output(GPIO_LCD_BL_EN, v)
#else
#define SET_BL_PIN(v)	gpio_direction_output(gpio_bl, v)
#endif

#define REGFLAG_DELAY		0xFFFC
#define REGFLAG_UDELAY	0xFFFB
#define REGFLAG_END_OF_TABLE	0xFFFD

#define GPIO_OUT_ONE  1
#define GPIO_OUT_ZERO 0

struct LCM_setting_table {
	unsigned int cmd;
	unsigned char count;
	unsigned char para_list[64];
};

#ifdef BUILD_LK
/* debug levels: 0-2 */
#define LCM_LOG(string, args...)   printf(string, ##args)
#define LCM_LOGE(string, args...)  dprintf(0, "[LCM/"LCM_NAME"]ERR: "string, ##args)
#define LCM_LOGI(string, args...)  dprintf(0, "[LCM/"LCM_NAME"] "string, ##args)
#define LCM_LOGD(string, args...)  dprintf(0, "[LCM/"LCM_NAME"] "string, ##args)
#define LCM_LOGV(string, args...)  dprintf(3, "[LCM/"LCM_NAME"] "string, ##args)
#else
#define LCM_LOG(fmt, args...)   pr_info(fmt, ##args)
#define LCM_LOGE(fmt, args...)  pr_info("[LCM/"LCM_NAME"]ERR: "fmt, ##args)
#define LCM_LOGI(fmt, args...)  pr_info("[LCM/"LCM_NAME"] "fmt, ##args)
#define LCM_LOGD(fmt, args...)  pr_info("[LCM/"LCM_NAME"] "fmt, ##args)
#define LCM_LOGV(fmt, args...)  pr_debug("[LCM/"LCM_NAME"] "fmt, ##args)

#define LCM_DEV_LOGE(dev, fmt, args...)  dev_info(dev, "[LCM]ERR: "fmt, ##args)
#define LCM_DEV_LOGI(dev, fmt, args...)  dev_info(dev, "[LCM] "fmt, ##args)
#define LCM_DEV_LOGD(dev, fmt, args...)  dev_dbg(dev, "[LCM] "fmt, ##args)
#define LCM_DEV_LOGV(dev, fmt, args...)  dev_dbg(dev, "[LCM] "fmt, ##args)
#endif

// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
//  Local Functions
// ---------------------------------------------------------------------------
static struct LCM_UTIL_FUNCS lcm_util;

#define MDELAY(n)		(lcm_util.mdelay(n))
#define UDELAY(n)		(lcm_util.udelay(n))

#define dsi_set_cmdq_V22(cmdq, cmd, count, ppara, force_update)                \
	lcm_util.dsi_set_cmdq_V22(cmdq, cmd, count, ppara, force_update)
#define dsi_set_cmdq_V2(cmd, count, ppara, force_update) \
		 (lcm_util.dsi_set_cmdq_V2(cmd, count, ppara, force_update))
#define dsi_set_cmdq(pdata, queue_size, force_update) \
		 (lcm_util.dsi_set_cmdq(pdata, queue_size, force_update))
#define wrtie_cmd(cmd) \
		 (lcm_util.dsi_write_cmd(cmd))
#define write_regs(addr, pdata, byte_nums) \
		 (lcm_util.dsi_write_regs(addr, pdata, byte_nums))
#define read_reg \
		 (lcm_util.dsi_read_reg())
#define read_reg_v2(cmd, buffer, buffer_size) \
		 (lcm_util.dsi_dcs_read_lcm_reg_v2(cmd, buffer, buffer_size))

#ifdef BUILD_LK
#define INVALID_GPIO	0xFFFFFFFF
static void lcm_set_gpio_output(unsigned int gpio, unsigned int output)
{
	if (gpio != INVALID_GPIO) {
		LCM_LOGI("GPIO %u: output %u\n", gpio & 0x0ff, output);

		mt_set_gpio_mode(gpio, GPIO_MODE_00);
		mt_set_gpio_dir(gpio, GPIO_DIR_OUT);
		mt_set_gpio_out(gpio, (output > 0) ? GPIO_OUT_ONE : GPIO_OUT_ZERO);
	}
}

#endif

#ifdef BUILD_LK
#else /* !BUILD_LK */
/******************************************************************************/
static int gpio_rst;
static int gpio_dcdc;
static int gpio_bl;

/* Refer to mtkfb.c _parse_tag_videolfb() */
struct tag_videolfb {
	u64 fb_base;
	u32 islcmfound;
	u32 fps;
	u32 vram;
	char lcmname[1];	/* this is the minimum size */
};

static bool panel_parse_tag_videolfb(void)
{
	bool found = false, parsed = false;
	struct tag_videolfb *videolfb_tag = NULL;
	struct device_node *chosen_node;

	LCM_LOGV("%s() enter\n", __func__);

	if (parsed)
		goto _end;

	chosen_node = of_find_node_by_path("/chosen");
	if (!chosen_node)
		chosen_node = of_find_node_by_path("/chosen@0");

	if (chosen_node) {
		videolfb_tag = (struct tag_videolfb *)of_get_property(chosen_node,
			"atag,videolfb", NULL);
		if (videolfb_tag) {
			LCM_LOGD("videolfb_tag->lcmname=%s\n", videolfb_tag->lcmname);
			LCM_LOGD("videolfb_tag->islcmfound=0x%x\n", videolfb_tag->islcmfound);

			if (videolfb_tag->lcmname[0] != '\0' &&
				strcmp(PANEL_DRV_NAME, videolfb_tag->lcmname) == 0)
				found = true;
			else
				LCM_LOGI("'%s' not found (%s)\n",
					PANEL_DRV_NAME, videolfb_tag->lcmname);

			parsed = true;
		}
	}

_end:
	LCM_LOGV("%s() exit: %d\n", __func__, found);
	return found;
}

static struct regulator *lcm_vdd;

static void lcm_get_regulators(struct device *dev)
{
	int ret = 0;

	LCM_LOGD("%s() enter\n", __func__);

	lcm_vdd = devm_regulator_get_optional(dev, "lcm-1v8");
	if (IS_ERR(lcm_vdd)) {
		LCM_LOGE("no 'lcm-1v8' regulator found: %ld\n", PTR_ERR(lcm_vdd));
		lcm_vdd = NULL;
	} else {
		ret = regulator_enable(lcm_vdd);
		if (ret)
			LCM_LOGE("failed to enable lcm-1v8 power: %d\n", ret);
		else
			LCM_LOGD("lcm-1v8 power enabled\n");
	}

	LCM_LOGV("%s() exit\n", __func__);
}

static int lcm_driver_probe(struct device *dev)
{
	int ret = 0;

	LCM_LOGD("%s() enter\n", __func__);
	lcm_get_regulators(dev);

	gpio_rst = of_get_named_gpio(dev->of_node, "gpio_lcd_rst", 0);

	ret = devm_gpio_request(dev, gpio_rst, "kd070-rst");
	if (ret) {
		LCM_LOGE("request gpio %d failed: %d\n", gpio_rst, ret);
		return -1;
	}

	gpio_dcdc = of_get_named_gpio(dev->of_node, "gpio_lcd_dcdc", 0);

	ret = devm_gpio_request(dev, gpio_dcdc, "kd070-dcdc");
	if (ret) {
		LCM_LOGE("request gpio %d failed: %d\n", gpio_dcdc, ret);
		return -1;
	}

	gpio_bl = of_get_named_gpio(dev->of_node, "gpio_lcd_bl", 0);

	ret = devm_gpio_request(dev, gpio_bl, "kd070-dcdc");
	if (ret) {
		LCM_LOGE("request gpio %d failed: %d\n", gpio_bl, ret);
		return -1;
	}
	LCM_LOGV("%s() exit\n", __func__);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id lcm_of_ids[] = {
	{ .compatible = PANEL_PLAT_NAME, },
	{}
};

MODULE_DEVICE_TABLE(of, lcm_of_ids);
#endif

static int lcm_platform_probe(struct platform_device *pdev)
{
	const struct of_device_id *id;

	LCM_LOGD("%s() enter\n", __func__);

	id = of_match_node(lcm_of_ids, pdev->dev.of_node);
	if (!id)
		return -ENODEV;

	return lcm_driver_probe(&pdev->dev);
}

static void lcm_shutdown(struct platform_device *pdev)
{
	LCM_LOGD("%s() enter\n", __func__);

	//is_shutdown = true;

	LCM_LOGV("%s() exit\n", __func__);
}

static struct platform_driver lcm_driver = {
	.driver = {
		.name   = PANEL_DRV_NAME,
		.owner  = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = lcm_of_ids,
#endif
	},
	.probe      = lcm_platform_probe,
	.shutdown   = lcm_shutdown,
};

static int __init panel_init(void)
{
	LCM_LOGD("LCM: Register panel driver for %s\n", PANEL_DRV_NAME);

	if (!panel_parse_tag_videolfb()) {
		LCM_LOGD("%s: panel not found\n", __func__, PANEL_DRV_NAME);
		return -ENODEV;
	}

	if (platform_driver_register(&lcm_driver)) {
		LCM_LOGE("LCM: failed to register driver %s!\n", PANEL_DRV_NAME);
		return -ENODEV;
	}

	return 0;
}

static void __exit panel_exit(void)
{
	platform_driver_unregister(&lcm_driver);
	LCM_LOGD("LCM: Unregister driver %s done\n", PANEL_DRV_NAME);
}

module_init(panel_init);
module_exit(panel_exit);
MODULE_AUTHOR("mediatek");
MODULE_DESCRIPTION("LCM display subsystem driver");
MODULE_LICENSE("GPL");
#endif /* BUILD_LK */

static struct LCM_setting_table bl_level[] = {{0x51, 1, {0xFF} },
					{REGFLAG_END_OF_TABLE, 0x00, {} }
};

static void push_table(struct LCM_setting_table *table, unsigned int count
						, unsigned char force_update)
{
	unsigned int i;
	unsigned int cmd;

	LCM_LOGD("%s() enter\n", __func__);

	for (i = 0; i < count; i++) {
		cmd = table[i].cmd;
		switch (cmd) {
		case REGFLAG_DELAY:
			MDELAY(table[i].count);
			break;
		case REGFLAG_UDELAY:
			UDELAY(table[i].count);
			break;
		case REGFLAG_END_OF_TABLE:
			goto end;
		default:
			dsi_set_cmdq_V2(cmd, table[i].count, table[i].para_list, force_update);
			//MDELAY(1);
			break;
		}
	}

end:
	LCM_LOGV("%s() exit\n", __func__);
}

// ---------------------------------------------------------------------------
//  LCM Driver Implementations
// ---------------------------------------------------------------------------
static void lcm_set_util_funcs(const struct LCM_UTIL_FUNCS *util)
{
	memcpy(&lcm_util, util, sizeof(struct LCM_UTIL_FUNCS));
}

static void lcm_get_params(struct LCM_PARAMS *params)
{
	LCM_LOGD("%s() enter\n", __func__);

	memset(params, 0, sizeof(struct LCM_PARAMS));

#ifdef BUILD_LK
	params->dsi.edp_panel = 1;
#endif

	params->type                        = LCM_TYPE_DSI;
	params->lcm_if                      = LCM_INTERFACE_DSI0;
	params->lcm_cmd_if                  = LCM_INTERFACE_DSI0;

	params->width                       = FRAME_WIDTH;
	params->height                      = FRAME_HEIGHT;

	params->physical_width              = LCM_PHYSICAL_WIDTH / 1000;
	params->physical_height             = LCM_PHYSICAL_HEIGHT / 1000;
#ifndef BUILD_LK
	params->physical_width_um           = LCM_PHYSICAL_WIDTH;
	params->physical_height_um          = LCM_PHYSICAL_HEIGHT;
	params->density                     = LCM_DENSITY;
#endif

	params->dsi.mode                    = SYNC_EVENT_VDO_MODE;
	params->dsi.LANE_NUM                = LCM_FOUR_LANE;

	/* The following defined the format for data coming from LCD engine. */
	params->dsi.data_format.color_order = LCM_COLOR_ORDER_RGB;
	params->dsi.data_format.trans_seq   = LCM_DSI_TRANS_SEQ_MSB_FIRST;
	params->dsi.data_format.padding     = LCM_DSI_PADDING_ON_LSB;
	params->dsi.data_format.format      = LCM_DSI_FORMAT_RGB888;

	// Highly depends on LCD driver capability.
	params->dsi.packet_size = 256;

	/* Video mode setting */
	params->dsi.intermediat_buffer_num  = 0;

	params->dsi.word_count              = FRAME_WIDTH * 3;
	params->dsi.PS                      = LCM_PACKED_PS_24BIT_RGB888;

	params->dsi.vertical_sync_active				= 4; //10;
	params->dsi.vertical_backporch					= 4; //20;
	params->dsi.vertical_frontporch					= 8; //20;
	params->dsi.vertical_active_line				= FRAME_HEIGHT;

	params->dsi.horizontal_sync_active				= 16; //20;
	params->dsi.horizontal_backporch				= 45; //72;
	params->dsi.horizontal_frontporch				= 144; //176;
	params->dsi.horizontal_active_pixel				= FRAME_WIDTH;

	// Bit rate calculation
	//params->dsi.PLL_CLOCK = LCM_DSI_6589_PLL_CLOCK_442; //260;
	//bit rate 60 fps-->996.328Mhz  494  416 450 ok;  474.5 fail
	//1 Every lane speed
	params->dsi.data_rate               = 1040;//1000;//960;//600; /* Mbps *//**/
	params->dsi.PLL_CLOCK               = params->dsi.data_rate >> 1; /* MHz */

	/* Workaround for MIPI DSI test: THS-PREPARE is too low */
	/* Refer to DSI_PHY_TIMCONFIG() in ddp_dsi.c */
{
	static unsigned char THS_PREPARE; /* THS_PREPARE */
	static unsigned char THS_PREPARE_and_ZERO; /* THS_PREPARE + THS_ZERO */

	if (THS_PREPARE == 0 || THS_PREPARE_and_ZERO == 0) {
		u32 cycle_time = 8000 / params->dsi.data_rate + 0x01;
		u32 ui = 1000 / params->dsi.data_rate + 0x01;

#define DSI_NS_TO_CYCLE(n, c)   ((n) / (c))

		/* Refer to the default calculation in DSI_PHY_TIMCONFIG() */
		THS_PREPARE = DSI_NS_TO_CYCLE((0x40 + 0x5 * ui), cycle_time);
		if (THS_PREPARE < 1)
			THS_PREPARE = 1;

		/* Increase 2 cycle for Data THS-PREPARE */
		THS_PREPARE += 2;

		/* THS_PREPARE + THS_ZERO */
		/* Refer to the default calculation in DSI_PHY_TIMCONFIG() */
		THS_PREPARE_and_ZERO = DSI_NS_TO_CYCLE((0xC8 + 0x0a * ui), cycle_time);

		LCM_LOGI("%s: THS_PREPARE=%d THS_PREPARE_and_ZERO=%d\n", __func__,
			 THS_PREPARE, THS_PREPARE_and_ZERO);

#undef DSI_NS_TO_CYCLE
	}

	params->dsi.HS_PRPR = THS_PREPARE;
	params->dsi.HS_ZERO = THS_PREPARE_and_ZERO;
}

	params->dsi.ssc_disable             = 1;
	params->dsi.pll_select              = 0; /* 0: MIPI_PLL; 1: LVDS_PLL */
	params->dsi.cont_clock              = 0;
	params->dsi.null_packet_en          = 0;

	LCM_LOGV("%s() exit\n", __func__);
}

#ifdef BUILD_LK
bool lk_power_initd;
#endif

static void lcm_init_power(void)
{
	LCM_LOGD("%s() enter\n", __func__);

#ifdef BUILD_LK
	bool lk_power_initd = false;

	if (lk_power_initd == false) {
		/* It's unnecessary to run twice in LK */
		lk_power_initd = true;

		/*Iinit lcm power */
		// VIOVCC(1.8V) is depended on VDD(3.3V) , can not control
		lcm_set_gpio_output(GPIO_LCD_RST, GPIO_OUT_ZERO); //RST (H)->(L)GPIO20
		lcm_set_gpio_output(GPIO_LCD_PWR_EN, GPIO_OUT_ZERO); //DCDC_EN (H)->(L)GPIO129
		lcm_set_gpio_output(GPIO_LCD_BL_EN, GPIO_OUT_ZERO); //LED (L)->(H)
		MDELAY(20);

		lcm_set_gpio_output(GPIO_LCD_PWR_EN, GPIO_OUT_ONE); //DCDC_EN (L)->(H)GPIO129
		MDELAY(10);
		lcm_set_gpio_output(GPIO_LCD_BL_EN, GPIO_OUT_ONE); //LED (L)->(H)
		MDELAY(20);
		lcm_set_gpio_output(GPIO_LCD_RST, GPIO_OUT_ONE); //RST (L)->(H)GPIO20
		MDELAY(10);
		lcm_set_gpio_output(GPIO_LCD_RST, GPIO_OUT_ZERO); //RST (H)->(L)GPIO20
		MDELAY(20);
		lcm_set_gpio_output(GPIO_LCD_RST, GPIO_OUT_ONE); //RST (L)->(H)GPIO20

#if defined(GPIO_DSI_SWITCH) && defined(GPIO_DSI_SWITCH_DSI_OUTPUT)
		/* DSI Switch 1 */
		lcm_set_gpio_output(GPIO_DSI_SWITCH, GPIO_DSI_SWITCH_DSI_OUTPUT);
#endif
#if defined(GPIO_DSI_SWITCH2) && defined(GPIO_DSI_SWITCH2_DSI_OUTPUT)
		/* DSI Switch 2 */
		lcm_set_gpio_output(GPIO_DSI_SWITCH2, GPIO_DSI_SWITCH2_DSI_OUTPUT);
#endif

		MDELAY(120);
	}
#endif

	LCM_LOGV("%s() exit\n", __func__);
}

#ifdef BUILD_LK
u32 kd070fhfid015_get_android_lcd_density(void)
{
	return LCM_DENSITY;
}
#endif

static void init_lcm_registers(void)
{
	unsigned int data_array[16];

	data_array[0] = 0x00010500;  //soft reset
	dsi_set_cmdq(data_array, 1, 1);
	MDELAY(5);

	data_array[0] = 0x00110500;  //exit sleep mode
	dsi_set_cmdq(data_array, 1, 1);
	MDELAY(120);

	data_array[0] = 0x04B02300;  //MCAP
	dsi_set_cmdq(data_array, 1, 1);

	data_array[0] = 0x00062902;  //interface setting
	data_array[1] = 0x000814B3;  //5 paras  04-->14 //cmd-->video mode
	data_array[2] = 0x00000022;
	dsi_set_cmdq(data_array, 3, 1);

	data_array[0] = 0x00022902;  //interface ID setting
	data_array[1] = 0x00000CB4;
	dsi_set_cmdq(data_array, 2, 1);

	data_array[0] = 0x00032902;  //DSI control
	data_array[1] = 0x00D33AB6;  //D3
	dsi_set_cmdq(data_array, 2, 1);

	data_array[0] = 0x2C531500;  //control display
	dsi_set_cmdq(data_array, 1, 1);

	data_array[0] = 0x773A1500;  //set pixel format
	dsi_set_cmdq(data_array, 1, 1);

	data_array[0] = 0x00290500;  //set display on
	dsi_set_cmdq(data_array, 1, 1);
	MDELAY(20);
}

#ifdef BUILD_LK
static void lcm_power_disable(void)
{
	LCM_LOGD("%s() enter\n", __func__);

	lk_power_initd = false;

	LCM_LOGV("%s() exit\n", __func__);
}
#endif

static void lcm_init(void)
{
	LCM_LOGD("%s() enter\n", __func__);

	init_lcm_registers();

	LCM_LOGV("%s() exit\n", __func__);
}

static void lcm_suspend_power(void)
{
	LCM_LOGD("%s() enter\n", __func__);

	SET_RESET_PIN(0);
	MDELAY(5);
	SET_BL_PIN(0);
	MDELAY(5);
	regulator_disable(lcm_vdd);
	MDELAY(20);
	SET_DCDC_PIN(0);

	LCM_LOGV("%s() exit\n", __func__);
}

static void lcm_suspend(void)
{
	unsigned int data_array[16];

	LCM_LOGD("%s() enter\n", __func__);

	data_array[0] = 0x00280500;
	dsi_set_cmdq(data_array, 1, 1);
	MDELAY(20);

	data_array[0] = 0x00100500;
	dsi_set_cmdq(data_array, 1, 1);

	LCM_LOGV("%s() exit\n", __func__);
}

static void lcm_resume_power(void)
{
	LCM_LOGD("%s() enter\n", __func__);

	SET_DCDC_PIN(1);
	MDELAY(5);
	regulator_enable(lcm_vdd);
	MDELAY(5);
	SET_BL_PIN(1);
	MDELAY(20);

	SET_RESET_PIN(1);
	MDELAY(5);
	SET_RESET_PIN(0);
	MDELAY(10);
	SET_RESET_PIN(1);
	MDELAY(50);

	LCM_LOGV("%s() exit\n", __func__);
}

static void lcm_resume(void)
{
	LCM_LOGD("%s() enter\n", __func__);

	lcm_init();

	LCM_LOGV("%s() exit\n", __func__);
}

static void lcm_set_backlight(unsigned int level)
{
	LCM_LOGD("%s() enter level = %d\n", __func__, level);
	bl_level[0].para_list[0] = level;
	push_table(bl_level, ARRAY_SIZE(bl_level), 1);
}

static void push_table_cmdq(void *cmdq, struct LCM_setting_table *table,
			    unsigned int count, unsigned char force_update)
{
	unsigned int i;
	unsigned int cmd;

	for (i = 0; i < count; i++) {
		cmd = table[i].cmd;
		switch (cmd) {
		case REGFLAG_DELAY:
			if (table[i].count <= 10)
				MDELAY(table[i].count);
			else
				MDELAY(table[i].count);
			break;
		case REGFLAG_UDELAY:
			UDELAY(table[i].count);
			break;
		case REGFLAG_END_OF_TABLE:
			break;
		default:
			dsi_set_cmdq_V22(cmdq, cmd, table[i].count,
					 table[i].para_list, force_update);
			break;
		}
	}
}

static void lcm_setbacklight_cmdq(void *handle, unsigned int level)
{
	LCM_LOGD("%s() enter level=%d\n", __func__, level);
	bl_level[0].para_list[0] = level;
	push_table_cmdq(handle, bl_level, ARRAY_SIZE(bl_level), 1);
}

struct LCM_DRIVER kd070fhfid015_dsi_1200x1920_lcm_drv = {
	.name           = PANEL_DRV_NAME,
	.set_util_funcs = lcm_set_util_funcs,
	.get_params     = lcm_get_params,
	.init           = lcm_init,
	.init_power     = lcm_init_power,
	.suspend        = lcm_suspend,
	.suspend_power  = lcm_suspend_power,
	.resume         = lcm_resume,
	.resume_power   = lcm_resume_power,
	//.compare_id     = lcm_compare_id,
	.set_backlight      = lcm_set_backlight,
	.set_backlight_cmdq = lcm_setbacklight_cmdq,
};
