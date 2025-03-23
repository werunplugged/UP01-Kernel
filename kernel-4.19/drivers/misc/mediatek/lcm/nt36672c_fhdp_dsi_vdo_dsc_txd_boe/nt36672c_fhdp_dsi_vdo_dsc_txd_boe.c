// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#define LOG_TAG "LCM_DRV"

#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include "disp_dts_gpio.h"
#include <linux/i2c.h> /*lcm power is provided by i2c*/
#include "lcm_drv.h"

#define LCM_LOGI(fmt, args...)      pr_notice("[KERNEL/"LOG_TAG"]"fmt, ##args)
#define LCM_LOGD(fmt, args...)      pr_debug("[KERNEL/"LOG_TAG"]"fmt, ##args)
#define LCM_LOGE(fmt, args...)      pr_err("[KERNEL/"LOG_TAG"]"fmt, ##args)

// ---------------------------------------------------------------------------
//  Extern Variable
// ---------------------------------------------------------------------------
//#if defined(TRAN_CTP_GESTURE_FUN)
//extern int gesture_fun_enable;
//#endif

//new ata check
static int get_lcm_status = -1;

// ---------------------------------------------------------------------------
//  Extern Functions
// --------------------------------------------------------------------------

// ---------------------------------------------------------------------------
//  Local Constants
// ---------------------------------------------------------------------------
#define LCM_DSI_CMD_MODE                0
#define FRAME_WIDTH                     (1080)
#define FRAME_HEIGHT                    (2460)
#define PHYSICAL_WIDTH                  (70956)
#define PHYSICAL_HEIGHT                 (161622)
#define REGFLAG_PORT_SWAP               0xFFFA
#define REGFLAG_DELAY                   0xFFFC
#define REGFLAG_UDELAY                  0xFFFB
#define REGFLAG_END_OF_TABLE            0xFFFD

#define DSC_ENABLE  //enable dsc function

#ifndef GPIO_LCM_RST
	#define GPIO_LCM_RST                (GPIO45 | 0x80000000)
#endif
#ifndef GPIO_LCD_BIAS_ENP_PIN
	#define GPIO_LCD_BIAS_ENP_PIN       (GPIO153 | 0x80000000)
#endif
#ifndef GPIO_LCD_BIAS_ENN_PIN
	#define GPIO_LCD_BIAS_ENN_PIN       (GPIO152 | 0x80000000)
#endif

// ---------------------------------------------------------------------------
//  LCM power is provided by I2C
// ---------------------------------------------------------------------------
/* Define -------------------------------------------------------------------*/
#define I2C_I2C_LCD_BIAS_CHANNEL 6  //for I2C channel 6
#define DCDC_I2C_BUSNUM  I2C_I2C_LCD_BIAS_CHANNEL  //for I2C channel 6
#define DCDC_I2C_ID_NAME "aw37501"
#define DCDC_I2C_ADDR 0x3E

struct AW37501_SETTING_TABLE {
	unsigned char cmd;
	unsigned char data;
};

static struct AW37501_SETTING_TABLE aw37501_cmd_data[5] = {
	{ 0x00, 0x12 }, //AVDD 5.8
	{ 0x01, 0x12 }, //AVEE 5.8
	{ 0x03, 0x43 }, //Applications configure register,default:0x43
	{ 0x04, 0x01 }, //Control State configure register,default:0x01
	{ 0x21, 0x00 }  //Written protect functional register,default:0x00
};

/* Variable -----------------------------------------------------------------*/
#if defined(CONFIG_MTK_LEGACY)
static struct i2c_board_info __initdata aw37501_board_info = {I2C_BOARD_INFO(DCDC_I2C_ID_NAME, DCDC_I2C_ADDR)};
#else
static const struct of_device_id lcm_of_match[] = {
	{.compatible = "mediatek,I2C_LCD_BIAS"},
	{},
};
#endif

static struct i2c_client *lcm_bias_common_i2c_client = NULL;

/* Functions Prototype --------------------------------------------------------*/
static int aw37501_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int aw37501_remove(struct i2c_client *client);

/* Data Structure -------------------------------------------------------------*/
struct aw37501_dev {
	struct i2c_client *client;
};

static const struct i2c_device_id aw37501_id[] = {
	{ DCDC_I2C_ID_NAME, 0 },
	{ }
};

/* I2C Driver  ----------------------------------------------------------------*/
static struct i2c_driver aw37501_iic_driver = {
	.id_table = aw37501_id,
	.probe    = aw37501_probe,
	.remove   = aw37501_remove,
	.driver   = {
		.owner          = THIS_MODULE,
		.name           = "aw37501",
#if !defined(CONFIG_MTK_LEGACY)
		.of_match_table = lcm_of_match,
#endif
	},
};

/* Functions ------------------------------------------------------------------*/
static int aw37501_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	lcm_bias_common_i2c_client = client;
	return 0;
}

static int aw37501_remove(struct i2c_client *client)
{
	lcm_bias_common_i2c_client = NULL;
	i2c_unregister_device(client);
	return 0;
}

static int aw37501_i2c_write_byte(unsigned char addr, unsigned char value)
{
	int ret = 0;
	struct i2c_client *client = lcm_bias_common_i2c_client;
	char write_data[2] = {0};
	if(client == NULL)
	{
		LCM_LOGE("ERROR!! lcm_bias_common_i2c_client is null\n");
		return 0;
	}
	write_data[0] = addr;
	write_data[1] = value;
	ret=i2c_master_send(client, write_data, 2);
	if(ret<0)
		LCM_LOGD("aw37501 write data fail !!\n");
	return ret ;
}

static int __init aw37501_iic_init(void)
{
#if defined(CONFIG_MTK_LEGACY)
	i2c_register_board_info(DCDC_I2C_BUSNUM, &aw37501_board_info, 1);
#endif
	i2c_add_driver(&aw37501_iic_driver);
	return 0;
}

static void __exit aw37501_iic_exit(void)
{
	i2c_del_driver(&aw37501_iic_driver);
}

module_init(aw37501_iic_init);
module_exit(aw37501_iic_exit);
MODULE_DESCRIPTION("AW37501 I2C Driver");
MODULE_LICENSE("GPL");

// ---------------------------------------------------------------------------
//  Local Variable
// ---------------------------------------------------------------------------
static struct LCM_UTIL_FUNCS lcm_util;
//0:lcm_power state is poweron, 1:lcm_power state is poweroff
static unsigned int is_lcm_poweroff;

// ---------------------------------------------------------------------------
//  Local function
// ---------------------------------------------------------------------------
#define MDELAY(n)                       (lcm_util.mdelay(n))
#define UDELAY(n)                       (lcm_util.udelay(n))
#ifdef BUILD_LK
	#define SET_RESET_PIN(v)            (mt_set_gpio_out(GPIO_LCM_RST,(v)))
#endif

#ifndef BUILD_LK
#define dsi_set_cmdq_V22(cmdq, cmd, count, ppara, force_update) \
		lcm_util.dsi_set_cmdq_V22(cmdq, cmd, count, ppara, force_update)
#endif
#define dsi_set_cmdq_V2(cmd, count, ppara, force_update) \
		lcm_util.dsi_set_cmdq_V2(cmd, count, ppara, force_update)
#define dsi_set_cmdq(pdata, queue_size, force_update) \
		lcm_util.dsi_set_cmdq(pdata, queue_size, force_update)
#define wrtie_cmd(cmd) \
		lcm_util.dsi_write_cmd(cmd)
#define write_regs(addr, pdata, byte_nums) \
		lcm_util.dsi_write_regs(addr, pdata, byte_nums)
#define read_reg(cmd) \
		lcm_util.dsi_dcs_read_lcm_reg(cmd)
#define read_reg_v2(cmd, buffer, buffer_size) \
		lcm_util.dsi_dcs_read_lcm_reg_v2(cmd, buffer, buffer_size)

struct LCM_setting_table {
	unsigned int cmd;
	unsigned char count;
	unsigned char para_list[120];
};

static struct LCM_setting_table lcm_initialization_setting[] = {
//------------- Init_code -------------//
	{0xFF,1,{0x10}},
	{0xFB,1,{0x01}},
	{0xB0,1,{0x00}},

	{0xC1,16,{0x89,0x28,0x00,0x14,0x00,0xAA,0x02,0x0E,0x00,0x71,0x00,0x07,0x05,0x0E,0x05,0x16}},
	{0xC2,2,{0x1B,0xA0}},

	{0xFF,1,{0x20}},
	{0xFB,1,{0x01}},
	{0x01,1,{0x66}},
	{0x07,1,{0x3C}},
	{0x1B,1,{0x01}},
	{0x5C,1,{0x90}},
	{0x5E,1,{0xE6}},
	{0x69,1,{0xD0}},
	{0x95,1,{0xEF}},
	{0x96,1,{0xEF}},
	{0xF2,1,{0x64}},
	{0xF4,1,{0x64}},
	{0xF6,1,{0x64}},
	{0xF8,1,{0x64}},

	{0xFF,1,{0x24}},
	{0xFB,1,{0x01}},
	{0x04,1,{0x22}},
	{0x05,1,{0x00}},
	{0x06,1,{0xA3}},
	{0x07,1,{0xA3}},
	{0x08,1,{0x0F}},
	{0x09,1,{0x0F}},
	{0x0A,1,{0x17}},
	{0x0B,1,{0x15}},
	{0x0C,1,{0x13}},
	{0x0D,1,{0x2D}},
	{0x0E,1,{0x2C}},
	{0x0F,1,{0x2F}},
	{0x10,1,{0x2E}},
	{0x11,1,{0x29}},
	{0x12,1,{0x24}},
	{0x13,1,{0x24}},
	{0x14,1,{0x0B}},
	{0x15,1,{0x0C}},
	{0x16,1,{0x1C}},
	{0x17,1,{0x01}},
	{0x1C,1,{0x22}},
	{0x1D,1,{0x00}},
	{0x1E,1,{0xA3}},
	{0x1F,1,{0xA3}},
	{0x20,1,{0x0F}},
	{0x21,1,{0x0F}},
	{0x22,1,{0x17}},
	{0x23,1,{0x15}},
	{0x24,1,{0x13}},
	{0x25,1,{0x2D}},
	{0x26,1,{0x2C}},
	{0x27,1,{0x2F}},
	{0x28,1,{0x2E}},
	{0x29,1,{0x29}},
	{0x2A,1,{0x24}},
	{0x2B,1,{0x24}},
	{0x2D,1,{0x0B}},
	{0x2F,1,{0x0C}},
	{0x30,1,{0x1C}},
	{0x31,1,{0x01}},
	{0x32,1,{0x44}},
	{0x33,1,{0x02}},
	{0x34,1,{0x00}},
	{0x35,1,{0x01}},
	{0x36,1,{0x01}},
	{0x36,1,{0x01}},
	{0x37,1,{0x01}},
	{0x38,1,{0x10}},
	{0x3B,1,{0x04}},
	{0x4E,1,{0x48}},
	{0x4F,1,{0x48}},
	{0x53,1,{0x48}},
	{0x7A,1,{0x83}},
	{0x7B,1,{0x93}},
	{0x7D,1,{0x04}},
	{0x80,1,{0x04}},
	{0x81,1,{0x04}},
	{0x82,1,{0x13}},
	{0x84,1,{0x31}},
	{0x85,1,{0x00}},
	{0x86,1,{0x00}},
	{0x87,1,{0x00}},
	{0x90,1,{0x13}},
	{0x92,1,{0x31}},
	{0x93,1,{0x00}},
	{0x94,1,{0x00}},
	{0x95,1,{0x00}},
	{0x9C,1,{0xF4}},
	{0x9D,1,{0x01}},
	{0xA0,1,{0x13}},
	{0xA2,1,{0x13}},
	{0xA3,1,{0x03}},
	{0xA4,1,{0x04}},
	{0xA5,1,{0x04}},
	{0xC4,1,{0x80}},
	{0xC6,1,{0xC0}},
	{0xC9,1,{0x00}},
	{0xD9,1,{0x80}},
	{0xE9,1,{0x03}},

	{0xFF,1,{0x25}},
	{0xFB,1,{0x01}},
	{0x0F,1,{0x1B}},
	{0x19,1,{0xE4}},
	{0x21,1,{0x40}},
	{0x58,1,{0x0C}},
	{0x59,1,{0x0A}},
	{0x5C,1,{0x05}},
	{0x5F,1,{0x10}},
	{0x66,1,{0xD8}},
	{0x67,1,{0x01}},
	{0x68,1,{0x58}},
	{0x69,1,{0x10}},
	{0x6B,1,{0x00}},
	{0x6C,1,{0x1D}},
	{0x71,1,{0x1D}},
	{0x77,1,{0x62}},
	{0x79,1,{0x90}},
	{0x7E,1,{0x15}},
	{0x7F,1,{0x00}},
	{0x84,1,{0x6D}},
	{0x8D,1,{0x00}},
	{0xC0,1,{0xD5}},
	{0xC1,1,{0x11}},
	{0xC3,1,{0x00}},
	{0xC4,1,{0x11}},
	{0xC5,1,{0x11}},
	{0xC6,1,{0x11}},
	{0xEF,1,{0x00}},
	{0xF0,1,{0x00}},
	{0xF1,1,{0x04}},

	{0xFF,1,{0x26}},
	{0xFB,1,{0x01}},
	{0x00,1,{0x00}},
	{0x01,1,{0xF7}},
	{0x02,1,{0xF7}},
	{0x03,1,{0x00}},
	{0x04,1,{0xF7}},
	{0x05,1,{0x08}},
	{0x06,1,{0x1A}},
	{0x07,1,{0x1A}},
	{0x08,1,{0x1A}},
	{0x14,1,{0x06}},
	{0x15,1,{0x01}},
	{0x74,1,{0xAF}},
	{0x81,1,{0x13}},
	{0x83,1,{0x03}},
	{0x84,1,{0x03}},
	{0x85,1,{0x01}},
	{0x86,1,{0x03}},
	{0x87,1,{0x01}},
	{0x88,1,{0x06}},
	{0x8A,1,{0x1A}},
	{0x8B,1,{0x11}},
	{0x8C,1,{0x24}},
	{0x8E,1,{0x42}},
	{0x8F,1,{0x11}},
	{0x90,1,{0x11}},
	{0x91,1,{0x11}},
	{0x9A,1,{0x80}},
	{0x9B,1,{0x08}},
	{0x9C,1,{0x00}},
	{0x9D,1,{0x00}},
	{0x9E,1,{0x00}},

	{0xFF,1,{0x27}},
	{0xFB,1,{0x01}},
	{0x01,1,{0x9C}},
	{0x20,1,{0x81}},
	{0x21,1,{0xDF}},
	{0x25,1,{0x82}},
	{0x26,1,{0x13}},
	{0x6E,1,{0x9A}},
	{0x6F,1,{0x78}},
	{0x70,1,{0x00}},
	{0x71,1,{0x00}},
	{0x72,1,{0x00}},
	{0x73,1,{0x00}},
	{0x74,1,{0x00}},
	{0x75,1,{0x00}},
	{0x76,1,{0x00}},
	{0x77,1,{0x00}},
	{0x7D,1,{0x09}},
	{0x7E,1,{0xA5}},
	{0x7F,1,{0x03}},
	{0x80,1,{0x23}},
	{0x82,1,{0x09}},
	{0x83,1,{0xA5}},
	{0x88,1,{0x02}},

	{0xFF,1,{0x2A}},
	{0xFB,1,{0x01}},
	{0x00,1,{0x91}},
	{0x03,1,{0x20}},
	{0x06,1,{0x0C}},
	{0x07,1,{0x50}},
	{0x0A,1,{0x60}},
	{0x0C,1,{0x09}},
	{0x0D,1,{0x40}},
	{0x0E,1,{0x02}},
	{0x0F,1,{0x00}},
	{0x11,1,{0xF5}},
	{0x15,1,{0x0F}},
	{0x16,1,{0x71}},
	{0x19,1,{0x0F}},
	{0x1A,1,{0x45}},
	{0x1B,1,{0x14}},
	{0x1D,1,{0x36}},
	{0x1E,1,{0x4D}},
	{0x1F,1,{0x63}},
	{0x20,1,{0x4D}},
	{0x27,1,{0x80}},
	{0x28,1,{0xF7}},
	{0x29,1,{0x06}},
	{0x2A,1,{0x55}},
	{0x2D,1,{0x06}},
	{0x2F,1,{0x01}},
	{0x30,1,{0x45}},
	{0x31,1,{0x3E}},
	{0x33,1,{0x68}},
	{0x34,1,{0xF9}},
	{0x35,1,{0x33}},
	{0x36,1,{0x15}},
	{0x36,1,{0x15}},
	{0x37,1,{0xF4}},
	{0x38,1,{0x37}},
	{0x39,1,{0x11}},
	{0x3A,1,{0x45}},
	{0xEE,1,{0x01}},
	{0xF0,1,{0xFE}},
	{0xF5,1,{0x00}},

	{0xFF,1,{0x2B}},
	{0xFB,1,{0x01}},
	{0xB7,1,{0x1B}},
	{0xB8,1,{0x13}},
	{0xC0,1,{0x01}},


	{0xFF,1,{0xE0}},
	{0xFB,1,{0x01}},
	{0x35,1,{0x82}},

	{0xFF,1,{0xF0}},
	{0xFF,1,{0xF0}},
	{0xFB,1,{0x01}},
	{0x1C,1,{0x01}},
	{0x33,1,{0x01}},
	{0x5A,1,{0x00}},
	{0xD2,1,{0x52}},

	{0xFF,1,{0xD0}},
	{0xFB,1,{0x01}},
	{0x53,1,{0x22}},
	{0x54,1,{0x02}},

	{0xFF,1,{0xC0}},
	{0xFB,1,{0x01}},
	{0x9C,1,{0x11}},
	{0x9D,1,{0x11}},

	{0xFF,1,{0x10}},

	{0x11, 1, {0x00}},
	{REGFLAG_DELAY, 100, {}},
	{0x29, 1, {0x00}},
	{REGFLAG_DELAY, 20, {}},
};


static struct LCM_setting_table lcm_standby_mode_in_setting[] = {
	// Display off sequence
	{0x28, 0,{}},
	// Sleep Mode On
	{0x10, 0,{}},
	{REGFLAG_DELAY,120,{}},
};

static void push_table(struct LCM_setting_table *table, unsigned int count, unsigned char force_update)
{
	unsigned int i;
	unsigned int cmd;
	for(i = 0; i < count; i++) {
		cmd = table[i].cmd;
		switch (cmd) {
			case REGFLAG_DELAY:
				MDELAY(table[i].count);
				break;
			case REGFLAG_END_OF_TABLE:
				break;
			default:
				dsi_set_cmdq_V2(cmd, table[i].count, table[i].para_list, force_update);
				if (table[i].count > 1)
					MDELAY(1);
				break;
		}
	}
}

static void lcm_set_util_funcs(const struct LCM_UTIL_FUNCS *util)
{
	memcpy(&lcm_util, util, sizeof(struct LCM_UTIL_FUNCS));
}

#ifdef CONFIG_MTK_HIGH_FRAME_RATE
static void lcm_dfps_int(struct LCM_DSI_PARAMS *dsi)
{
	struct dfps_info *dfps_params = dsi->dfps_params;

	dsi->dfps_enable = 1;
	dsi->dfps_default_fps = 9000;/*real fps * 100, to support float*/
	dsi->dfps_def_vact_tim_fps = 9000;/*real vact timing fps * 100*/

	/*traversing array must less than DFPS_LEVELS*/
	/*DPFS_LEVEL0*/
	dfps_params[0].level = DFPS_LEVEL0;
	dfps_params[0].fps = 6000;/*real fps * 100, to support float*/
	dfps_params[0].vact_timing_fps = 9000;/*real vact timing fps * 100*/
	dfps_params[0].vertical_frontporch = 1321;

	/*if need mipi hopping params add here*/
	dfps_params[0].dynamic_switch_mipi = 0;
	dfps_params[0].PLL_CLOCK_dyn = 544;
	dfps_params[0].horizontal_frontporch_dyn = 236; //288;
	dfps_params[0].vertical_frontporch_dyn = 1321;

	/*DPFS_LEVEL1*/
	dfps_params[1].level = DFPS_LEVEL1;
	dfps_params[1].fps = 9000;/*real fps * 100, to support float*/
	dfps_params[1].vact_timing_fps = 9000;/*real vact timing fps * 100*/
	dfps_params[1].vertical_frontporch = 54;
	//dfps_params[1].vertical_frontporch_for_low_power = 1321;

	/*if need mipi hopping params add here*/
	dfps_params[1].dynamic_switch_mipi = 0;
	dfps_params[1].PLL_CLOCK_dyn = 544;
	dfps_params[1].horizontal_frontporch_dyn = 236; //288;
	dfps_params[1].vertical_frontporch_dyn = 54;

	dsi->dfps_num = 2;
}
#endif

static void lcm_get_params(struct LCM_PARAMS *params)
{
	memset(params, 0, sizeof(struct LCM_PARAMS));

	params->type                = LCM_TYPE_DSI;
	params->width               = FRAME_WIDTH;
	params->height              = FRAME_HEIGHT;
	params->physical_width      = PHYSICAL_WIDTH/1000;
	params->physical_height     = PHYSICAL_HEIGHT/1000;
	params->physical_width_um   = PHYSICAL_WIDTH;
	params->physical_height_um  = PHYSICAL_HEIGHT;

#if (LCM_DSI_CMD_MODE)
	params->dsi.mode            = CMD_MODE;
	params->dsi.switch_mode     = SYNC_PULSE_VDO_MODE;
#else
	params->dsi.mode            = SYNC_PULSE_VDO_MODE;
	params->dsi.switch_mode     = CMD_MODE;
#endif
	params->dsi.switch_mode_enable = 0;
	/* The following defined the fomat for data coming from LCD engine */
	params->dsi.data_format.color_order  = LCM_COLOR_ORDER_RGB;
	params->dsi.data_format.trans_seq    = LCM_DSI_TRANS_SEQ_MSB_FIRST;
	params->dsi.data_format.padding      = LCM_DSI_PADDING_ON_LSB;
	params->dsi.data_format.format       = LCM_DSI_FORMAT_RGB888;
	/* Highly depends on LCD driver capability. */
	params->dsi.packet_size              = 256;
	params->dsi.PS                       = LCM_PACKED_PS_24BIT_RGB888;

	/* LCD Panel params */
	params->dsi.vertical_sync_active     = 10;
	params->dsi.vertical_backporch       = 10;
	params->dsi.vertical_frontporch      = 54;//90Hz:54,60Hz:1321
	params->dsi.vertical_active_line     = FRAME_HEIGHT;
	params->dsi.horizontal_sync_active   = 36;
	params->dsi.horizontal_backporch     = 60;
	params->dsi.horizontal_frontporch    = 100;
	params->dsi.horizontal_active_pixel  = FRAME_WIDTH;
	params->dsi.LANE_NUM                 = LCM_FOUR_LANE;

	/* bdg dsc params */
#ifdef DSC_ENABLE
	params->dsi.dsc_params.ver = 11;
	params->dsi.dsc_params.slice_mode = 1;
	params->dsi.dsc_params.rgb_swap = 0;
	params->dsi.dsc_params.dsc_cfg = 34;
	params->dsi.dsc_params.rct_on = 1;
	params->dsi.dsc_params.bit_per_channel = 8;
	params->dsi.dsc_params.dsc_line_buf_depth = 9;
	params->dsi.dsc_params.bp_enable = 1;
	params->dsi.dsc_params.bit_per_pixel = 128;
	params->dsi.dsc_params.pic_height = FRAME_HEIGHT;
	params->dsi.dsc_params.pic_width = FRAME_WIDTH;
	params->dsi.dsc_params.slice_height = 20;
	params->dsi.dsc_params.slice_width = 540;
	params->dsi.dsc_params.chunk_size = 540;
	params->dsi.dsc_params.xmit_delay = 170;//V1:170, V2:512
	params->dsi.dsc_params.dec_delay = 526;
	params->dsi.dsc_params.scale_value = 32;
	params->dsi.dsc_params.increment_interval = 113;//V1:113, V2:488
	params->dsi.dsc_params.decrement_interval = 7;
	params->dsi.dsc_params.line_bpg_offset = 12;
	params->dsi.dsc_params.nfl_bpg_offset = 1294;
	params->dsi.dsc_params.slice_bpg_offset = 1302;
	params->dsi.dsc_params.initial_offset = 6144;
	params->dsi.dsc_params.final_offset = 7072;//V1:7072, V2:4336
	params->dsi.dsc_params.flatness_minqp = 3;
	params->dsi.dsc_params.flatness_maxqp = 12;
	params->dsi.dsc_params.rc_model_size = 8192;
	params->dsi.dsc_params.rc_edge_factor = 6;
	params->dsi.dsc_params.rc_quant_incr_limit0 = 11;
	params->dsi.dsc_params.rc_quant_incr_limit1 = 11;
	params->dsi.dsc_params.rc_tgt_offset_hi = 3;
	params->dsi.dsc_params.rc_tgt_offset_lo = 3;

	params->dsi.dsc_enable               = 0; //enable dsi dsc function,MTK set 0
	params->dsi.bdg_dsc_enable           = 1; //enable bdg dsc function
	params->dsi.bdg_ssc_disable          = 1; //default: close bdg ssc
	params->dsi.PLL_CLOCK                = 382; //with dsc
	params->dsi.data_rate                = 764; //2*PLL_CLOCK
#else
	params->dsi.dsc_enable               = 0; //close dsi dsc function
	params->dsi.bdg_dsc_enable           = 0; //close bdg dsc function
	params->dsi.bdg_ssc_disable          = 1; //default: close bdg ssc
	params->dsi.PLL_CLOCK                = 500; //without dsc
	params->dsi.data_rate                = 1000; //2*PLL_CLOCK
#endif
//	params->dsi.CLK_HS_POST              = 36;  //add mtk
//	params->dsi.PLL_CK_CMD               = 480;  //add mtk

	params->dsi.ssc_disable              = 1;
	params->dsi.ssc_range                = 4;
	params->dsi.clk_lp_per_line_enable   = 0;

	/* ESD check function */
	params->dsi.esd_check_enable                    = 0;
	params->dsi.customization_esd_check_enable      = 0;
	params->dsi.lcm_esd_check_table[0].cmd          = 0x0A;
	params->dsi.lcm_esd_check_table[0].count        = 1;
	params->dsi.lcm_esd_check_table[0].para_list[0] = 0x9C;

#ifdef CONFIG_MTK_HIGH_FRAME_RATE
	/****DynFPS start****/
	lcm_dfps_int(&(params->dsi));
	/****DynFPS end****/
#endif
}

static void lcm_init_power(void)
{
#ifdef BUILD_LK
	mt_set_gpio_mode(GPIO_LCD_BIAS_ENP_PIN, GPIO_MODE_00);
	mt_set_gpio_dir(GPIO_LCD_BIAS_ENP_PIN, GPIO_DIR_OUT);
	mt_set_gpio_out(GPIO_LCD_BIAS_ENP_PIN, GPIO_OUT_ONE);
	MDELAY(1);  //optimize DDIC resume time 20210702
	mt_set_gpio_mode(GPIO_LCD_BIAS_ENN_PIN, GPIO_MODE_00);
	mt_set_gpio_dir(GPIO_LCD_BIAS_ENN_PIN, GPIO_DIR_OUT);
	mt_set_gpio_out(GPIO_LCD_BIAS_ENN_PIN, GPIO_OUT_ONE);
#else
	disp_dts_gpio_select_state(DTS_GPIO_STATE_LCD_BIAS_ENP1);
	MDELAY(1);
	disp_dts_gpio_select_state(DTS_GPIO_STATE_LCD_BIAS_ENN1);
#endif
	aw37501_i2c_write_byte(aw37501_cmd_data[0].cmd, aw37501_cmd_data[0].data);
	MDELAY(1);
	aw37501_i2c_write_byte(aw37501_cmd_data[1].cmd, aw37501_cmd_data[1].data);
	MDELAY(1);
	aw37501_i2c_write_byte(aw37501_cmd_data[2].cmd, aw37501_cmd_data[2].data);
	MDELAY(1);
	aw37501_i2c_write_byte(aw37501_cmd_data[3].cmd, aw37501_cmd_data[3].data);
	MDELAY(1);
	aw37501_i2c_write_byte(aw37501_cmd_data[4].cmd, aw37501_cmd_data[4].data);
	MDELAY(5);
}

static void lcm_init(void)
{
#ifdef BUILD_LK
	SET_RESET_PIN(0);
	MDELAY(5);
	SET_RESET_PIN(1);
	MDELAY(5);
	SET_RESET_PIN(0);
	MDELAY(5);
	SET_RESET_PIN(1);
#else
	disp_dts_gpio_select_state(DTS_GPIO_STATE_LCM_RST_OUT0);
	MDELAY(5);
	disp_dts_gpio_select_state(DTS_GPIO_STATE_LCM_RST_OUT1);
	MDELAY(5);
	disp_dts_gpio_select_state(DTS_GPIO_STATE_LCM_RST_OUT0);
	MDELAY(5);
	disp_dts_gpio_select_state(DTS_GPIO_STATE_LCM_RST_OUT1);
#endif
	MDELAY(10);  //optimize DDIC resume time 20210702

	/* when phone initial , config output high, enable backlight drv chip */
	push_table(lcm_initialization_setting, sizeof(lcm_initialization_setting) / sizeof(struct LCM_setting_table), 1);
}

static void lcm_suspend(void)
{
	push_table(lcm_standby_mode_in_setting, sizeof(lcm_standby_mode_in_setting) / sizeof(struct LCM_setting_table), 1);
	LCM_LOGI("[%s] %s \n",LOG_TAG,__func__);
}

static void lcm_suspend_power(void)
{
#ifdef BUILD_LK
	mt_set_gpio_mode(GPIO_LCD_BIAS_ENN_PIN, GPIO_MODE_00);
	mt_set_gpio_dir(GPIO_LCD_BIAS_ENN_PIN, GPIO_DIR_OUT);
	mt_set_gpio_out(GPIO_LCD_BIAS_ENN_PIN, GPIO_OUT_ZERO);
	MDELAY(5);
	mt_set_gpio_mode(GPIO_LCD_BIAS_ENP_PIN, GPIO_MODE_00);
	mt_set_gpio_dir(GPIO_LCD_BIAS_ENP_PIN, GPIO_DIR_OUT);
	mt_set_gpio_out(GPIO_LCD_BIAS_ENP_PIN, GPIO_OUT_ZERO);
#else
	disp_dts_gpio_select_state(DTS_GPIO_STATE_LCD_BIAS_ENN0);
	MDELAY(5);
	disp_dts_gpio_select_state(DTS_GPIO_STATE_LCD_BIAS_ENP0);
	is_lcm_poweroff = 1; //lcm_power state is poweroff
#endif
}

static void lcm_resume_power(void)
{
#ifndef BUILD_LK
	if (is_lcm_poweroff) {
		disp_dts_gpio_select_state(DTS_GPIO_STATE_LCD_BIAS_ENP1);
		MDELAY(1);   //optimize DDIC resume time 20210630
		disp_dts_gpio_select_state(DTS_GPIO_STATE_LCD_BIAS_ENN1);

		aw37501_i2c_write_byte(aw37501_cmd_data[0].cmd, aw37501_cmd_data[0].data);
		MDELAY(1);
		aw37501_i2c_write_byte(aw37501_cmd_data[1].cmd, aw37501_cmd_data[1].data);
		MDELAY(1);
		aw37501_i2c_write_byte(aw37501_cmd_data[2].cmd, aw37501_cmd_data[2].data);
		MDELAY(1);
		aw37501_i2c_write_byte(aw37501_cmd_data[3].cmd, aw37501_cmd_data[3].data);
		MDELAY(1);
		aw37501_i2c_write_byte(aw37501_cmd_data[4].cmd, aw37501_cmd_data[4].data);
		MDELAY(5);
		is_lcm_poweroff = 0; // lcm_power state is poweron
	}
#endif
}

static void lcm_resume(void)
{
#ifndef BUILD_LK
	disp_dts_gpio_select_state(DTS_GPIO_STATE_LCM_RST_OUT0);
	MDELAY(5);
	disp_dts_gpio_select_state(DTS_GPIO_STATE_LCM_RST_OUT1);
	MDELAY(5);
	disp_dts_gpio_select_state(DTS_GPIO_STATE_LCM_RST_OUT0);
	MDELAY(5);
	disp_dts_gpio_select_state(DTS_GPIO_STATE_LCM_RST_OUT1);
	MDELAY(10);   //optimize DDIC resume time 20210630
	push_table(lcm_initialization_setting, sizeof(lcm_initialization_setting) / sizeof(struct LCM_setting_table), 1);
#endif
}

static unsigned int lcm_compare_id(void)
{
	return 1;
}

static unsigned int lcm_ata_check(unsigned char *buffer)
{
	if(get_lcm_status==0x9c){
		pr_info("[%s] ata check lcm status OK is 0x%x\n", LOG_TAG, get_lcm_status);
		return 1;
	}else{
		pr_debug("[%s] ata check lcm status NG is 0x%x\n", LOG_TAG, get_lcm_status);
		return 0;
	}
}

struct LCM_DRIVER nt36672c_fhdp_dsi_vdo_dsc_txd_boe_lcm_drv =
{
	.name           = "nt36672c_fhdp_dsi_vdo_dsc_txd_boe",
	.set_util_funcs = lcm_set_util_funcs,
	.get_params     = lcm_get_params,
	.init           = lcm_init,
	.suspend        = lcm_suspend,
	.resume         = lcm_resume,
	.compare_id     = lcm_compare_id,
	.init_power     = lcm_init_power,
#ifndef BUILD_LK
	.ata_check      = lcm_ata_check,
	.resume_power	= lcm_resume_power,
	.suspend_power	= lcm_suspend_power,
#endif
};
