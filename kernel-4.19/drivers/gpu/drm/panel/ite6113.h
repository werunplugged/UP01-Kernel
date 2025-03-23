/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#ifndef __IT6113_H__
#define __IT6113_H__

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>
#include <linux/i2c.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

/* i2c control start */
#define LCM_I2C_MODE			ST_MODE
#define LCM_I2C_SPEED			100
#define IT6112_BUSNUM			I2C2
#define MIPITX_ADDR				0x56
#define MIPIRX_ADDR				0x5e

#define it6112_DEBUG
#define OUTPUT_AS_RX_INPUT_MODE			0
#define SYNC_PULSE						1

#define ENABLE_MIPI_TX_PATTERN_GENERATOR		0
#define ENABLE_MIPI_TX_VRR_PATTERN_GENERATOR	0

/* TRUE: select tx clock from external, FALSE: select tx clock from rx */
#define ENABLE_MIPI_TX_EXTERNAL_MCLK			0
/* current config for BOE TV108QDM-NH4-D850 1600x2560 */
#define ENABLE_MIPI_TX_OUTPUT_CLOCK_CONTINUOUS	1
#define ENABLE_MIPI_TX_VRR_DETECT				0
#define ENABLE_MIPI_RX_LANE_SWAP				0
#define ENABLE_MIPI_PN_SWAP						0
#define ENABLE_MIPI_TX_LANE_SWAP				0
#define ENABLE_MIPI_TX_PN_SWAP					0
#define ENABLE_MIPI_TX_LINK_SWAP				0
#define LP_CMD_SET_MAX_RETURN_SIZE				1
/* 0 : mipi tx output 8 lane */
#define ENABLE_MIPI_TX_4_LANE_MODE				0

#define PANEL_DATA 0x0A

/* vendor option */
/* rx phase only 0, 1 */
#define ENABLE_INVERSE_RX_MCLK					1
/* tx phase only 0, 1 */
#define ENABLE_INVERSE_TX_MCLK					0

/* MIPI RX lane skew 0 ~ 7 */
#define MIPI_RX_ADJUST_LANE_DATA_SKEW			0
#define MIPI_RX_LANE_0_DATA_SKEW				4
#define MIPI_RX_LANE_1_DATA_SKEW				4
#define MIPI_RX_LANE_2_DATA_SKEW				4
#define MIPI_RX_LANE_3_DATA_SKEW				4
#define MIPI_RX_HS_SETTLE						3
#define MIPI_RX_HS_SKIP							4 /* 0 ~ 7 */
#define MIPI_TX_PATTERN_GENERATOR_FORMAT		0x0F
#define MIPI_TX_PATTERN_GENERATOR_BASE			0xFF
#define MIPI_TX_PATTERN_GENERATOR_V_INC			0xFF
#define MIPI_TX_PATTERN_GENERATOR_H_INC			0xFF
#define MIPI_TX_PATTERN_GENERATOR_COLOR_DEPTH	0x0E
/* 0: no change 1: positive vary 2: negative vary 3: positive <-> negative vary */
#define MIPI_TX_VRR_PATTERN_GENERATOR_MODE		3
/* 0~3, 0: normal(no change) n: every n frame */
#define MIPI_TX_VRR_PATTERN_GENERATOR_FREQ		1
/* number of pixels */
#define MIPI_TX_VRR_PATTERN_GENERATOR_AMP		3
/* change interval, 0: vfp, for it6113 D0 must be 0 */
#define MIPI_TX_VRR_PATTERN_GENERATOR_CHANGE_INTERVAL 0
#define ENABLE_MIPI_TX_PRE_1T					TRUE
#define ENABLE_MIPI_TX_HS_AUTO_SET				0
#define MIPI_TX_HS_AUTO_SET_PREPARE_ZERO_OFFSET	0
#define MIPI_TX_HS_AUTO_SET_TRAIL_OFFSET		0
#define MIPI_TX_LPX								0x04
#define MIPI_TX_HS_PREPARE						0x01
#define MIPI_TX_HS_PREPARE_ZERO					0x3B
#define MIPI_TX_HS_TRAIL						0x06
#define MIPI_RX_VIDEO_TYPE						RGB_24b
#define MIPI_TX_FIRE_LP_H_LINE_COUNT			3
#define MIPI_TX_FIRE_LP_START_LINE				VSYNC_START /* VSYNC_START, DE */
#define MIPI_TX_OCLK							0x16 /* oclk 0x14:50 MHz */
#define MIPI_TX_OCLK_NS							20
#define ENABLE_MIPI_TX_LP_BYPASS				0
#define ENABLE_MIPI_TX_LP_BYPASS_OPTION_FIX		0
#define MIPI_TX_ENABLE_INITIAL_FIRE_LP_CMD		0
#define ENABLE_MIPI_TX_HS_AUTO_READ				0
#define MIPI_TX_AUTO_READ_FRAME_COUNT			16
#define ENABLE_MIPI_RX_SYNC_ERROR_BIT			0
#define LP_CMD_FIFO_SIZE						128

struct mipi_display_mode {
	int m_hdisplay;
	int m_hfront_porch;
	int m_hsync_width;
	int m_hback_porch;
	int m_htotal;
	int m_vdisplay;
	int m_vfront_porch;
	int m_vsync_width;
	int m_vback_porch;
	int m_vtotal;
};

enum MIPI_VIDEO_TYPE {
	RGB_24b = 0x3E,
	RGB_30b = 0x0D,
	RGB_36b = 0x1D,
	RGB_18b = 0x1E,
	RGB_18b_L = 0x2E,
	YCbCr_16b = 0x2C,
	YCbCr_20b = 0x0C,
	YCbCr_24b = 0x1C,
};

enum chip {
	IT6112,
	IT6113,
};

enum chip_lp_cmd_fifo {
	IT6112_LP_CMD_FIFO = 32,
	IT6113_LP_CMD_FIFO = 128,
};

enum dcs_cmd_name {
	REGW0 = 0,
	REGW1,
	REGW2,
	REGW3,
	REGW4,
	REGW5,
	REGW6,
	REGW7,
	REGW8,
	REGW9,
	REGW10,
	REGW11,
	REGW12,
	REGW13,
	REGW14,
	REGW15,
	REGW16,
	REGW17,
	REGW18,
	REGW19,
	REGW20,
	REGW21,
	REGW22,
	REGW23,
	REGW24,
	REGW25,
	REGW26,
	REGW27,
	REGW28,
	REGW29,
	REGW30,
	REGW31,
	REGW32,
	REGW33,
	REGW34,
	REGW35,
	REGW36,
	REGW37,
	REGW38,
	REGW39,
	REGW40,
	REGW41,
	REGW42,
	REGW43,
	REGW44,
	REGW45,
	REGW46,
	REGW47,
	REGW48,
	REGW49,
	REGW50,
	REGW51,
	REGW52,
	REGW53,
	REGW54,
	REGW55,
	REGW56,
	REGW57,
	REGW58,
	REGW59,
	REGW60,
	REGW61,
	REGW62,
	REGW63,
	REGW64,
	REGW65,
	REGW66,
	REGW67,
	REGW68,
	REGW69,
	REGW70,
	REGW71,
	REGW72,
	REGW73,
	REGW74,
	REGW75,
	REGW76,
	REGW77,
	REGW78,
	REGW79,
	REGW80,
	REGW81,
	REGW82,
	REGW83,
	REGW84,
	REGW85,
	REGW86,
	REGW87,
	REGW88,
	REGW89,
	REGW90,
	REGW91,
	REGW92,
	REGW93,
	REGW94,
	REGW95,
	REGW96,
	REGW97,
	REGW98,
	REGW99,
	REGW100,
	REGW101,
	REGW102,
	REGW103,
	REGW104,
	REGW105,
	REGW106,
	REGW107,
	REGW108,
	REGW109,
	REGW110,
	REGW111,
	REGW112,
	REGW113,
	REGW114,
	REGW115,
	REGW116,
	REGW117,
	REGW118,
	REGW119,
	REGW120,
	REGW121,
	REGW122,
	REGW123,
	REGW124,
	REGW125,
	REGW126,
	REGW127,
	REGW128,
	REGW129,
	REGW130,
	REGW131,
	REGW132,
	REGW133,
	REGW134,
	REGW135,
	REGW136,
	REGW137,
	REGW138,
	REGW139,
	REGW140,
	REGW141,
	REGW142,
	REGW143,
	REGW144,
	REGW145,
	REGW146,
	REGW147,
	REGW148,
	REGW149,
	REGW150,
	REGW151,
	REGW152,
	REGW153,
	REGW154,
	REGW155,
	REGW156,
	REGW157,
	REGW158,
	REGW159,
	REGW160,
	REGW161,
	REGW162,
	REGW163,
	REGW164,
	REGW165,
	REGW166,
	REGW167,
	REGW168,
	REGW169,
	REGW170,
	REGW171,
	REGW172,
	REGW173,
	REGW174,
	REGW175,
	REGW176,
	REGW177,
	REGW178,
	REGW179,
	REGW180,
	REGW181,
	REGW182,
	REGW183,
	REGW184,
	REGW185,
	REGW186,
	REGW187,
	REGW188,
	REGW189,
	REGW190,
	REGW191,
	REGW192,
	REGW193,
	REGW194,
	REGW195,
	REGW196,
	REGW197,
	REGW198,
	REGW199,
	REGW200,
	REGW201,
	REGW202,
	REGW203,
	REGW204,
	REGW205,
	REGW206,
	REGW207,
	REGW208,
	REGW209,
	REGW210,
	REGW211,
	REGW212,
	REGW213,
	REGW214,
	REGW215,
	REGW216,
	REGW217,
	REGW218,
	REGW219,
	REGW220,
	REGW221,
	REGW222,
	REGW223,
	REGW224,
	REGW225,
	REGW226,
	REGW227,
	REGW228,
	REGW229,
	REGW230,
	REGW231,
	REGW232,
	REGW233,
	REGW234,
	REGW235,
	REGW236,
	REGW237,
	REGW238,
	REGW239,
	REGW240,
	REGW241,
	REGW242,
	REGW243,
	REGW244,
	REGW245,
	REGW246,
	REGW247,
	REGW248,
	REGW249,
	REGW250,
	REGW251,
	REGW252,
	REGW253,
	REGW254,
	REGW255,
	REGW256,
	REGW257,
	REGW258,
	REGW259,
	REGW260,
	REGW261,
	REGW262,
	REGW263,
	REGW264,
	REGW265,
	REGW266,
	REGW267,
	REGW268,
	REGW269,
	REGW270,
	REGW271,
	REGW272,
	REGW273,
	REGW274,
	REGW275,
	REGW276,
	REGW277,
	REGW278,
	REGW279,
	REGW280,
	REGW281,
	REGW282,
	REGW283,
	REGW284,
	REGW285,
	REGW286,
	REGW287,
	REGW288,
	REGW289,
	REGW290,
	REGW291,
	REGW292,
	REGW293,
	REGW294,
	REGW295,
	REGW296,
	REGW297,
	REGW298,
	REGW299,
	REGW300,
	REGW301,
	REGW302,
	REGW303,
	REGW304,
	REGW305,
	REGW306,
	REGW307,
	REGW308,
	REGW309,
	REGW310,
	REGW311,
	REGW312,
	REGW313,
	REGW314,
	REGW315,
	REGW316,
	REGW317,
	REGW318,
	REGW319,
	REGW320,
	REGW321,
	REGW322,
	REGW323,
	REGW324,
	REGW325,
	REGW326,
	REGW327,
	REGW328,
	REGW329,
	REGW330,
	REGW331,
	REGW332,
	REGW333,
	REGW334,
	REGW335,
	REGW336,
	REGW337,
	REGW338,
	LONG_WRITE_CMD = 512,
	LONG_WRITE_CMD_1,
	LONG_WRITE_CMD_2,
	LONG_WRITE_CMD_3,
	ENTER_SLEEP_MODE,
	SET_DISPLAY_OFF,
	EXIT_SLEEP_MODE,
	SET_DISPLAY_ON,
	GET_DISPLAY_MODE,
	LP_CMD_ENABLE_BYPASS_MODE,
	LP_CMD_DISABLE_BYPASS_MODE,
	LP_CMD_ENABLE_BYPASS_SEQ_MODE,
	SET_MAX_RETURN_SIZE,
	DELAY,
};

enum mipi_lp_cmd_type {
	LP_CMD_LPDT = 0x87,
	LP_CMD_BTA = 0xFF,
};

enum mipi_packet_size {
	SHORT_PACKET,
	LONG_PACKET,
	UNKNOWN_PACKET,
};

enum mipi_tx_start_line_count {
	DE,
	VSYNC_START,
};

enum mipi_tx_lp_cmd_header {
	NO_HEADER,
	CALC_HEADER,
};

struct mipi_packet_map {
	u8 data_id;
	enum mipi_packet_size packet_size;
};

/*
 * for dcs short packet word_count_l and word_count_h are transfer data[0] and data[1]
 * for dcs long packet word_count_l and word_count_h are word count low byte and high byte
 */

struct mipi_packet {
	u8 data_id;
	u8 word_count_l;
	u8 word_count_h;
	u8 ecc;
};

struct dcs_setting_entry {
	enum dcs_cmd_name cmd_name;
	enum mipi_lp_cmd_type cmd;
	u8 data_id;
	u8 count;
	/* for it6112 fifo up to LP_CMD_FIFO_SIZE byte, buffer maximum to LP_CMD_FIFO_SIZE */
	u8 para_list[LP_CMD_FIFO_SIZE];
};

struct init_table_info {
	unsigned int count;
	struct dcs_setting_entry *init_cmd_table;
};

struct it6112 {
	u8 revision;
	u8 chip_id;
	u8 enable_mipi_tx_vrr_detect;
	u8 enable_mipi_rx_bypass_mode;
	u8 enable_mipi_rx_lane_swap;
	u8 enable_mipi_rx_pn_swap;
	u8 enable_mipi_4_lane_mode;
	u8 mipi_rx_video_type;
	u8 enable_mipi_rx_mclk_inverse;
	u8 enable_mipi_tx_mclk_inverse;
	u8 mipi_rx_hs_skip;
	u8 mipi_rx_hs_settle;
	u8 enable_mipi_tx_output_clock_continuous;
	u8 enable_mipi_tx_pre_1t;
	u8 enable_mipi_tx_hs_auto_set;
	u8 enable_mipi_tx_h_enter_lps;
	u8 enable_mipi_tx_h_fire_packet;
	u8 enable_mipi_tx_lp_bypass;
	u8 enable_mipi_tx_lp_bypass_option_fix;
	u8 mipi_tx_oclk;
	u8 mipi_tx_oclk_ns;
	u8 mipi_tx_fire_lp_h_line_count;
	u8 mipi_tx_fire_lp_start_line;
	u8 enable_mipi_tx_hs_auto_read;
	u8 enable_ppi;
	u8 enable_mipi_tx_external_mclk;
	u8 enable_mipi_tx_pattern_generator;
	u8 enable_mipi_tx_vrr_pattern_generator;
	u8 enable_mipi_tx_initial_fire_lp_cmd;
	int mipi_tx_h_lps_time;
	int mipi_tx_v_lps_time;
	u8 mipi_tx_lp_11_time;
	u8 mipi_tx_hs_pretime;
	u8 mipi_tx_hs_prepare;
	u8 mipi_tx_hs_end_time;
	u8 mipi_tx_sync_pulse;
	u8 mipi_tx_lpx_num;
	u8 mipi_tx_pattern_generator_color_depth;
	u8 mipi_tx_pattern_generator_format;
	u8 mipi_tx_pattern_generator_v_inc;
	u8 mipi_tx_pattern_generator_h_inc;
	u8 mipi_tx_pattern_generator_base;
	u8 enable_mipi_tx_link_swap;
	u8 enable_mipi_tx_pn_swap;
	u8 enable_mipi_tx_lane_swap;
	u8 mipi_power_saving;
	struct mipi_display_mode mipi_rx_display_mode;
	/* kHz */
	int mipi_rx_rclk;
	/* kHz */
	int mipi_rx_mclk;

	struct init_table_info *init_table;

	void (*push_table)(struct drm_panel *panel,
		const struct dcs_setting_entry *dcs_setting_table,
		int dcs_table_size, enum dcs_cmd_name start, int count);
	struct device *dev;
	struct i2c_client *tx_i2c;
	struct i2c_client *rx_i2c;
	struct drm_panel *panel;
};

void chip_init(struct it6112 *it6112_client);
void device_power_off(struct it6112 *it6112_client);
int init_config(struct it6112 *it6112_client);
void it6112_set_backlight(struct it6112 *it6112_client, int map_level);
void it6112_read_ddic_reg(struct it6112 *it6112_client, u8 *buff);

void ite_poll_int(void *data);
void ite_poll_enable(int enable);

#endif
