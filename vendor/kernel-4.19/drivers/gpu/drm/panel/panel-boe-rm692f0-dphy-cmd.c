// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <drm/drmP.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/of_graph.h>
#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#define CONFIG_MTK_PANEL_EXT
#if defined(CONFIG_MTK_PANEL_EXT)
#include "../mediatek/mtk_panel_ext.h"
#include "../mediatek/mtk_log.h"
#include "../mediatek/mtk_drm_graphics_base.h"
#endif

extern char tpgesture_status;
static atomic_t current_backlight;

#if IS_ENABLED(CONFIG_CUST_DEVICE_INFO_SUPPORT)

int up_set_lcm_device_used(char * module_name, int pdata);
typedef enum 
{ 
    DEVICE_SUPPORTED = 0,        
    DEVICE_USED = 1,
}compatible_type;

static struct mipi_dsi_driver lcm_driver;
#endif


#define ENABLE_DSC 1
#define REGFLAG_CMD       	0xFFFA
#define REGFLAG_DELAY      	0xFFFC
#define REGFLAG_UDELAY  	0xFFFB
#define REGFLAG_END_OF_TABLE    0xFFFD
#define AOD_BACKLIGHT_RATIO 10


static int current_fps = 60;
bool aod_state = false;
EXPORT_SYMBOL(aod_state);


struct lcm {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *powerdm_gpio;
	struct gpio_desc *dvdd_gpio;
	struct gpio_desc *vci_gpio;
	bool prepared;
	bool enabled;

	int error;
};


struct LCM_setting_table {
	unsigned int cmd;
	unsigned char count;
	unsigned char para_list[128];
};

static struct LCM_setting_table lcm_aod_to_normal[] = {
	{REGFLAG_CMD,2,{0xFE,0x00}},
	{REGFLAG_CMD,2,{0x38,0x00}},
	{REGFLAG_CMD,2,{0XFE,0x84}},
	{REGFLAG_CMD,2,{0xE0,0x90}},
	{REGFLAG_CMD,2,{0xFE,0xA0}},
	{REGFLAG_CMD,2,{0x1F,0x1E}},
	{REGFLAG_CMD,2,{0xFE,0x00}},
	

	{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_normal_to_aod[] = {
    //{REGFLAG_DELAY,20,{}},

	{REGFLAG_CMD,2,{0xFE,0x84}},
	{REGFLAG_CMD,2,{0xE0,0x90}},
	{REGFLAG_CMD,2,{0XFE,0x00}},
	{REGFLAG_CMD,2,{0x39,0x00}},
	{REGFLAG_CMD,2,{0xFE,0xA0}},
	{REGFLAG_CMD,2,{0x1F,0x16}},
	{REGFLAG_CMD,2,{0xFE,0x00}},


    /* Display on */
    //{REGFLAG_CMD, 1, {0x29}},

    {REGFLAG_END_OF_TABLE, 0x00, {}}
    
    
    
};




static struct LCM_setting_table lcm_aod_high_mode[] = {
	/* aod 60nit*/
	{REGFLAG_CMD,2,{0xFE,0x00}},
	{REGFLAG_CMD,2,{0x39,0x00}},
	{REGFLAG_CMD,2,{0XFE,0x00}},
	{REGFLAG_CMD,3,{0x51,0x0D,0xBB}},
	{REGFLAG_END_OF_TABLE, 0x00, {}}
};


static struct LCM_setting_table lcm_aod_mid_mode[] = {
	/* aod 30nit*/
	{REGFLAG_CMD,2,{0xFE,0x00}},
	{REGFLAG_CMD,2,{0x39,0x00}},
	{REGFLAG_CMD,2,{0XFE,0x00}},
	{REGFLAG_CMD,3,{0x51,0x0A,0x06}},
	{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_aod_low_mode[] = {
	/* aod 15nit*/
	{REGFLAG_CMD,2,{0xFE,0x00}},
	{REGFLAG_CMD,2,{0x39,0x00}},
	{REGFLAG_CMD,2,{0XFE,0x00}},
	{REGFLAG_CMD,3,{0x51,0x07,0x50}},
	{REGFLAG_END_OF_TABLE, 0x00, {}}
};

#define lcm_dcs_write_seq(ctx, seq...)                                         \
	({                                                                     \
		const u8 d[] = { seq };                                        \
		BUILD_BUG_ON_MSG(ARRAY_SIZE(d) > 64,                           \
				 "DCS sequence too big for stack");            \
		lcm_dcs_write(ctx, d, ARRAY_SIZE(d));                          \
	})

#define lcm_dcs_write_seq_static(ctx, seq...)                                  \
	({                                                                     \
		static const u8 d[] = { seq };                                 \
		lcm_dcs_write(ctx, d, ARRAY_SIZE(d));                          \
	})

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
		dev_err(ctx->dev, "error %zd writing seq: %ph\n", ret, data);
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
		dev_err(ctx->dev, "error %d reading dcs seq:(%#x)\n", ret, cmd);
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

	char bl_tb[] = {0x51, 0x07, 0xff};
	unsigned int level = 0;

	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(10 * 1000, 15 * 1000);
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(10 * 1000, 15 * 1000);	
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(10 * 1000, 15 * 1000);
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(10 * 1000, 15 * 1000);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

   
	lcm_dcs_write_seq_static(ctx,0xFE,0x40);
	lcm_dcs_write_seq_static(ctx,0x6F,0x00);
	lcm_dcs_write_seq_static(ctx,0x74,0x2C);
	lcm_dcs_write_seq_static(ctx,0xFE,0x72);
	lcm_dcs_write_seq_static(ctx,0xB9,0x23);	
	lcm_dcs_write_seq_static(ctx,0xBA,0x23);	
	lcm_dcs_write_seq_static(ctx,0xBB,0x22);	
	lcm_dcs_write_seq_static(ctx,0xBC,0x21);	
	lcm_dcs_write_seq_static(ctx,0xBD,0x20);	
	lcm_dcs_write_seq_static(ctx,0xBE,0x1F);	
	lcm_dcs_write_seq_static(ctx,0xBF,0x1E);	
	lcm_dcs_write_seq_static(ctx,0xC0,0x1D);	
	lcm_dcs_write_seq_static(ctx,0xC1,0x1C);	
	lcm_dcs_write_seq_static(ctx,0xC2,0x1B);	
	lcm_dcs_write_seq_static(ctx,0xC3,0x1A);	
	lcm_dcs_write_seq_static(ctx,0xC4,0x19);	
	lcm_dcs_write_seq_static(ctx,0xC5,0x18);	
	lcm_dcs_write_seq_static(ctx,0xC6,0x17);	
	lcm_dcs_write_seq_static(ctx,0xC7,0x16);	
	lcm_dcs_write_seq_static(ctx,0xC8,0x15);	
	lcm_dcs_write_seq_static(ctx,0xC9,0x14);


	lcm_dcs_write_seq_static(ctx,0xFE,0xA0);
	lcm_dcs_write_seq_static(ctx,0x06,0x36);
	lcm_dcs_write_seq_static(ctx,0xFE,0xD0);
	lcm_dcs_write_seq_static(ctx,0x11,0x75);
	lcm_dcs_write_seq_static(ctx,0x92,0x03);
	lcm_dcs_write_seq_static(ctx,0xFE,0x42);
	lcm_dcs_write_seq_static(ctx,0x68,0x02);
	lcm_dcs_write_seq_static(ctx,0xFE,0xD4);
	lcm_dcs_write_seq_static(ctx,0x40,0x03);
	lcm_dcs_write_seq_static(ctx,0xFE,0xFD);
	lcm_dcs_write_seq_static(ctx,0x80,0x06);
	lcm_dcs_write_seq_static(ctx,0x83,0x00);
	lcm_dcs_write_seq_static(ctx,0xFE,0xA1);
	lcm_dcs_write_seq_static(ctx,0xC3,0x87);
	lcm_dcs_write_seq_static(ctx,0xC4,0xFF);
	lcm_dcs_write_seq_static(ctx,0xC5,0x7F);
	



	lcm_dcs_write_seq_static(ctx,0xFE,0x00);
	lcm_dcs_write_seq_static(ctx,0x5E,0x01);
	lcm_dcs_write_seq_static(ctx,0xC2,0x08);
	lcm_dcs_write_seq_static(ctx,0x35,0x00);


	lcm_dcs_write_seq_static(ctx,0xFE,0x84);
	lcm_dcs_write_seq_static(ctx,0xE0,0x80);	
	lcm_dcs_write_seq_static(ctx,0xFE,0x00);
	lcm_dcs_write_seq_static(ctx,0xFA,0x01);	
	lcm_dcs_write_seq_static(ctx,0xFE,0xD0);
	lcm_dcs_write_seq_static(ctx,0x42,0x81);
	lcm_dcs_write_seq_static(ctx,0xFE,0x00);



    
    	//backlight
    	level = atomic_read(&current_backlight);
    	bl_tb[1] = (level >> 8) & 0x0F;
    	bl_tb[2] = level & 0xFF;
    	lcm_dcs_write(ctx, bl_tb, ARRAY_SIZE(bl_tb));
	
	lcm_dcs_write_seq_static(ctx,0x11,0x00);

	msleep(80);

	lcm_dcs_write_seq_static(ctx,0x29,0x00);

	pr_info("%s-\n", __func__);
}

static int lcm_disable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (!ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = false;

	return 0;
}

static int lcm_unprepare(struct drm_panel *panel)
{

	struct lcm *ctx = panel_to_lcm(panel);

	if (!ctx->prepared)
		return 0;
	lcm_dcs_write_seq_static(ctx, MIPI_DCS_SET_DISPLAY_OFF);
	msleep(20);
	lcm_dcs_write_seq_static(ctx, MIPI_DCS_ENTER_SLEEP_MODE);
	msleep(200);
	
#if 1	
	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(ctx->dev, "%s: cannot get reset_gpio %ld\n",
			__func__, PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	if(!tpgesture_status)
	gpiod_set_value(ctx->reset_gpio, 0);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	msleep(10);
	 
	
	
	ctx->vci_gpio = devm_gpiod_get(ctx->dev, "vci", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->vci_gpio)) {
		dev_err(ctx->dev, "%s: cannot get vci_gpio %ld\n",
			__func__, PTR_ERR(ctx->vci_gpio));
		return PTR_ERR(ctx->vci_gpio);
	}

	if(!tpgesture_status)
	gpiod_set_value(ctx->vci_gpio, 0);
	devm_gpiod_put(ctx->dev, ctx->vci_gpio);
	msleep(10);	
	ctx->dvdd_gpio =
		devm_gpiod_get(ctx->dev, "dvdd", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->dvdd_gpio)) {
		dev_err(ctx->dev, "%s: cannot get dvdd_gpio %ld\n",
			__func__, PTR_ERR(ctx->dvdd_gpio));
		return PTR_ERR(ctx->dvdd_gpio);
	}

	if(!tpgesture_status)
	gpiod_set_value(ctx->dvdd_gpio, 0);
	devm_gpiod_put(ctx->dev, ctx->dvdd_gpio);
	ctx->error = 0;
	ctx->prepared = false;
		
#endif


	return 0;
}

static int lcm_prepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int ret;

	pr_info("%s+\n", __func__);
	if (ctx->prepared)
		return 0;
	#if 0
	ctx->powerdm_gpio =
		devm_gpiod_get(ctx->dev, "powerdm", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->powerdm_gpio)) {
		dev_err(ctx->dev, "%s: cannot get powerdm_gpio %ld\n",
			__func__, PTR_ERR(ctx->powerdm_gpio));
		return PTR_ERR(ctx->powerdm_gpio);
	}

	gpiod_set_value(ctx->powerdm_gpio, 1);
	devm_gpiod_put(ctx->dev, ctx->powerdm_gpio);
	msleep(10);
	#endif
	ctx->dvdd_gpio =
		devm_gpiod_get(ctx->dev, "dvdd", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->dvdd_gpio)) {
		dev_err(ctx->dev, "%s: cannot get dvdd_gpio %ld\n",
			__func__, PTR_ERR(ctx->dvdd_gpio));
		return PTR_ERR(ctx->dvdd_gpio);
	}

	gpiod_set_value(ctx->dvdd_gpio, 1);
	devm_gpiod_put(ctx->dev, ctx->dvdd_gpio);

	msleep(10);

	ctx->vci_gpio = devm_gpiod_get(ctx->dev, "vci", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->vci_gpio)) {
		dev_err(ctx->dev, "%s: cannot get vci_gpio %ld\n",
			__func__, PTR_ERR(ctx->vci_gpio));
		return PTR_ERR(ctx->vci_gpio);
	}

	gpiod_set_value(ctx->vci_gpio, 1);
	devm_gpiod_put(ctx->dev, ctx->vci_gpio);



	lcm_panel_init(ctx);

	ret = ctx->error;
	if (ret < 0)
		lcm_unprepare(panel);

	ctx->prepared = true;

#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_tch_rst(panel);
#endif
#ifdef PANEL_SUPPORT_READBACK
	lcm_panel_get_data(ctx);
#endif

	pr_info("%s-\n", __func__);
	return ret;
}

static int lcm_enable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = true;

	return 0;
}


static int sethbm_cmdq(struct drm_panel *panel, void *dsi, dcs_write_gce cb, void *handle,
				 bool en)
{
	char bl_tb0[] = {0x51, 0x07, 0xFF};

	pr_info("%s,benter:%d+\n", __func__, en);

	if (!cb)
		return -1;

	if(en){
		bl_tb0[1] = 0x0f;
		bl_tb0[2] = 0xff;
		cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));
	}else{
		bl_tb0[1] = 0x03;
		bl_tb0[2] = 0xff;
		cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));
	}
	return 0;
}


#define HFP (26)
#define HSA (2)
#define HBP (36)
#define VSA (8)
#define VBP (12)
#define VFP (12)
#define VFP2 (12)
#define HAC (1080)
#define VAC (2400)
#define PLL_CLOCK (550)
#define PLL_CLOCK_90HZ (550)
#if 1
#define PCLK_IN_KHZ \
    ((HAC+HFP+HSA+HBP)*(VAC+VFP+VSA+VBP)*(60)/1000) 
#define PCLK2_IN_KHZ \
	((HAC+HFP+HSA+HBP)*(VAC+VFP2+VSA+VBP)*(90)/1000)
#define PCLK3_IN_KHZ \
    ((HAC+HFP+HSA+HBP)*(VAC+VFP3+VSA+VBP)*(120)/1000) 
#endif

static const struct drm_display_mode default_mode = {
	.clock = PCLK_IN_KHZ,
	.hdisplay	= HAC,
	.hsync_start	= HAC + HFP,
	.hsync_end	= HAC + HFP + HSA,
	.htotal		= HAC + HFP + HSA + HBP,//1150
	.vdisplay	= VAC,
	.vsync_start	= VAC + VFP,
	.vsync_end	= VAC + VFP + VSA,
	.vtotal		= VAC + VFP + VSA + VBP,//2430:disabel dsc 4900 enable dsc
	.vrefresh = 60,
};

static const struct drm_display_mode performance_mode = {
	.clock = PCLK2_IN_KHZ,
	.hdisplay	= HAC,
	.hsync_start	= HAC + HFP,
	.hsync_end	= HAC + HFP + HSA,
	.htotal		= HAC + HFP + HSA + HBP,//1150
	.vdisplay	= VAC,
	.vsync_start	= VAC + VFP2,
	.vsync_end	= VAC + VFP2 + VSA,
	.vtotal		= VAC + VFP2 + VSA + VBP,//2430
	.vrefresh = 90,
};

#if defined(CONFIG_MTK_PANEL_EXT)
static struct mtk_panel_params ext_params = {
	.data_rate = PLL_CLOCK * 2,
	.pll_clk = PLL_CLOCK,
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0A,
		.count = 1,
		.para_list[0] = 0x9C,
	},
	.lcm_esd_check_table[1] = {
		.cmd = 0x53,
		.count = 1,
		.para_list[0] = 0x20,
	},
	.lcm_esd_check_table[2] = {
		.cmd = 0xFA,
		.count = 1,
		.para_list[0] = 0x01,
	},

#if ENABLE_DSC
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.dsc_params = {
		.enable = 1,
		.ver = 11,
		.slice_mode =0,
		.rgb_swap = 0,
		.dsc_cfg = 0x828,
		.rct_on = 1,
		.bit_per_channel = 8,
		.dsc_line_buf_depth = 9,
		.bp_enable = 1,//1
		.bit_per_pixel = 128,//128
		.pic_height = 2400,
		.pic_width = 1080,
		.slice_height = 12,
		.slice_width = 1080,
		.chunk_size = 1080,
		.xmit_delay = 512,//512
		.dec_delay = 897,
		.scale_value = 32,
		.increment_interval = 382,//246
		.decrement_interval = 15,
		.line_bpg_offset = 12,
		.nfl_bpg_offset = 2235,//3511
		.slice_bpg_offset = 1085,//1628
		.initial_offset = 6144,
		.final_offset = 4336,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,
	},
#endif
    //.lp_perline_en = 1,
    .dyn_fps = {
        .switch_en = 1, .vact_timing_fps = 60,
    },

};
static struct mtk_panel_params ext_params_90hz = {
	.data_rate = PLL_CLOCK_90HZ * 2,
	.pll_clk = PLL_CLOCK_90HZ,
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0A,
		.count = 1,
		.para_list[0] = 0x9C,
	},
	.lcm_esd_check_table[1] = {
		.cmd = 0x53,
		.count = 1,
		.para_list[0] = 0x20,
	},
	.lcm_esd_check_table[2] = {
		.cmd = 0xFA,
		.count = 1,
		.para_list[0] = 0x01,
	},

#if ENABLE_DSC
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.dsc_params = {
		.enable = 1,
		.ver = 11,
		.slice_mode =0,
		.rgb_swap = 0,
		.dsc_cfg = 0x828,
		.rct_on = 1,
		.bit_per_channel = 8,
		.dsc_line_buf_depth = 9,
		.bp_enable = 1,//1
		.bit_per_pixel = 128,//128
		.pic_height = 2400,
		.pic_width = 1080,
		.slice_height = 12,
		.slice_width = 1080,
		.chunk_size = 1080,
		.xmit_delay = 512,//512
		.dec_delay = 897,
		.scale_value = 32,
		.increment_interval = 382,//246
		.decrement_interval = 15,
		.line_bpg_offset = 12,
		.nfl_bpg_offset = 2235,//3511
		.slice_bpg_offset = 1085,//1628
		.initial_offset = 6144,
		.final_offset = 4336,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,
	},
#endif
    //.lp_perline_en = 1,
    .dyn_fps = {
        .switch_en = 1, .vact_timing_fps = 90,
    },

};

static int panel_ata_check(struct drm_panel *panel)
{
	/* Customer test by own ATA tool */
	return 1;
}

static int lcm_setbacklight_cmdq(void *dsi, dcs_write_gce cb, void *handle,
				 unsigned int level)
{
	
	char bl_tb[] = {0x51, 0xf, 0xff};
	if(aod_state){
		level *= AOD_BACKLIGHT_RATIO;
	}
	bl_tb[1] = (level >> 8) & 0x0F;
	bl_tb[2] = level & 0xFF;
	if (!cb)
		return -1;
	cb(dsi, handle, bl_tb, ARRAY_SIZE(bl_tb));
	atomic_set(&current_backlight, level);
	pr_info("%s %d %d %d\n", __func__, level, bl_tb[1], bl_tb[2]);
	return 0;

}

static int panel_ext_reset(struct drm_panel *panel, int on)
{
	struct lcm *ctx = panel_to_lcm(panel);

	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(ctx->dev, "%s: cannot get reset_gpio %ld\n",
			__func__, PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	gpiod_set_value(ctx->reset_gpio, on);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	return 0;
}



struct drm_display_mode *get_mode_by_id(struct drm_panel *panel,
	unsigned int mode)
{
	struct drm_display_mode *m;
	unsigned int i = 0;

	list_for_each_entry(m, &panel->connector->modes, head) {
		if (i == mode)
			return m;
		i++;
	}
	return NULL;
}


static int mtk_panel_ext_param_set(struct drm_panel *panel,
			 unsigned int mode)
{
	struct mtk_panel_ext *ext = find_panel_ext(panel);
	int ret = 0;

	if (mode == 0)
		ext->params = &ext_params;
	else if (mode == 1)
		ext->params = &ext_params_90hz;
	else
		ret = 1;

	ext->params = &ext_params;
	return ret;
}

static int mtk_panel_ext_param_get(struct mtk_panel_params *ext_para,
			 unsigned int mode)
{
	int ret = 0;

	if (mode == 0)
		ext_para = &ext_params;
	else if (mode == 1)
		ext_para = &ext_params_90hz;
	else
		ret = 1;

	ext_para = &ext_params;
	return ret;

}

static void mode_switch_to_90(struct drm_panel *panel,
	enum MTK_PANEL_MODE_SWITCH_STAGE stage)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (stage == BEFORE_DSI_POWERDOWN) {
		/* display on switch to 90hz */
		lcm_dcs_write_seq_static(ctx, 0xFE,0x84);
		lcm_dcs_write_seq_static(ctx, 0xE0,0x80);
		lcm_dcs_write_seq_static(ctx, 0xFE,0x00);
	} 
}

static void mode_switch_to_60(struct drm_panel *panel,
	enum MTK_PANEL_MODE_SWITCH_STAGE stage)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (stage == BEFORE_DSI_POWERDOWN) {
		/* display on switch to 60hz */
		lcm_dcs_write_seq_static(ctx, 0xFE,0x84);
		lcm_dcs_write_seq_static(ctx, 0xE0,0x90);
		lcm_dcs_write_seq_static(ctx, 0xFE,0x00);
	} 
}

static int mode_switch(struct drm_panel *panel, unsigned int cur_mode,
		unsigned int dst_mode, enum MTK_PANEL_MODE_SWITCH_STAGE stage)
{
	int ret = 0;
	struct drm_display_mode *m = get_mode_by_id(panel, dst_mode);

	if (cur_mode == dst_mode)
		return ret;

	if (drm_mode_vrefresh(m) == 60) { /*switch to 60 */
		mode_switch_to_60(panel, stage);
		current_fps = 60;
	} else if (drm_mode_vrefresh(m) == 90) { /*switch to 90 */
		mode_switch_to_90(panel, stage);
		current_fps = 90;
	} else
		ret = 1;

	return ret;
}


static int panel_doze_disable(struct drm_panel *panel, void *dsi, dcs_write_gce cb, void *handle)
{
	unsigned int i=0;
	pr_info("doze %s.\n", __func__);

	/* Switch back to normal mode */
	for (i = 0; i < (sizeof(lcm_aod_to_normal) / sizeof(struct LCM_setting_table)); i++) {
		unsigned cmd;
		cmd = lcm_aod_to_normal[i].cmd;

		switch (cmd) {

			case REGFLAG_DELAY:
					msleep(lcm_aod_to_normal[i].count);
				break;

			case REGFLAG_UDELAY:
				udelay(lcm_aod_to_normal[i].count);
				break;

			case REGFLAG_END_OF_TABLE:
				break;

			default:
				cb(dsi, handle, lcm_aod_to_normal[i].para_list, lcm_aod_to_normal[i].count);
		}
	}



	aod_state = false;

	return 0;
}

static int panel_doze_enable(struct drm_panel *panel, void *dsi, dcs_write_gce cb, void *handle)
{
	unsigned int i=0;
	pr_info("doze %s\n", __func__);
	aod_state = true;

	for (i = 0; i < (sizeof(lcm_normal_to_aod) / sizeof(struct LCM_setting_table)); i++) {
		unsigned cmd;
		cmd = lcm_normal_to_aod[i].cmd;

		switch (cmd) {

			case REGFLAG_DELAY:
				msleep(lcm_normal_to_aod[i].count);
				break;

			case REGFLAG_UDELAY:
				udelay(lcm_normal_to_aod[i].count);
				break;

			case REGFLAG_END_OF_TABLE:
				break;

			default:
				cb(dsi, handle, lcm_normal_to_aod[i].para_list, lcm_normal_to_aod[i].count);
		}
	}

	return 0;
}

static int panel_set_aod_light_mode(void *dsi, dcs_write_gce cb, void *handle, unsigned int level)
{
	int i = 0;

	pr_info("[aod] %s : %d !\n",__func__, level);
	if (level == 0) {
		for (i = 0; i < sizeof(lcm_aod_high_mode)/sizeof(struct LCM_setting_table); i++){
			cb(dsi, handle, lcm_aod_high_mode[i].para_list, lcm_aod_high_mode[i].count);
		}
		
	} else if (level == 1) {
		for (i = 0; i < sizeof(lcm_aod_mid_mode)/sizeof(struct LCM_setting_table); i++){
			cb(dsi, handle, lcm_aod_mid_mode[i].para_list, lcm_aod_mid_mode[i].count);
		}		
		
	} else {
		for (i = 0; i < sizeof(lcm_aod_low_mode)/sizeof(struct LCM_setting_table); i++){
			cb(dsi, handle, lcm_aod_low_mode[i].para_list, lcm_aod_low_mode[i].count);
		}
	}


	return 0;
}



static int lcm_get_virtual_heigh(void)
{
	return VAC;
}

static int lcm_get_virtual_width(void)
{
	return HAC;
}
static struct mtk_panel_funcs ext_funcs = {
	.reset = panel_ext_reset,
	.set_backlight_cmdq = lcm_setbacklight_cmdq,
	.ata_check = panel_ata_check,
	.ext_param_set = mtk_panel_ext_param_set,
	.ext_param_get = mtk_panel_ext_param_get,
	.doze_enable = panel_doze_enable,
	.doze_disable = panel_doze_disable,
	.set_aod_light_mode = panel_set_aod_light_mode,
	.mode_switch = mode_switch,
	.get_virtual_heigh = lcm_get_virtual_heigh,
	.get_virtual_width = lcm_get_virtual_width,
	.hbm_set_cmdq = sethbm_cmdq,
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
	struct drm_display_mode *mode2;

	mode = drm_mode_duplicate(panel->drm, &default_mode);
	if (!mode) {
		dev_err(panel->drm->dev, "failed to add mode %ux%ux@%u\n",
			default_mode.hdisplay, default_mode.vdisplay,
			default_mode.vrefresh);
		return -ENOMEM;
	}

	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(panel->connector, mode);

	mode2 = drm_mode_duplicate(panel->drm, &performance_mode);
	if (!mode2) {
		dev_err(panel->drm->dev, "failed to add mode %ux%ux@%u\n",
			performance_mode.hdisplay, performance_mode.vdisplay,
			performance_mode.vrefresh);
	}

	drm_mode_set_name(mode2);
	mode2->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(panel->connector, mode2);

	panel->connector->display_info.width_mm = 64;
	panel->connector->display_info.height_mm = 129;

#if IS_ENABLED(CONFIG_CUST_DEVICE_INFO_SUPPORT)
	pr_info("The lcm is used\n");
	up_set_lcm_device_used("panel-boe-rm692f0-dphy-cmd",DEVICE_USED);
#endif

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
	struct device_node *dsi_node, *remote_node = NULL, *endpoint = NULL;
	struct lcm *ctx;
	struct device_node *backlight;
	int ret;

	pr_info("%s+\n", __func__);

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

	pr_info("%s+\n", __func__);
	ctx = devm_kzalloc(dev, sizeof(struct lcm), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_EOT_PACKET
			 | MIPI_DSI_CLOCK_NON_CONTINUOUS;

	backlight = of_parse_phandle(dev->of_node, "backlight", 0);
	if (backlight) {
		ctx->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);

		if (!ctx->backlight)
			return -EPROBE_DEFER;
	}

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_info(dev, "cannot get reset-gpios %ld\n",
			 PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	atomic_set(&current_backlight, 2047);
	devm_gpiod_put(dev, ctx->reset_gpio);
#if 0
	ctx->powerdm_gpio =
		devm_gpiod_get(ctx->dev, "powerdm", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->powerdm_gpio)) {
		dev_err(ctx->dev, "%s: cannot get powerdm-gpio %ld\n",
			__func__, PTR_ERR(ctx->powerdm_gpio));
		return PTR_ERR(ctx->powerdm_gpio);
	}
	devm_gpiod_put(dev, ctx->powerdm_gpio);
#endif
	ctx->dvdd_gpio =
		devm_gpiod_get(ctx->dev, "dvdd", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->dvdd_gpio)) {
		dev_err(ctx->dev, "%s: cannot get dvdd-gpios %ld\n",
			__func__, PTR_ERR(ctx->dvdd_gpio));
		return PTR_ERR(ctx->dvdd_gpio);
	}
	devm_gpiod_put(dev, ctx->dvdd_gpio);

	ctx->vci_gpio = devm_gpiod_get(ctx->dev, "vci", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->vci_gpio)) {
		dev_err(ctx->dev, "cannot get vci-gpios %ld\n",
			 PTR_ERR(ctx->vci_gpio));
		return PTR_ERR(ctx->vci_gpio);
	}
	devm_gpiod_put(dev, ctx->vci_gpio);

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
	ret = mtk_panel_ext_create(dev, &ext_params, &ext_funcs, &ctx->panel);
	if (ret < 0)
		return ret;

#endif

	pr_info("%s-\n", __func__);


	


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
	{
	    .compatible = "boe,rm692f0,cmd,120hz",
	},
	{ }
};

MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.driver = {
		.name = "panel-boe-rm692f0-dphy-cmd",
		.owner = THIS_MODULE,
		.of_match_table = lcm_of_match,
	},
};

module_mipi_dsi_driver(lcm_driver);

MODULE_AUTHOR("MEDIATEK");
MODULE_DESCRIPTION("boe rm692f0 AMOLED CMD LCD Panel Driver");
MODULE_LICENSE("GPL v2");
