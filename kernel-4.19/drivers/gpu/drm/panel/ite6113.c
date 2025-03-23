// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <uapi/linux/sched/types.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include "ite6113.h"

static struct i2c_client *it6112_tx_i2c_client;
static struct i2c_client *it6112_rx_i2c_client;

static struct task_struct *ite_poll_task;
static wait_queue_head_t _ite_poll_task_wq;
static atomic_t _ite_poll_task_wakeup = ATOMIC_INIT(1);

const static struct dcs_setting_entry bypass_mode_table[] = {
	{LP_CMD_ENABLE_BYPASS_MODE, LP_CMD_LPDT, 0x23, 2, {0x5A, 0x28}},
	{LP_CMD_DISABLE_BYPASS_MODE, LP_CMD_LPDT, 0x23, 2, {0x6C, 0x5F}},
	{LP_CMD_ENABLE_BYPASS_SEQ_MODE, LP_CMD_LPDT, 0x5A, 3, {0x28, 0x6C, 0x5F}},
};

const static struct dcs_setting_entry lp_cmd_read[] = {
	{SET_MAX_RETURN_SIZE, LP_CMD_LPDT, 0x37, 2,
		{LP_CMD_SET_MAX_RETURN_SIZE & 0xFF, LP_CMD_SET_MAX_RETURN_SIZE >> 8}},
	{GET_DISPLAY_MODE, LP_CMD_LPDT, 0x06, 2, {PANEL_DATA & 0xFF, PANEL_DATA >> 8}}
};

const static struct dcs_setting_entry suspend_table[] = {
	{ENTER_SLEEP_MODE, LP_CMD_LPDT, 0x05, 2, {0x28, 0x00}},
	{DELAY, 100, 0, 0, {0}},
	{SET_DISPLAY_OFF, LP_CMD_LPDT, 0x05, 2, {0x10, 0x00}},
};

struct mipi_packet_map packet_size_data_id_map[] = {
	{0x05, SHORT_PACKET},	/* dcs short write without parameter */
	{0x15, SHORT_PACKET},	/* dcs short write with one parameter */
	{0x23, SHORT_PACKET},	/* generic short write, 2 parameters */
	{0x29, LONG_PACKET},	/* generic long write */
	{0x39, LONG_PACKET},	/* dcs long write */
	{0x06, SHORT_PACKET},
	{0x16, SHORT_PACKET},
	{0x37, SHORT_PACKET},
	{0x03, SHORT_PACKET},
	{0x13, SHORT_PACKET},
	{0x23, SHORT_PACKET},
	{0x04, SHORT_PACKET},
	{0x14, SHORT_PACKET},
	{0x24, SHORT_PACKET}
};

static u32 it6112_i2c_tx_write_byte(struct it6112 *it6112_client, u8 addr, u8 data)
{
	int ret;
	u8 write_data[8];

	write_data[0]= addr;
	write_data[1] = data;

	ret = i2c_master_send(it6112_client->tx_i2c, write_data, 2);

	return ret;
}
static u32 it6112_i2c_rx_write_byte(struct it6112 *it6112_client, u8 addr, u8 data)
{
	int ret;
	u8 write_data[8];

	write_data[0]= addr;
	write_data[1] = data;

	ret = i2c_master_send(it6112_client->rx_i2c, write_data, 2);

	return ret;
}

static u32 it6112_i2c_tx_read_byte(struct it6112 *it6112_client, u8 addr, u8 *dataBuffer)
{
	u32 ret;

	ret = i2c_master_send(it6112_client->tx_i2c, &addr, 1);
	if (ret != 1)
		pr_info("%s: send fail=0x%x\n", __func__, ret);

	ret = i2c_master_recv(it6112_client->tx_i2c, dataBuffer, 1);
	if (ret != 1)
		pr_info("%s: read fail=0x%x\n", __func__, ret);

	return ret;
}

static u32 it6112_i2c_rx_read_byte(struct it6112 *it6112_client, u8 addr, u8 *dataBuffer)
{
	u32 ret;

	ret = i2c_master_send(it6112_client->rx_i2c, &addr, 1);
	if (ret != 1)
		pr_info("%s: send fail=0x%x\n", __func__, ret);

	ret = i2c_master_recv(it6112_client->rx_i2c, dataBuffer, 1);
	if (ret != 1)
		pr_info("%s: read fail=0x%x\n", __func__, ret);

	return ret;
}

int mipi_tx_write(struct it6112 *it6112_client, int offset, int data)
{
	return it6112_i2c_tx_write_byte(it6112_client, offset, data);
}

u8 mipi_tx_read(struct it6112 *it6112_client, int offset)
{
	u8 dataBuffer;

	it6112_i2c_tx_read_byte(it6112_client, offset, &dataBuffer);
	return dataBuffer;
}

int mipi_tx_set_bits(struct it6112 *it6112_client, int offset, int mask, int data)
{
	int temp;

	temp = mipi_tx_read(it6112_client, offset);
	temp = (temp & ((~mask) & 0xFF)) + (mask & data);

	return mipi_tx_write(it6112_client, offset, temp);
}

int mipi_rx_write(struct it6112 *it6112_client, int offset, int data)
{
	return it6112_i2c_rx_write_byte(it6112_client, offset, data);
}

u8 mipi_rx_read(struct it6112 *it6112_client, int offset)
{
	u8 dataBuffer;

	it6112_i2c_rx_read_byte(it6112_client, offset, &dataBuffer);
	return dataBuffer;
}

int mipi_rx_set_bits(struct it6112 *it6112_client, int offset, int mask, int data)
{
	int temp;

	temp = mipi_rx_read(it6112_client, offset);
	temp = (temp & ((~mask) & 0xFF)) + (mask & data);
	return mipi_rx_write(it6112_client, offset, temp);
}

u8 mipi_rx_get_video_state(struct it6112 *it6112_client)
{
	return !!(mipi_rx_read(it6112_client, 0x0D) & 0x10);
}

void mipi_tx_calc_hs_para(struct it6112 *it6112_client)
{
	u32 mclk_ps, tx_mclk_mhz = it6112_client->mipi_rx_mclk / 1000;
	int mipi_tx_calc_hs_end_time;

	pr_info( "renable hs auto set\n");
	pr_info( "rdefault reg0x22:0x%02x, reg0x1F:0x%02x\n",
		it6112_client->mipi_tx_hs_pretime, it6112_client->mipi_tx_hs_end_time);

	if (it6112_client->mipi_rx_mclk == 0) {
		pr_info( "mipi rx mclk = 0, error! use default reg0x22, reg0x1F\n");
		return;
	}

	mclk_ps = 1000000000 / it6112_client->mipi_rx_mclk;

	if (mclk_ps == 0) {
		pr_info( " mipi mclk_ps = 0, error! use default reg0x22, reg0x1F\n");
		return;
	}

	if (!it6112_client->enable_mipi_4_lane_mode) {
		mclk_ps *= 2;
		tx_mclk_mhz /= 2;
	}

	/* (5.5 * mclk_ps - 20 * 1000) / mclk_ps */
	mipi_tx_calc_hs_end_time =
		(11 * mclk_ps - 2 * it6112_client->mipi_tx_oclk_ns * 1000) / (2 * mclk_ps);

	it6112_client->mipi_tx_hs_pretime = (((145 + ((4 + it6112_client->mipi_tx_lpx_num)
		* it6112_client->mipi_tx_oclk_ns)) * 1000 - ((3 * mclk_ps) >> 1)) / mclk_ps)
		+ MIPI_TX_HS_AUTO_SET_PREPARE_ZERO_OFFSET;
	it6112_client->mipi_tx_hs_end_time = mipi_tx_calc_hs_end_time
		+ 1 + MIPI_TX_HS_AUTO_SET_TRAIL_OFFSET;

	if (tx_mclk_mhz >= 300)
		it6112_client->mipi_tx_hs_pretime += 20;
	else if (tx_mclk_mhz >= 150)
		it6112_client->mipi_tx_hs_pretime += (12 + (tx_mclk_mhz - 150) / 50 * 3);
	else
		it6112_client->mipi_tx_hs_pretime += 9;

	if (mipi_tx_calc_hs_end_time <= 0)
		it6112_client->mipi_tx_hs_end_time = 3;

	pr_info( "%s lanes mode, mclk_ns: %d.%d ns\n",
		it6112_client->enable_mipi_4_lane_mode ? "4" : "8", mclk_ps / 1000, mclk_ps % 1000);
	pr_info( "calc reg0x22:0x%02x, reg0x1F:0x%02x, lpx_num:0x%02x\n",
		it6112_client->mipi_tx_hs_pretime, it6112_client->mipi_tx_hs_end_time,
		it6112_client->mipi_tx_lpx_num);
}

void mipi_enter_power_saving(struct it6112 *it6112_client)
{
	mipi_rx_set_bits(it6112_client, 0x11, 0x08, 0x08);
	mipi_tx_set_bits(it6112_client, 0x06, 0x6e, 0x6e);
	it6112_client->mipi_power_saving = 1;
	pr_info("%s: enter power saving\n", __func__);
}

void mipi_exit_power_saving(struct it6112 *it6112_client)
{
	mipi_rx_set_bits(it6112_client, 0x11, 0x08, 0x0);
	mipi_tx_set_bits(it6112_client, 0x06, 0x6e, 0x0);
	it6112_client->mipi_power_saving = 0;
	pr_info("%s: exit power saving\n", __func__);
}
void mipi_rx_reset(struct it6112 *it6112_client)
{
	mipi_rx_set_bits(it6112_client, 0x05, 0x03, 0x03);
	mipi_rx_set_bits(it6112_client, 0x05, 0x03, 0x00);
}

void mipi_tx_reset(struct it6112 *it6112_client)
{
	mipi_tx_set_bits(it6112_client, 0x05, 0x06, 0x06);
	mipi_tx_set_bits(it6112_client, 0x05, 0x06, 0x00);
}

inline void mipi_tx_disable_ppi(struct it6112 *it6112_client)
{
	mipi_tx_set_bits(it6112_client, 0x06, 0x04, 0x04);
	it6112_client->enable_ppi = FALSE;
}

inline void mipi_tx_enable_ppi(struct it6112 *it6112_client)
{
	mipi_tx_set_bits(it6112_client, 0x06, 0x04, 0x00);
	it6112_client->enable_ppi = TRUE;
}

void mipi_polling_state(struct it6112 *it6112_client)
{
	int i, reg0e, reg0d, times = 10;

	for (i = 0; i < times; i++) {
		reg0e = mipi_tx_read(it6112_client, 0x0E);
		reg0d = mipi_rx_read(it6112_client, 0x0D);

		pr_info( "polling tx state reg0x0E:0x%02x, rx state reg0x0D:0x%02x\n",
			reg0e, reg0d);

		if ((reg0e & 0x27) != 0x00)
			mipi_tx_reset(it6112_client);
		else
			break;

		usleep_range(2000, 2500);
	}

	mipi_tx_enable_ppi(it6112_client);
	mdelay(10);

	pr_info( "after enable ppi tx state reg0x0E:0x%02x, rx state reg0x0D:0x%02x\n",
		mipi_tx_read(it6112_client, 0x0E), mipi_rx_read(it6112_client, 0x0D));
}

void mipi_rx_calc_rclk(struct it6112 *it6112_client)
{
	int rddata, i, sum, retry = 1;

	sum = 0;
	for(i = 0; i < retry; i++) {
		/* enable rclk 100ms counter */
		mipi_rx_set_bits(it6112_client, 0x94, 0x80, 0x80);
		mdelay(10);
		/* disable rclk 100ms counter */
		mipi_rx_set_bits(it6112_client, 0x94, 0x80, 0x00);

		rddata = mipi_rx_read(it6112_client, 0x95);
		rddata += (mipi_rx_read(it6112_client, 0x96) << 8);
		rddata += (mipi_rx_read(it6112_client, 0x97) << 16);

		sum += rddata;
	}
	sum /= retry;

	it6112_client->mipi_rx_rclk = sum / 100;
}

void mipi_rx_calc_mclk(struct it6112 *it6112_client)
{
	int i, rddata, sum = 0, retry = 1;

	for(i = 0; i < retry; i++) {
		mipi_rx_set_bits(it6112_client, 0x9B, 0x80, 0x80);
		mdelay(2);
		mipi_rx_set_bits(it6112_client, 0x9B, 0x80, 0x00);

		rddata = mipi_rx_read(it6112_client, 0x9A);
		rddata = ((mipi_rx_read(it6112_client, 0x9B) & 0x0F) << 8) + rddata;

		sum += rddata;
	}

	sum /= retry;

	it6112_client->mipi_rx_mclk = it6112_client->mipi_rx_rclk * 2048 / sum;
}

int mipi_rx_read_word(struct it6112 *it6112_client, unsigned int reg)
{
	int val_0, val_1;

	val_0 = mipi_rx_read(it6112_client, reg);

	if (val_0 < 0)
		return val_0;

	val_1 = mipi_rx_read(it6112_client, reg + 1);

	if (val_1 < 0)
		return val_1;

	return (val_1 << 8) | val_0;
}

void mipi_rx_get_mrec_timing(struct it6112 *it6112_client)
{
	struct mipi_display_mode *display_mode = &it6112_client->mipi_rx_display_mode;
	int MHVR2nd, MVFP2nd;

	mdelay(10);
	display_mode->m_hsync_width = mipi_rx_read_word(it6112_client, 0x52) & 0x3FFF;
	display_mode->m_hfront_porch = mipi_rx_read_word(it6112_client, 0x50) & 0x3FFF;
	display_mode->m_hback_porch = mipi_rx_read_word(it6112_client, 0x54) & 0x3FFF;
	display_mode->m_hdisplay = mipi_rx_read_word(it6112_client, 0x56) & 0x3FFF;
	display_mode->m_htotal = display_mode->m_hfront_porch + display_mode->m_hsync_width
		+ display_mode->m_hback_porch + display_mode->m_hdisplay;
	MHVR2nd = mipi_rx_read_word(it6112_client, 0x58) & 0x3FFF;

	display_mode->m_vsync_width = mipi_rx_read_word(it6112_client, 0x5C) & 0x1FFF;
	display_mode->m_vfront_porch = mipi_rx_read_word(it6112_client, 0x5A) & 0x1FFF;
	display_mode->m_vback_porch = mipi_rx_read_word(it6112_client, 0x5E) & 0x1FFF;
	display_mode->m_vdisplay = mipi_rx_read_word(it6112_client, 0x60) & 0x1FFF;
	display_mode->m_vtotal = display_mode->m_vfront_porch + display_mode->m_vsync_width
		+ display_mode->m_vback_porch + display_mode->m_vdisplay;
	MVFP2nd = mipi_rx_read_word(it6112_client, 0x62) & 0x3FFF;

	pr_info( "m_hfront_porch    = %d\n", display_mode->m_hfront_porch);
	pr_info( "m_hsync_width    = %d\n", display_mode->m_hsync_width);
	pr_info( "m_hback_porch    = %d\n", display_mode->m_hback_porch);
	pr_info( "m_hdisplay   = %d\n", display_mode->m_hdisplay);
	pr_info( "m_htotal  = %d\n", display_mode->m_htotal);
	pr_info( "MHVR2nd = %d\n", MHVR2nd);

	pr_info( "m_vfront_porch    = %d\n", display_mode->m_vfront_porch);
	pr_info( "m_vsync_width    = %d\n", display_mode->m_vsync_width);
	pr_info( "m_vback_porch   = %d\n", display_mode->m_vback_porch);
	pr_info( "m_vdisplay   = %d\n", display_mode->m_vdisplay);
	pr_info( "m_vtotal = %d\n", display_mode->m_vtotal);
	pr_info( "MVFP2nd   = %d\n", MVFP2nd);
}

void mipi_rx_init(struct it6112 *it6112_client)
{
	int reg30 = (0x08 | MIPI_RX_LANE_0_DATA_SKEW) | ((0x08 | MIPI_RX_LANE_1_DATA_SKEW) << 4);
	int reg31 = (0x08 | MIPI_RX_LANE_2_DATA_SKEW) | ((0x08 | MIPI_RX_LANE_3_DATA_SKEW) << 4);

	//Enable MPRX interrupt
	mipi_rx_write(it6112_client, 0x09, 0xFF);
	mipi_rx_write(it6112_client, 0x0A, 0xFF);
	mipi_rx_write(it6112_client, 0x0B, 0x3F);
	mipi_rx_write(it6112_client, 0x05, 0x03);
	mipi_rx_write(it6112_client, 0x05, 0x00);

	/* Setup INT Pin: Active Low */
	mipi_rx_set_bits(it6112_client, 0x0C, 0x0F, (it6112_client->enable_mipi_rx_lane_swap << 3)
		| (it6112_client->enable_mipi_rx_pn_swap << 2) | 0x03);
	mipi_rx_set_bits(it6112_client, 0x11, 0x01, it6112_client->enable_mipi_rx_mclk_inverse);

	if ((it6112_client->chip_id == IT6113) && (it6112_client->revision >= 0xD0)) {
		mipi_rx_set_bits(it6112_client, 0x18, 0xff, (ENABLE_MIPI_RX_SYNC_ERROR_BIT << 7)
			| (it6112_client->mipi_rx_hs_skip << 4) | it6112_client->mipi_rx_hs_settle);
	} else {
		mipi_rx_set_bits(it6112_client, 0x18, 0xf7, (ENABLE_MIPI_RX_SYNC_ERROR_BIT << 7)
			| (it6112_client->mipi_rx_hs_skip << 4) | it6112_client->mipi_rx_hs_settle);
	}

	mipi_rx_set_bits(it6112_client, 0x19, 0xf3, 0x03);
	mipi_rx_set_bits(it6112_client, 0x20, 0xf7, 0x03);
	/* rx auto detect video format */
	mipi_rx_set_bits(it6112_client, 0x21, 0x08, 0x08);
	mipi_rx_set_bits(it6112_client, 0x44, 0x22, 0x22);
	mipi_rx_write(it6112_client, 0x27, it6112_client->mipi_rx_video_type);
	mipi_rx_write(it6112_client, 0x72, 0x07);
	/* previous setting is 0x03 */
	mipi_rx_set_bits(it6112_client, 0x8A, 0x07, 0x02);
	mipi_rx_set_bits(it6112_client, 0xA0, 0x01, 0x01);

	if (it6112_client->enable_mipi_4_lane_mode)
		mipi_rx_set_bits(it6112_client, 0x80, 0x3F, 0x03);
	else
		mipi_rx_set_bits(it6112_client, 0x80, 0x3F, 0x07);

	if (MIPI_RX_ADJUST_LANE_DATA_SKEW) {
		mipi_rx_write(it6112_client, 0x30, reg30);
		mipi_rx_write(it6112_client, 0x31, reg31);
		pr_info( "MPRX setup lane data skew! reg0x30:0x%02x, reg0x31:0x%02x\n",
			mipi_rx_read(it6112_client, 0x30), mipi_rx_read(it6112_client, 0x31));
	}

	pr_info( "MPRX reg0x18:0x%02x hs_settle(reg0x18[6:4]) hs_skip(reg0x18[%s:0])\n",
		mipi_rx_read(it6112_client, 0x18),
		((it6112_client->chip_id == IT6113) && (it6112_client->revision >= 0xD0))
		? "3" : "2");
	pr_info( "MPRX initial done!\n");
}

void mipi_tx_adjusted_oclk(struct it6112 *it6112_client)
{
	if (it6112_client->chip_id == IT6113) {
		mipi_tx_write(it6112_client, 0xD3, it6112_client->mipi_tx_oclk);
		it6112_client->mipi_tx_oclk_ns = MIPI_TX_OCLK_NS; /* oclk 50 MHz, 20 ns */
		pr_info( "mipi tx oclk reg0xD3:0x%02x\n", mipi_tx_read(it6112_client, 0xD3));
	}
}

void mipi_tx_enable_vrr_detect(struct it6112 *it6112_client)
{
	mipi_tx_set_bits(it6112_client, 0x27, 0x40, 0x40);
}

void mipi_tx_disable_vrr_detect(struct it6112 *it6112_client)
{
	mipi_tx_set_bits(it6112_client, 0x27, 0x40, 0x00);
}

void mipi_tx_setup_lps_time(struct it6112 *it6112_client)
{
	mipi_tx_write(it6112_client, 0x23, it6112_client->mipi_tx_v_lps_time & 0xFF);
	mipi_tx_set_bits(it6112_client, 0x24, 0x0F,
		(it6112_client->mipi_tx_v_lps_time >> 8) & 0x0F);
	mipi_tx_write(it6112_client, 0x25, it6112_client->mipi_tx_h_lps_time & 0xFF);
	mipi_tx_set_bits(it6112_client, 0x26, 0x0F,
		(it6112_client->mipi_tx_h_lps_time >> 8) & 0x0F);
}

void mipi_tx_enable_lp_bypass(struct it6112 *it6112_client)
{
	mipi_tx_set_bits(it6112_client, 0x70, 0x18,
		(it6112_client->enable_mipi_tx_lp_bypass_option_fix << 4) | 0x08);
	mipi_tx_set_bits(it6112_client, 0x32, 0xBF,
		(it6112_client->mipi_tx_fire_lp_start_line << 7)
		| it6112_client->mipi_tx_fire_lp_h_line_count);
}

void mipi_tx_disable_lp_bypass(struct it6112 *it6112_client)
{
	mipi_tx_set_bits(it6112_client, 0x70, 0x00, 0x00);
}

void mipi_tx_setup_lps(struct it6112 *it6112_client)
{
	/* enable every line enter lps or only enter lps at last line in v blank */
	mipi_tx_set_bits(it6112_client, 0x21, 0x02,
		it6112_client->enable_mipi_tx_h_enter_lps ? 0x02 : 0x00);
	mipi_tx_setup_lps_time(it6112_client);
}

inline void mipi_tx_setup_lp_11_time(struct it6112 *it6112_client)
{
	mipi_tx_set_bits(it6112_client, 0x3C, 0x0F, it6112_client->mipi_tx_lp_11_time);
}

void mipi_tx_init(struct it6112 *it6112_client)
{
	u8 mipi_tx_link_lane_config = ((it6112_client->enable_mipi_tx_link_swap << 3)
		| (it6112_client->enable_mipi_tx_lane_swap << 1)
		| it6112_client->enable_mipi_tx_pn_swap);
	// MPTX Software Reset
	mipi_tx_write(it6112_client, 0x05, 0xFE);
	mipi_tx_write(it6112_client, 0x05, 0x00);
	mipi_tx_set_bits(it6112_client, 0x05, 0x01, 0x01); //software reset all, include RX!!
	mipi_tx_set_bits(it6112_client, 0x05, 0x20, 0x20); //hold tx first
	mipi_tx_set_bits(it6112_client, 0xD0, 0x04,
		it6112_client->enable_mipi_tx_external_mclk << 2);
	mdelay(2);

	mipi_tx_setup_lps(it6112_client);
	mipi_tx_setup_lp_11_time(it6112_client);
	mipi_tx_set_bits(it6112_client, 0x10, 0x20, OUTPUT_AS_RX_INPUT_MODE ? 0x00 : 0x20);
	mipi_tx_write(it6112_client, 0x22, it6112_client->mipi_tx_hs_pretime);

	if (!it6112_client->enable_mipi_tx_output_clock_continuous)
		mipi_tx_set_bits(it6112_client, 0x46, 0xF0, 0xF0);

	if (!OUTPUT_AS_RX_INPUT_MODE)
		mipi_tx_set_bits(it6112_client, 0x11, 0x10,
			it6112_client->mipi_tx_sync_pulse ? 0x00 : 0x10);

	mipi_tx_set_bits(it6112_client, 0x10, 0x02,
		(it6112_client->enable_mipi_tx_mclk_inverse << 1));
	mipi_tx_set_bits(it6112_client, 0x11, 0x08, (it6112_client->enable_mipi_4_lane_mode << 3));
	mipi_tx_set_bits(it6112_client, 0x3C, 0x20, 0x20);
	mipi_tx_set_bits(it6112_client, 0x44, 0x04, it6112_client->enable_mipi_tx_pre_1t << 2);
	mipi_tx_set_bits(it6112_client, 0x45, 0x0f, it6112_client->mipi_tx_lpx_num);
	mipi_tx_set_bits(it6112_client, 0x47, 0xf0, it6112_client->mipi_tx_hs_prepare << 4);
	mipi_tx_set_bits(it6112_client, 0xB0, 0xFF, 0x27);
	mipi_tx_write(it6112_client, 0x1f, it6112_client->mipi_tx_hs_end_time);

	if (it6112_client->chip_id == IT6113) {
		/* enable mipi tx non continuous clock hs fire lp not depends on mipi clock */
		mipi_tx_set_bits(it6112_client, 0x78, 0x20, 0x20);
		switch (it6112_client->revision) {
		case 0xD0:
			if (it6112_client->enable_mipi_tx_vrr_detect)
				mipi_tx_enable_vrr_detect(it6112_client);
			mipi_tx_set_bits(it6112_client, 0x8c,0x0b,mipi_tx_link_lane_config);
			break;
		default:
			break;
		}
	}

	mipi_tx_adjusted_oclk(it6112_client);

	pr_info( "mipi tx initial done! set reg0x22: 0x%02x, reg0x1F:0x%02x, lpx_num:0x%02x\n",
		it6112_client->mipi_tx_hs_pretime,
		it6112_client->mipi_tx_hs_end_time,
		it6112_client->mipi_tx_lpx_num);
}

void get_support_feature(struct it6112 *it6112_client)
{
	switch (it6112_client->chip_id) {
	case IT6112:
		if (!OUTPUT_AS_RX_INPUT_MODE && !it6112_client->mipi_tx_sync_pulse) {
			pr_info( "\not support sync event, output sync pulse\n");
			it6112_client->mipi_tx_sync_pulse = true;
		}
		break;

	case IT6113:
		if (!it6112_client->enable_mipi_tx_h_enter_lps) {
			pr_info( "not support h fire packet\n");
			it6112_client->enable_mipi_tx_h_fire_packet = false;
		}

		if (!it6112_client->enable_mipi_tx_link_swap && it6112_client->revision<0xD0) {
			pr_info( "not support link swap\n");
			it6112_client->enable_mipi_tx_link_swap = false;
		}
		break;

	default:
		break;
	}

	/* this value depends on mclk */
	//it6112_client->mipi_tx_hs_pretime =
	//	it6112_client->enable_mipi_tx_output_clock_continuous ?
	//	((it6112_client->chip_id == IT6112) ? 0x20 : 0x40) : 0x40;
}

void device_init(struct it6112 *it6112_client)
{
	get_support_feature(it6112_client);
	mipi_rx_init(it6112_client);

	//mdelay(50);
	mipi_rx_calc_rclk(it6112_client);
	mipi_rx_calc_mclk(it6112_client);
	//mdelay(50);

	if (it6112_client->enable_mipi_tx_hs_auto_set)
		mipi_tx_calc_hs_para(it6112_client);

	mipi_tx_init(it6112_client);
}

void it6112_mipi_rx_power_down(struct it6112 *it6112_client)
{
	/* MPRX Software Reset */
	/* Video Clock Domain Reset */
	mipi_rx_set_bits(it6112_client, 0x05, 0x02, 0x02);
}

int get_dcs_ecc(int dcshead)
{
	int q0, q1, q2, q3, q4, q5;

	q0 = ((dcshead >> 0) & (0x01)) ^ ((dcshead >> 1) & (0x01))
		^ ((dcshead >> 2) & (0x01)) ^ ((dcshead >> 4) & (0x01))
		^ ((dcshead >> 5) & (0x01)) ^ ((dcshead >> 7) & (0x01))
		^ ((dcshead >> 10) & (0x01)) ^ ((dcshead >> 11) & (0x01))
		^ ((dcshead >> 13) & (0x01)) ^ ((dcshead >> 16) & (0x01))
		^ ((dcshead >> 20) & (0x01)) ^ ((dcshead >> 21) & (0x01))
		^ ((dcshead >> 22) & (0x01)) ^ ((dcshead >> 23) & (0x01));
	q1 = ((dcshead >> 0) & (0x01)) ^ ((dcshead >> 1) & (0x01))
		^ ((dcshead >> 3) & (0x01)) ^ ((dcshead >> 4) & (0x01))
		^ ((dcshead >> 6) & (0x01)) ^ ((dcshead >> 8) & (0x01))
		^ ((dcshead >> 10) & (0x01)) ^ ((dcshead >> 12) & (0x01))
		^ ((dcshead >> 14) & (0x01)) ^ ((dcshead >> 17) & (0x01))
		^ ((dcshead >> 20) & (0x01)) ^ ((dcshead >> 21) & (0x01))
		^ ((dcshead >> 22) & (0x01)) ^ ((dcshead >> 23) & (0x01)) ;
	q2 = ((dcshead >> 0) & (0x01)) ^ ((dcshead>> 2) & (0x01))
		^ ((dcshead >> 3) & (0x01)) ^ ((dcshead >> 5) & (0x01))
		^ ((dcshead >> 6) & (0x01)) ^ ((dcshead >> 9) & (0x01))
		^ ((dcshead >> 11) & (0x01)) ^ ((dcshead >> 12) & (0x01))
		^ ((dcshead >> 15) & (0x01)) ^ ((dcshead >> 18) & (0x01))
		^ ((dcshead >> 20) & (0x01)) ^ ((dcshead >> 21) & (0x01))
		^ ((dcshead >> 22) & (0x01));
	q3 = ((dcshead >> 1) & (0x01)) ^ ((dcshead>> 2) & (0x01))
		^ ((dcshead >> 3) & (0x01)) ^ ((dcshead >> 7) & (0x01))
		^ ((dcshead >> 8) & (0x01)) ^ ((dcshead >> 9) & (0x01))
		^ ((dcshead >> 13) & (0x01)) ^ ((dcshead >> 14) & (0x01))
		^ ((dcshead >> 15) & (0x01)) ^ ((dcshead >> 19) & (0x01))
		^ ((dcshead >> 20) & (0x01)) ^ ((dcshead >> 21) & (0x01))
		^ ((dcshead >> 23) & (0x01));
	q4 = ((dcshead >> 4) & (0x01)) ^ ((dcshead >> 5) & (0x01))
		^ ((dcshead >> 6) & (0x01)) ^ ((dcshead >> 7) & (0x01))
		^ ((dcshead >> 8) & (0x01)) ^ ((dcshead >> 9) & (0x01))
		^ ((dcshead >> 16) & (0x01)) ^ ((dcshead >> 17) & (0x01))
		^ ((dcshead >> 18) & (0x01)) ^ ((dcshead >> 19) & (0x01))
		^ ((dcshead >> 20) & (0x01)) ^ ((dcshead >> 22) & (0x01))
		^ ((dcshead >> 23) & (0x01));
	q5 = ((dcshead >> 10) & (0x01)) ^ ((dcshead >> 11) & (0x01))
		^ ((dcshead >> 12) & (0x01)) ^ ((dcshead >> 13) & (0x01))
		^ ((dcshead >> 14) & (0x01)) ^ ((dcshead >> 15) & (0x01))
		^ ((dcshead >> 16) & (0x01)) ^ ((dcshead >> 17) & (0x01))
		^ ((dcshead >> 18) & (0x01)) ^ ((dcshead >> 19) & (0x01))
		^ ((dcshead >> 21) & (0x01)) ^ ((dcshead >> 22) & (0x01))
		^ ((dcshead >> 23) & (0x01));
	return (q0 + (q1 << 1) + (q2 << 2) + (q3 << 3) + (q4 << 4) + (q5 << 5));
}

int dcs_crc8t(int crcq16b, const u8 crc8bin)
{
	int lfsrout = 0, lfsr[16], i;

	lfsr[15] = ((crc8bin >> 7) & 0x01) ^ ((crc8bin >> 3) & 0x01) ^
		   ((crcq16b >> 7) & 0x01) ^ ((crcq16b >> 3) & 0x01);
	lfsr[14] = ((crc8bin >> 6) & 0x01) ^ ((crc8bin >> 2) & 0x01) ^
		   ((crcq16b >> 6) & 0x01) ^ ((crcq16b >> 2) & 0x01);
	lfsr[13] = ((crc8bin >> 5) & 0x01) ^ ((crc8bin >> 1) & 0x01) ^
		   ((crcq16b >> 5) & 0x01) ^ ((crcq16b >> 1) & 0x01);
	lfsr[12] = ((crc8bin >> 4) & 0x01) ^ ((crc8bin >> 0) & 0x01) ^
		   ((crcq16b >> 4) & 0x01) ^ ((crcq16b >> 0) & 0x01);
	lfsr[11] = ((crc8bin >> 3) & 0x01) ^
		   ((crcq16b >> 3) & 0x01);
	lfsr[10] = ((crc8bin >> 7) & 0x01) ^ ((crc8bin >> 3) & 0x01) ^ ((crc8bin >> 2) & 0x01) ^
		   ((crcq16b >> 7) & 0x01) ^ ((crcq16b >> 3) & 0x01) ^ ((crcq16b >> 2) & 0x01);
	lfsr[9] = ((crc8bin >> 6) & 0x01) ^ ((crc8bin >> 2) & 0x01) ^ ((crc8bin >> 1) & 0x01) ^
		  ((crcq16b >> 6) & 0x01) ^ ((crcq16b >> 2) & 0x01) ^ ((crcq16b >> 1) & 0x01);
	lfsr[8] = ((crc8bin >> 5) & 0x01) ^ ((crc8bin >> 1) & 0x01) ^ ((crc8bin >> 0) & 0x01) ^
		  ((crcq16b >> 5) & 0x01) ^ ((crcq16b >> 1) & 0x01) ^ ((crcq16b >> 0) & 0x01);
	lfsr[7] = ((crc8bin >> 4) & 0x01) ^ ((crc8bin >> 0) & 0x01) ^
		  ((crcq16b >> 15) & 0x01) ^ ((crcq16b >> 4) & 0x01) ^ ((crcq16b >> 0) & 0x01);
	lfsr[6] = ((crc8bin >> 3) & 0x01) ^
		  ((crcq16b >> 14) & 0x01) ^ ((crcq16b >> 3) & 0x01);
	lfsr[5] = ((crc8bin >> 2) & 0x01) ^
		  ((crcq16b >> 13) & 0x01) ^ ((crcq16b >> 2) & 0x01);
	lfsr[4] = ((crc8bin >> 1) & 0x01) ^
		  ((crcq16b >> 12) & 0x01) ^ ((crcq16b >> 1) & 0x01);
	lfsr[3] = ((crc8bin >> 7) & 0x01) ^ ((crc8bin >> 3) & 0x01) ^ ((crc8bin >> 0) & 0x01) ^
		  ((crcq16b >> 11) & 0x01) ^ ((crcq16b >> 7) & 0x01)
		  ^ ((crcq16b >> 3) & 0x01) ^ ((crcq16b >> 0) & 0x01);
	lfsr[2] = ((crc8bin >> 6) & 0x01) ^ ((crc8bin >> 2) & 0x01) ^
		  ((crcq16b >> 10) & 0x01) ^ ((crcq16b >> 6) & 0x01) ^ ((crcq16b >> 2) & 0x01);
	lfsr[1] = ((crc8bin >> 5) & 0x01) ^ ((crc8bin >> 1) & 0x01) ^
		  ((crcq16b >> 9) & 0x01) ^ ((crcq16b >> 5) & 0x01) ^ ((crcq16b >> 1) & 0x01);
	lfsr[0] = ((crc8bin >> 4) & 0x01) ^ ((crc8bin >> 0) & 0x01) ^
		  ((crcq16b >> 8) & 0x01) ^ ((crcq16b >> 4) & 0x01) ^ ((crcq16b >> 0) & 0x01);

	for (i = 0; i < ARRAY_SIZE(lfsr); i++)
		lfsrout = lfsrout + (lfsr[i]<<i);

	return lfsrout;
}

int get_dcs_crc(int bytenum, const u8 *crcbyte)
{
	int i, crctemp = 0xFFFF;

	for (i = 0; i <= bytenum - 1; i++)
		crctemp = dcs_crc8t(crctemp, crcbyte[i]);

	return crctemp;
}

void mipi_tx_setup_long_packet_header(struct mipi_packet *pheader, u32 word_count)
{
	int header;

	pheader->word_count_h = word_count >> 8;
	pheader->word_count_l = (u8)word_count;
	header = pheader->data_id | pheader->word_count_h << 16 | pheader->word_count_l << 8;
	pheader->ecc = get_dcs_ecc(header);
}

enum mipi_packet_size mipi_tx_get_packet_size(const struct dcs_setting_entry *dcs_setting_table,
	enum dcs_cmd_name cmd_name)
{
	u8 i, size = ARRAY_SIZE(packet_size_data_id_map);

	for (i = 0; i < size; i++) {
		if (dcs_setting_table[cmd_name].data_id == packet_size_data_id_map[i].data_id)
			break;
	}

	if (i == size) {
		if (dcs_setting_table[cmd_name].count == 0) {
			pr_info( "error! cmd index: %d count = 0\n", cmd_name);
			return UNKNOWN_PACKET;
		} else if (dcs_setting_table[cmd_name].count < 3) {
			return SHORT_PACKET;
		} else {
			return LONG_PACKET;
		}
	}

	return packet_size_data_id_map[i].packet_size;

}

void mipi_tx_get_packet_fire_state(struct it6112 *it6112_client)
{
	u8 lp_cmd_fifo, link0_data_fifo, link1_data_fifo;

	lp_cmd_fifo = mipi_tx_read(it6112_client, 0x7F);
	link0_data_fifo = mipi_tx_read(it6112_client, 0x80);
	link1_data_fifo = mipi_tx_read(it6112_client, 0x81);

	if (lp_cmd_fifo != 0)
		pr_info( "error! fire low power cmd fail, remain bytes not fire, reg0x7F:0x%02x\n",
			lp_cmd_fifo);
	if (link0_data_fifo != 0)
		pr_info( "error! fire link0 low power data fail, remain %d bytes not fire, reg0x80:0x%02x\n",
			link0_data_fifo, link0_data_fifo);
	if (link1_data_fifo != 0)
		pr_info( "error! fire link1 low power data fail, remain %d bytes not fire, reg0x81:0x%02x\n",
			link1_data_fifo, link1_data_fifo);
}

void mipi_tx_setup_packet(struct mipi_packet *pheader,
			  const struct dcs_setting_entry *dcs_setting_table,
			  enum dcs_cmd_name cmd_name)
{
	int short_cmd;
	enum mipi_packet_size packet_size;

	pheader->data_id = dcs_setting_table[cmd_name].data_id;
	packet_size = mipi_tx_get_packet_size(dcs_setting_table, cmd_name);

	if (packet_size == UNKNOWN_PACKET) {
		pr_info( "error! unknown packet size and check dcs table parameter\n");
		return;
	}

	if (packet_size == SHORT_PACKET) {
		pheader->word_count_l = dcs_setting_table[cmd_name].para_list[0];
		pheader->word_count_h = dcs_setting_table[cmd_name].para_list[1];
		short_cmd = pheader->data_id |
			pheader->word_count_l << 8 | pheader->word_count_h << 16;
		pheader->ecc = get_dcs_ecc(short_cmd);
	}

	if (packet_size == LONG_PACKET)
		mipi_tx_setup_long_packet_header(pheader, dcs_setting_table[cmd_name].count);
}

inline void mipi_tx_fire_packet(struct it6112 *it6112_client,
	const struct dcs_setting_entry *dcs_setting_table, enum dcs_cmd_name cmd_name)
{
	mipi_tx_write(it6112_client, 0x75, dcs_setting_table[cmd_name].cmd);
}

void mipi_tx_setup_packet_process(struct it6112 *it6112_client,
	const struct dcs_setting_entry *dcs_setting_table,
	enum dcs_cmd_name cmd_name, enum mipi_tx_lp_cmd_header header_select)
{
	struct mipi_packet packet;
	enum mipi_packet_size packet_size;
	u32 long_packet_checksum;
	int i, header_crc, data_count;

	if (!header_select) {
		pr_info( "no header packet\n");

		for (i = 0; i < dcs_setting_table[cmd_name].count; i++) {
			mipi_tx_write(it6112_client, 0x73,
				dcs_setting_table[cmd_name].para_list[i]);
			pr_info( "data[%d]: 0x%02x ", i, dcs_setting_table[cmd_name].para_list[i]);
		}

		header_crc = 0;
		goto short_packet;
	}

	mipi_tx_setup_packet(&packet, dcs_setting_table, cmd_name);
	packet_size = mipi_tx_get_packet_size(dcs_setting_table, cmd_name);

	for (i = 0; i < sizeof(packet); i++)
		mipi_tx_write(it6112_client, 0x73, ((u8 *)(&packet))[i]);

	if (packet_size == SHORT_PACKET) {
		header_crc = 2;
		goto short_packet;
	}

	header_crc = sizeof(packet) + 2;

	long_packet_checksum = get_dcs_crc(dcs_setting_table[cmd_name].count,
		dcs_setting_table[cmd_name].para_list);
	for (i = 0; i < dcs_setting_table[cmd_name].count; i++)
		mipi_tx_write(it6112_client, 0x73, dcs_setting_table[cmd_name].para_list[i]);

	mipi_tx_write(it6112_client, 0x73, (u8)long_packet_checksum);
	mipi_tx_write(it6112_client, 0x73, (u8)(long_packet_checksum >> 8));

short_packet:

	switch (it6112_client->chip_id) {
	case IT6112:
		mipi_tx_write(it6112_client, 0x74,
			0x40 | (dcs_setting_table[cmd_name].count + header_crc));
		break;

	case IT6113:
		data_count = dcs_setting_table[cmd_name].count + header_crc;

		if (data_count == IT6113_LP_CMD_FIFO)
			data_count = 1;

		mipi_tx_write(it6112_client, 0x74,
			(it6112_client->enable_mipi_tx_h_fire_packet << 7) | data_count);
		break;

	default:
		break;
	}

}

inline u8 mipi_tx_get_video_state(struct it6112 *it6112_client)
{
	return mipi_tx_read(it6112_client, 0x0E) & 0x10;
}

void mipi_tx_write_dcs_cmds(struct it6112 *it6112_client,
	const struct dcs_setting_entry *dcs_setting_table, int dcs_table_size,
	enum dcs_cmd_name start, int count, enum mipi_tx_lp_cmd_header header_select)
{
	u8 header_size, i, data_count,
		enable_force_lp_mode = !mipi_tx_get_video_state(it6112_client);
	u8 lp_cmd_fifo_size[] = { IT6112_LP_CMD_FIFO, IT6113_LP_CMD_FIFO };

	if (enable_force_lp_mode) {
		mipi_tx_set_bits(it6112_client, 0x11, 0x80, 0x80);
		mipi_tx_set_bits(it6112_client, 0x70, 0x04, 0x04);
	}

	mipi_tx_write(it6112_client, 0x3D, 0x00);
	mipi_tx_write(it6112_client, 0x3E, enable_force_lp_mode ? 0x00 : 0x10);
	mipi_tx_write(it6112_client, 0x3F, enable_force_lp_mode ? 0x30 : 0x90);

	if (it6112_client->chip_id == IT6113 && it6112_client->revision == 0xD0) {
		mipi_tx_write(it6112_client, 0x3D, 0xFF);
		mipi_tx_write(it6112_client, 0x3E, 0xFF);
		mipi_tx_write(it6112_client, 0x3F, 0xD0);
	}

	for (i = start; i < start + count; i++) {
		pr_info( "cmd:%d rx reg0d:0x%02x, tx reg0e:0x%02x\n", i,
			mipi_rx_read(it6112_client, 0x0D), mipi_tx_read(it6112_client, 0x0E));
		if (i >= dcs_table_size)
			goto complete_write_dcs;
		if (dcs_setting_table[i].cmd_name == DELAY) {
			mdelay(dcs_setting_table[i].cmd);
			continue;
		}

		header_size = header_select ?
			((mipi_tx_get_packet_size(dcs_setting_table, i) == SHORT_PACKET) ? 2 : 6)
			: 0;
		data_count = dcs_setting_table[i].count + header_size;

		if (data_count > lp_cmd_fifo_size[it6112_client->chip_id]) {
			pr_info( "error! lp cmd: %d, exceed cmd fifo\n", i);
			continue;
		}

		mipi_tx_setup_packet_process(it6112_client, dcs_setting_table, i, header_select);
		mipi_tx_fire_packet(it6112_client, dcs_setting_table, i);
		if (enable_force_lp_mode)
			mipi_tx_get_packet_fire_state(it6112_client);
	}
	usleep_range(10000, 15000);

complete_write_dcs:
	if (i >= dcs_table_size && (start + count > dcs_table_size))
		pr_info( "error! exceed maximum dcs setting table index\n");

	if (enable_force_lp_mode) {
		mipi_tx_set_bits(it6112_client, 0x11, 0x80, 0x00);
		mipi_tx_set_bits(it6112_client, 0x70, 0x04, 0x00);
	}

	mdelay(2);

	if (!enable_force_lp_mode)
		mipi_tx_get_packet_fire_state(it6112_client);
}

void it6112_enter_bus_turn_around(struct it6112 *it6112_client)
{
	u8 enable_force_lp_mode = !mipi_tx_get_video_state(it6112_client);

	if (enable_force_lp_mode) {
		mipi_tx_set_bits(it6112_client, 0x11, 0x80, 0x80);
		mipi_tx_set_bits(it6112_client, 0x70, 0x04, 0x04);
	}

	mipi_tx_write(it6112_client, 0x3E, 0x10);
	mipi_tx_write(it6112_client, 0x3F, 0x90);

	if (it6112_client->chip_id == IT6113 && it6112_client->revision == 0xD0) {
		mipi_tx_write(it6112_client, 0x3E, 0xFF);
		mipi_tx_write(it6112_client, 0x3F, 0xD0);
	}

	switch (it6112_client->chip_id) {
	case IT6112:
		mipi_tx_write(it6112_client, 0x74, 0x40);
		break;

	case IT6113:
		mipi_tx_write(it6112_client, 0x74,
			it6112_client->enable_mipi_tx_h_fire_packet << 7);
		break;
	default:
		break;
	}

	mipi_tx_write(it6112_client, 0x75, LP_CMD_BTA);

	if (enable_force_lp_mode) {
		mipi_tx_set_bits(it6112_client, 0x11, 0x80, 0x00);
		mipi_tx_set_bits(it6112_client, 0x70, 0x04, 0x00);
	}
}

void mipi_tx_enable_lp_cmd_bypass_mode(struct it6112 *it6112_client)
{
	mipi_rx_set_bits(it6112_client, 0x05, 0x02, 0x02);
	mdelay(2);
	mipi_tx_write_dcs_cmds(it6112_client, bypass_mode_table,
		ARRAY_SIZE(bypass_mode_table), 0, 1, CALC_HEADER);
}

void mipi_tx_disable_lp_cmd_bypass_mode(struct it6112 *it6112_client)
{
	mipi_tx_write_dcs_cmds(it6112_client, bypass_mode_table,
		ARRAY_SIZE(bypass_mode_table), 1, 1, CALC_HEADER);
	mdelay(2);
	mipi_rx_set_bits(it6112_client, 0x05, 0x02, 0x00);
}

void it6112_mipi_read_panel(struct it6112 *it6112_client,
	const struct dcs_setting_entry *dcs_setting_table, int dcs_table_size,
	enum dcs_cmd_name cmd_name, u8 *buffer)
{
	int /*data,*/ link0_data_count, link0_cmd_type, link1_data_count, link1_cmd_type, i;

	mipi_tx_write_dcs_cmds(it6112_client, dcs_setting_table,
		dcs_table_size, cmd_name, 1, CALC_HEADER);
	it6112_enter_bus_turn_around(it6112_client);
	mdelay(5);
	link0_data_count = mipi_tx_read(it6112_client, 0x7A);
	link0_cmd_type = mipi_tx_read(it6112_client, 0x7B);

	for (i = 0; i < link0_data_count; i++) {
		buffer[i] = mipi_tx_read(it6112_client, 0x79);
		pr_info( "link0_data[%d]:0x%02x\n", i, mipi_tx_read(it6112_client, 0x79));
	}
	if (it6112_client->chip_id == IT6113) {
		link1_data_count = mipi_tx_read(it6112_client, 0x7D);
		link1_cmd_type = mipi_tx_read(it6112_client, 0x7E);

		for (i = 0; i < link1_data_count; i++)
			pr_info( "\n\rlink1_data[%d]:0x%02x", i, mipi_tx_read(it6112_client, 0x7C));
	}
}

void mipi_tx_read_back_link0_fifo_data(struct it6112 *it6112_client, u8 *buffer, int data_count)
{
	int i;

	for (i = 0; i < data_count; i++) {
		buffer[i] = mipi_tx_read(it6112_client, 0x79);
		pr_info("link0_data[%d]:0x%02x\n", i, buffer[i]);
	}
}

int mipi_tx_get_link0_read_back_data_count(struct it6112 *it6112_client)
{
	return mipi_tx_read(it6112_client, 0x7A);
}

int mipi_tx_get_link0_read_back_cmd_type(struct it6112 *it6112_client)
{
	return mipi_tx_read(it6112_client, 0x7B);
}

int mipi_tx_get_read_stage(struct it6112 *it6112_client)
{
	return mipi_tx_read(it6112_client, 0x82);
}

int mipi_tx_read_panel(struct it6112 *it6112_client,
	const struct dcs_setting_entry *dcs_setting_table, int dcs_table_size,
	enum dcs_cmd_name cmd_name, u8 *buffer)
{
	int link0_data_count, link0_cmd_type;

	mipi_tx_write_dcs_cmds(it6112_client,
		dcs_setting_table, dcs_table_size, cmd_name, 1, CALC_HEADER);
	it6112_enter_bus_turn_around(it6112_client);
	mdelay(5);
	link0_data_count = mipi_tx_get_link0_read_back_data_count(it6112_client);
	link0_cmd_type = mipi_tx_get_link0_read_back_cmd_type(it6112_client);

	//pr_info(("link0_data_count(reg0x7A):0x%02x, link0_cmd_type(reg0x7B):0x%02x\n",
	//	link0_data_count, link0_cmd_type));

	mipi_tx_read_back_link0_fifo_data(it6112_client, buffer, link0_data_count);

	return link0_data_count;
}

int mipi_tx_read_panel_process(struct it6112 *it6112_client, u8 *buffer)
{
	int data_count;

	mipi_tx_write_dcs_cmds(it6112_client,
		lp_cmd_read, ARRAY_SIZE(lp_cmd_read), 0, 1, CALC_HEADER);
	data_count = mipi_tx_read_panel(it6112_client,
		lp_cmd_read, ARRAY_SIZE(lp_cmd_read), 1, buffer);

	return data_count;
}

void mipi_tx_enable_auto_read(struct it6112 *it6112_client, u8 enabled)
{
	mipi_tx_write(it6112_client, 0x77, MIPI_TX_AUTO_READ_FRAME_COUNT);
	mipi_tx_set_bits(it6112_client, 0x70, 0x40, enabled << 6);
}

void mipi_tx_auto_read_polling(struct it6112 *it6112_client, u8 *buffer)
{
	int link0_data_count, link0_cmd_type;

	if (mipi_tx_get_read_stage(it6112_client) > 0) {
		link0_data_count = mipi_tx_get_link0_read_back_data_count(it6112_client);
		link0_cmd_type = mipi_tx_get_link0_read_back_cmd_type(it6112_client);
		mipi_tx_read_back_link0_fifo_data(it6112_client, buffer, link0_data_count);
		pr_info("%s link0_data_count:%d link0_cmd_type:0x%02x\n",
			__func__, link0_data_count, link0_cmd_type);
	}
}

void mipi_tx_enable_auto_read_panel(struct it6112 *it6112_client, u8 *buffer)
{
	mipi_tx_read_panel_process(it6112_client, buffer);
	mipi_tx_enable_auto_read(it6112_client, it6112_client->enable_mipi_tx_hs_auto_read);
	pr_info("%s able auto read panel\n",
		it6112_client->enable_mipi_tx_hs_auto_read ? "en" : "dis");
}

void mipi_tx_setup_pattern_generator_video_format(struct it6112 *it6112_client)
{
	struct mipi_display_mode *display_mode = &it6112_client->mipi_rx_display_mode;
	int m_hdisplay = display_mode->m_hdisplay * 3 / 2,
		m_hfront_porch = display_mode->m_hfront_porch * 4 / 2;
	int m_hback_porch = display_mode->m_hback_porch * 4 / 2,
		m_hsync_width = display_mode->m_hsync_width * 4 / 2;
	int m_htotal = m_hfront_porch + m_hsync_width + m_hback_porch + m_hdisplay;

	mipi_tx_write(it6112_client, 0x91,
		(it6112_client->mipi_tx_pattern_generator_color_depth << 4)
		| it6112_client->mipi_tx_pattern_generator_format);
	mipi_tx_write(it6112_client, 0x92, it6112_client->mipi_tx_pattern_generator_base);
	mipi_tx_write(it6112_client, 0x93, it6112_client->mipi_tx_pattern_generator_v_inc);
	mipi_tx_write(it6112_client, 0x94, it6112_client->mipi_tx_pattern_generator_h_inc);
	mipi_tx_write(it6112_client, 0x95, display_mode->m_vsync_width);
	mipi_tx_write(it6112_client, 0x96, m_htotal & 0xFF);
	mipi_tx_write(it6112_client, 0x97, (m_htotal & 0xFF00) >> 8);
	mipi_tx_write(it6112_client, 0x98, m_hdisplay & 0xFF);
	mipi_tx_write(it6112_client, 0x99, (m_hdisplay & 0xFF00) >> 8);
	mipi_tx_write(it6112_client, 0x9A, m_hback_porch & 0xFF);
	mipi_tx_write(it6112_client, 0x9B, (m_hback_porch & 0xFF00) >> 8);
	mipi_tx_write(it6112_client, 0x9C, m_hsync_width & 0xFF);
	mipi_tx_write(it6112_client, 0x9D, (m_hsync_width & 0xFF00) >> 8);
	mipi_tx_write(it6112_client, 0x9E, display_mode->m_vtotal & 0xFF);
	mipi_tx_write(it6112_client, 0x9F, (display_mode->m_vtotal & 0xFF00) >> 8);
	mipi_tx_write(it6112_client, 0xA0, display_mode->m_vdisplay & 0xFF);
	mipi_tx_write(it6112_client, 0xA1, (display_mode->m_vdisplay & 0xFF00) >> 8);
	mipi_tx_write(it6112_client, 0xA2, display_mode->m_vback_porch & 0xFF);
	mipi_tx_write(it6112_client, 0xA3, (display_mode->m_vback_porch & 0xFF00) >> 8);
}

inline void mipi_tx_config_pattern_generator(struct it6112 *it6112_client)
{
	mipi_tx_write(it6112_client, 0x90, it6112_client->enable_mipi_tx_pattern_generator);
}

void mipi_tx_setup_vrr_pattern_generator(struct it6112 *it6112_client)
{
	mipi_tx_set_bits(it6112_client, 0x34, 0x7f, (MIPI_TX_VRR_PATTERN_GENERATOR_MODE << 5)
		| (MIPI_TX_VRR_PATTERN_GENERATOR_CHANGE_INTERVAL << 3)
		| (MIPI_TX_VRR_PATTERN_GENERATOR_FREQ << 1)
		| it6112_client->enable_mipi_tx_vrr_pattern_generator);
	mipi_tx_write(it6112_client, 0x35, MIPI_TX_VRR_PATTERN_GENERATOR_AMP & 0x00FF);
	mipi_tx_write(it6112_client, 0x36, (MIPI_TX_VRR_PATTERN_GENERATOR_AMP & 0xFF00) >> 8);
	mipi_tx_disable_vrr_detect(it6112_client);
}

void mipi_tx_config_pattern_generator_process(struct it6112 *it6112_client)
{
	mipi_tx_setup_pattern_generator_video_format(it6112_client);
	if (it6112_client->chip_id == IT6113) {
		switch (it6112_client->revision) {
		case 0xD0:
			if (it6112_client->enable_mipi_tx_vrr_pattern_generator)
				mipi_tx_setup_vrr_pattern_generator(it6112_client);
			break;
		default:
			break;
		}
	}
	mipi_tx_config_pattern_generator(it6112_client);
}

void mipi_tx_set_output(struct it6112 *it6112_client)
{
	if (it6112_client->enable_mipi_tx_initial_fire_lp_cmd) {
		mipi_tx_write_dcs_cmds(it6112_client,
			it6112_client->init_table->init_cmd_table,
			it6112_client->init_table->count, 0,
			it6112_client->init_table->count, CALC_HEADER);
	} else {
		pr_info( "%s,send cmd by dsi\n",__func__);
		it6112_client->push_table(it6112_client->panel,
			bypass_mode_table, ARRAY_SIZE(bypass_mode_table), 0, 1);
		it6112_client->push_table(it6112_client->panel,
			it6112_client->init_table->init_cmd_table,
			it6112_client->init_table->count, 0, it6112_client->init_table->count);
		it6112_client->push_table(it6112_client->panel,
			bypass_mode_table, ARRAY_SIZE(bypass_mode_table), 1, 1);
	}
	mipi_tx_set_bits(it6112_client, 0x44, 0x01,
		it6112_client->enable_mipi_tx_output_clock_continuous);

	usleep_range(1000, 1500);
	mipi_tx_write(it6112_client, 0x05, 0xfe);
	mipi_tx_disable_ppi(it6112_client);
	mipi_tx_write(it6112_client, 0x05, 0x00);
	mipi_polling_state(it6112_client);
	pr_info("output clock:%d tx reg0x44:0x%02x, mipi rx receive video type reg0x28: 0x%02x\n",
			it6112_client->enable_mipi_tx_output_clock_continuous,
			mipi_tx_read(it6112_client, 0x44),
			mipi_rx_read(it6112_client, 0x28));
}

int chip_identify(struct it6112 *it6112_client)
{
	static const char * const id_name[] = {"it6112", "it6113"};
	u8 id[][4] = {{0x54, 0x49, 0x12, 0x61},
					{0x54, 0x49, 0x13, 0x61}};
	u8 i, j;

	for (i = 0; i < ARRAY_SIZE(id); i++) {

		for (j = 0; j < ARRAY_SIZE(id[i]); j++) {
			if (mipi_tx_read(it6112_client, j) != id[i][j])
				break;
		}

		if (j != ARRAY_SIZE(id[i]))
			break;
	}

	if (i < ARRAY_SIZE(id)) {
		it6112_client->revision = mipi_tx_read(it6112_client, 0x04);

		switch (mipi_tx_read(it6112_client, 0x02)) {
		case 0x12:
			it6112_client->chip_id = IT6112;
			break;

		case 0x13:
			it6112_client->chip_id = IT6113;
			break;

		default:
			break;
		}

		pr_info( "find %s", id_name[it6112_client->chip_id]);

		return 0;
	}

	pr_info( "can not find chip");

	return -1;
}


int init_config(struct it6112 *it6112_client)
{
	if (!it6112_tx_i2c_client || !it6112_rx_i2c_client) {
		pr_info("it6112 iic NULL, please wait!\n");
		return -1;
	}

	it6112_client->tx_i2c = it6112_tx_i2c_client;
	it6112_client->rx_i2c = it6112_rx_i2c_client;
	it6112_client->enable_mipi_rx_lane_swap = ENABLE_MIPI_RX_LANE_SWAP;
	it6112_client->enable_mipi_rx_pn_swap = ENABLE_MIPI_PN_SWAP;
	it6112_client->enable_mipi_4_lane_mode = ENABLE_MIPI_TX_4_LANE_MODE;
	it6112_client->mipi_rx_video_type = MIPI_RX_VIDEO_TYPE;
	it6112_client->enable_mipi_rx_mclk_inverse = ENABLE_INVERSE_RX_MCLK;
	it6112_client->enable_mipi_tx_mclk_inverse = ENABLE_INVERSE_TX_MCLK;
	it6112_client->mipi_rx_hs_settle = MIPI_RX_HS_SETTLE;
	it6112_client->mipi_rx_hs_skip = MIPI_RX_HS_SKIP;
	it6112_client->enable_mipi_tx_vrr_detect = ENABLE_MIPI_TX_VRR_DETECT;
	it6112_client->enable_mipi_tx_pattern_generator = ENABLE_MIPI_TX_PATTERN_GENERATOR;
	it6112_client->enable_mipi_tx_vrr_pattern_generator = ENABLE_MIPI_TX_VRR_PATTERN_GENERATOR;
	it6112_client->enable_mipi_tx_external_mclk = ENABLE_MIPI_TX_EXTERNAL_MCLK;
	it6112_client->mipi_tx_pattern_generator_h_inc = MIPI_TX_PATTERN_GENERATOR_H_INC;
	it6112_client->mipi_tx_pattern_generator_v_inc = MIPI_TX_PATTERN_GENERATOR_V_INC;
	it6112_client->mipi_tx_pattern_generator_color_depth =
			MIPI_TX_PATTERN_GENERATOR_COLOR_DEPTH;
	it6112_client->mipi_tx_pattern_generator_format = MIPI_TX_PATTERN_GENERATOR_FORMAT;
	it6112_client->mipi_tx_pattern_generator_base = MIPI_TX_PATTERN_GENERATOR_BASE;
	it6112_client->mipi_tx_hs_end_time = MIPI_TX_HS_TRAIL;/* default: 0x08 */
	/* adjust tx low power state's command time interval */
	it6112_client->mipi_tx_lpx_num = MIPI_TX_LPX;
	it6112_client->mipi_tx_hs_prepare = MIPI_TX_HS_PREPARE;
	it6112_client->mipi_tx_hs_pretime = MIPI_TX_HS_PREPARE_ZERO;
	it6112_client->enable_mipi_tx_h_enter_lps = TRUE;
	it6112_client->enable_mipi_tx_output_clock_continuous =
			ENABLE_MIPI_TX_OUTPUT_CLOCK_CONTINUOUS;
	it6112_client->mipi_tx_h_lps_time = 0x100;
	it6112_client->mipi_tx_v_lps_time = 0x100;
	it6112_client->mipi_tx_lp_11_time = 0x03;
	it6112_client->mipi_tx_sync_pulse = SYNC_PULSE;
	it6112_client->enable_mipi_tx_h_fire_packet = true;
	it6112_client->enable_mipi_tx_pre_1t = ENABLE_MIPI_TX_PRE_1T;
	it6112_client->enable_mipi_tx_hs_auto_set = ENABLE_MIPI_TX_HS_AUTO_SET;
	it6112_client->mipi_tx_fire_lp_h_line_count = MIPI_TX_FIRE_LP_H_LINE_COUNT;
	it6112_client->mipi_tx_fire_lp_start_line = MIPI_TX_FIRE_LP_START_LINE;
	it6112_client->enable_mipi_tx_lp_bypass = ENABLE_MIPI_TX_LP_BYPASS;
	it6112_client->enable_mipi_tx_lp_bypass_option_fix = ENABLE_MIPI_TX_LP_BYPASS_OPTION_FIX;
	it6112_client->enable_mipi_tx_hs_auto_read = ENABLE_MIPI_TX_HS_AUTO_READ;
	it6112_client->mipi_tx_oclk = MIPI_TX_OCLK;
	it6112_client->mipi_tx_oclk_ns = MIPI_TX_OCLK_NS; /* oclk 50 MHz, 20 ns */
	it6112_client->enable_mipi_tx_initial_fire_lp_cmd = MIPI_TX_ENABLE_INITIAL_FIRE_LP_CMD;
	it6112_client->enable_mipi_tx_link_swap = ENABLE_MIPI_TX_LINK_SWAP;
	it6112_client->enable_mipi_tx_pn_swap = ENABLE_MIPI_TX_PN_SWAP;
	it6112_client->enable_mipi_tx_lane_swap = ENABLE_MIPI_TX_LANE_SWAP;
	return chip_identify(it6112_client);
}

void device_power_off(struct it6112 *it6112_client)
{
	ite_poll_enable(0);
	it6112_client->enable_mipi_tx_output_clock_continuous = true;
	mipi_tx_set_bits(it6112_client, 0x44, 0x01,
		it6112_client->enable_mipi_tx_output_clock_continuous);
	mdelay(5);
	mipi_tx_write_dcs_cmds(it6112_client,
		suspend_table, ARRAY_SIZE(suspend_table), 0,
		ARRAY_SIZE(suspend_table), CALC_HEADER);
	mipi_tx_set_bits(it6112_client, 0x05, 0xFE, 0xFE);
	mipi_rx_set_bits(it6112_client, 0x05, 0x03, 0x03);
	mipi_tx_set_bits(it6112_client, 0x06, 0x02, 0x02);
}

void device_power_on(struct it6112 *it6112_client)
{
	/* Video Clock Domain Reset */
	mipi_tx_set_bits(it6112_client, 0x06, 0x02, 0x00);
	mdelay(2);
	mipi_rx_set_bits(it6112_client, 0x05, 0x0F, 0x00);
	/* tx mclk still hold */
	mipi_tx_set_bits(it6112_client, 0x05, 0xCC, 0x00);
	mdelay(2);
}

void it6112_set_backlight(struct it6112 *it6112_client, int map_level)
{
	struct dcs_setting_entry backlight_table = {
		REGW0, LP_CMD_LPDT, 0x39, 3, {0x51, 0xff, 0xff}
	};

	backlight_table.para_list[1] = map_level & 0xFF00 >> 8;
	backlight_table.para_list[2] = map_level & 0xff;
	mipi_tx_write_dcs_cmds(it6112_client, &backlight_table, 1, REGW0, 1, CALC_HEADER);
	pr_info("%s: set backlight map_level:%d, map to %d\n", __func__, map_level);
}

void chip_init(struct it6112 *it6112_client)
{
	u8 read_back[10] = {0};

	if(!it6112_client->init_table || !it6112_client->push_table) {
		pr_info( "Must set init_cmd and push_table before used it!\n");
		return;
	}

	device_power_on(it6112_client);
	device_init(it6112_client);
	mipi_rx_get_mrec_timing(it6112_client);

	if (it6112_client->enable_mipi_tx_pattern_generator)
		mipi_tx_config_pattern_generator_process(it6112_client);

	mipi_tx_set_output(it6112_client);
	mipi_tx_enable_auto_read_panel(it6112_client, read_back);
	ite_poll_int(it6112_client);
	ite_poll_enable(1);
}

void it6112_read_ddic_reg(struct it6112 *it6112_client, u8 *buff)
{
	if(!it6112_client) {
		pr_info("%s: error! hane NULL\n", __func__);
		return;
	}
	mipi_tx_read_panel_process(it6112_client, buff);
}

/*
 * ret: 1 tx is ok; 0 tx has some error
 *
 */
u8 mipi_tx_state(struct it6112 *it6112_client)
{
	int reg0e, reg0d;

	reg0d = mipi_rx_read(it6112_client, 0x0D);
	reg0e = mipi_tx_read(it6112_client, 0x0E);

	return ((reg0e & 0x27) == 0x00);
}

void device_esd_reinit(struct it6112 *it6112_client)
{
	device_power_on(it6112_client);
	device_init(it6112_client);
	mipi_rx_get_mrec_timing(it6112_client);

	if (it6112_client->enable_mipi_tx_pattern_generator)
		mipi_tx_config_pattern_generator_process(it6112_client);

	mipi_tx_set_output(it6112_client);
}

void poll(struct it6112 *it6112_client)
{
	int reg0d;

	reg0d = mipi_rx_read(it6112_client, 0x0D);
	pr_info( "it6112 rx reg0D:0x%02x enable_ppi:%d\n", reg0d, it6112_client->enable_ppi);

	if ((reg0d & 0x10) && (reg0d & 0x08)) {
		pr_info( "it6112 rx clock not stable\n");
		mipi_rx_reset(it6112_client);
		return;
	}

	if (reg0d & 0x10) {
		if (it6112_client->enable_ppi) {
			if (!mipi_tx_state(it6112_client))
				device_esd_reinit(it6112_client);
		} else {
			mipi_polling_state(it6112_client);
		}
	}
}

static int ite_poll_worker_kthread(void *data)
{
	int ret = 0;
	struct sched_param param = {
		.sched_priority = 99
	};

	sched_setscheduler(current, SCHED_RR, &param);
	while (1) {
		msleep(1000);
		ret = wait_event_interruptible(_ite_poll_task_wq,
			atomic_read(&_ite_poll_task_wakeup));
		if (ret < 0) {
			pr_info("[ite] ite polling thread waked up accidently!\n");
			continue;
		}

		poll((struct it6112 *)data);
		pr_debug("[ite] ite polling!\n");
		if (kthread_should_stop())
			break;
	}
	return 0;
}

void ite_poll_enable(int enable)
{
	pr_debug("[ite] %s enable=%d\n", __func__, enable);
	if (enable) {
		atomic_set(&_ite_poll_task_wakeup, 1);
		wake_up_interruptible(&_ite_poll_task_wq);
	} else
		atomic_set(&_ite_poll_task_wakeup, 0);
}

void ite_poll_int(void *data)
{
	ite_poll_task = kthread_create(ite_poll_worker_kthread, data, "ite_polling");
	init_waitqueue_head(&_ite_poll_task_wq);
	wake_up_process(ite_poll_task);
}

/* I2C */
static int it6112_driver_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	pr_info("%s++++:\n", __func__);
	it6112_tx_i2c_client = devm_kzalloc(&client->dev,
		sizeof(struct i2c_client), GFP_KERNEL);
	if (!it6112_tx_i2c_client)
		return -ENOMEM;

	memcpy(it6112_tx_i2c_client, client, sizeof(struct i2c_client));
	it6112_rx_i2c_client = i2c_new_dummy(client->adapter, 0x5e);
	pr_info("%s----\n", __func__);
	return 0;
}

static int it6112_driver_remove(struct i2c_client *client)
{
	pr_info("it6112 iic driver remove!\n");
	it6112_rx_i2c_client = NULL;
	it6112_tx_i2c_client = NULL;

	return 0;
}

static const struct of_device_id it6112_match[] = {
	{.compatible = "ite,it6112"},
	{},
};

MODULE_DEVICE_TABLE(of, it6112_match);

static struct i2c_driver it6112_driver = {
	.driver = {
		.name = "ite,it6112",
		.of_match_table = it6112_match,
	},
	.probe = it6112_driver_probe,
	.remove = it6112_driver_remove,
};

static int __init it6112_init(void)
{
	return i2c_add_driver(&it6112_driver);
}
module_init(it6112_init);

static void __exit it6112_exit(void)
{
	i2c_del_driver(&it6112_driver);
}
module_exit(it6112_exit);

MODULE_AUTHOR("henry tu <henry.tu@mediatek.com>");
MODULE_DESCRIPTION("it6112 panel driver");
MODULE_LICENSE("GPL v2");

