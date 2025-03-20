/*****************************************************************************
 *
 * Filename:
 * ---------
 *	 s5kgd2smmipiraw_Sensor.c
 *
 ****************************************************************************/


#define PFX "S5KGD2SM_camera_sensor"
#define pr_fmt(fmt) PFX "[%s] " fmt, __func__

#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <asm/atomic.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>

#include "kd_camera_typedef.h"
#include "kd_imgsensor.h"
#include "kd_imgsensor_define.h"
#include "kd_imgsensor_errcode.h"

#include "s5kgd2smmipiraw_Sensor.h"

#define MULTI_WRITE 1

#if MULTI_WRITE
static const int I2C_BUFFER_LEN = 1020; //trans# max is 255, each 4 bytes
#else
static const int I2C_BUFFER_LEN = 4;
#endif
int up_otp_state_sub;
static int custom_status = 0;
static DEFINE_SPINLOCK(imgsensor_drv_lock);
static void sensor_init(void);

static struct imgsensor_info_struct imgsensor_info = {
	.sensor_id = S5KGD2SM_SENSOR_ID,
	.checksum_value = 0xd5fee28,

	.pre = {
		.pclk = 1600000000,
		.linelength = 17792,
		.framelength = 2992,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 3280,
		.grabwindow_height = 2464,
		.mipi_data_lp2hs_settle_dc = 85,
		.max_framerate = 300,
		.mipi_pixel_rate = 320000000,
	},
	.cap = {
		.pclk = 1600000000,
		.linelength = 17792,
		.framelength = 2992,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 3280,
		.grabwindow_height = 2464,
		.mipi_data_lp2hs_settle_dc = 85,
		.max_framerate = 300,
		.mipi_pixel_rate = 320000000,
	},

	.normal_video = {
		.pclk = 1600000000,
		.linelength = 17792,
		.framelength = 2992,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 3280,
		.grabwindow_height = 2464,
		.mipi_data_lp2hs_settle_dc = 85,
		.max_framerate = 300,
		.mipi_pixel_rate = 320000000,
	},
	.hs_video = {
		.pclk = 1600000000,
		.linelength = 4560,
		.framelength = 2916,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 3280,
		.grabwindow_height = 2464,
		.mipi_data_lp2hs_settle_dc = 85,
		.max_framerate = 1203,
		.mipi_pixel_rate = 1440000000,
	},
	.slim_video = {
		.pclk = 1600000000,
		.linelength = 17792,
		.framelength = 2992,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 3280,
		.grabwindow_height = 2464,
		.mipi_data_lp2hs_settle_dc = 85,
		.max_framerate = 300,
		.mipi_pixel_rate = 320000000,
	},
	.custom1 = {
		.pclk = 1600000000,
		.linelength = 10144,
		.framelength = 5182,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 6560,
		.grabwindow_height = 4936,
		.mipi_data_lp2hs_settle_dc = 85,
		.max_framerate = 304,
		.mipi_pixel_rate = 1296000000,

	},
	.margin = 100,	  //sensor framelength & shutter margin
	.min_shutter = 6, //min shutter
	.min_gain = 64,	  //1x gain
	.max_gain = 1024, //16x gain
	.min_gain_iso = 100,
	.exp_step = 2,
	.gain_step = 2,
	.gain_type = 2,

	/* max framelength by sensor register's limitation */
	.max_frame_length = 0xfff5,

	/* shutter delay frame for AE cycle,
	 * 2 frame with ispGain_delay-shut_delay=2-0=2
	 */
	.ae_shut_delay_frame = 0,

	/* sensor gain delay frame for AE cycle,
	 * 2 frame with ispGain_delay-sensor_gain_delay=2-0=2
	 */
	.ae_sensor_gain_delay_frame = 0,

	.ae_ispGain_delay_frame = 2,	/* isp gain delay frame for AE cycle */
	.ihdr_support = 0,	/* 1, support; 0,not support */
	.ihdr_le_firstline = 0,	/* 1,le first ; 0, se first */
	.temperature_support = 0,        // 1, support; 0,not support
	.sensor_mode_num = 6,	/* support sensor mode num */

	.cap_delay_frame = 2,		 //enter capture delay frame num
	.pre_delay_frame = 2,		 //enter preview delay frame num
	.video_delay_frame = 2,		 //enter video delay frame num

	/* enter high speed video  delay frame num */
	.hs_video_delay_frame = 2,	 //enter high speed video  delay frame num
	.slim_video_delay_frame = 2, //enter slim video delay frame num

	.frame_time_delay_frame = 2,

	.isp_driving_current = ISP_DRIVING_4MA,	/* mclk driving current */

	/* sensor_interface_type */
	.sensor_interface_type = SENSOR_INTERFACE_TYPE_MIPI,

	/* 0,MIPI_OPHY_NCSI2;  1,MIPI_OPHY_CSI2 */
	.mipi_sensor_type = MIPI_OPHY_NCSI2,

	/* 0,MIPI_SETTLEDELAY_AUTO; 1,MIPI_SETTLEDELAY_MANNUAL */
	.mipi_settle_delay_mode = 0,
	.sensor_output_dataformat = SENSOR_OUTPUT_FORMAT_RAW_4CELL_HW_BAYER_Gb,
	.mclk = 24,	/* mclk value, suggest 24 or 26 for 24Mhz or 26Mhz */
	.mipi_lane_num = SENSOR_MIPI_4_LANE,
	.i2c_speed = 1000, /*support 1MHz write*/
	/* record sensor support all write id addr,
	 * only supprt 4 must end with 0xff
	 */
	.i2c_addr_table = {0x20, 0x5a, 0xff},
};

static struct imgsensor_struct imgsensor = {
	.mirror = IMAGE_NORMAL,	/* mirrorflip information */

	/* IMGSENSOR_MODE enum value,record current sensor mode,such as:
	 * INIT, Preview, Capture, Video,High Speed Video, Slim Video
	 */
	.sensor_mode = IMGSENSOR_MODE_INIT,

	.shutter = 0x200,	/* current shutter */
	.gain = 0x200,		/* current gain */
	.dummy_pixel = 0,	/* current dummypixel */
	.dummy_line = 0,	/* current dummyline */
	.current_fps = 0,	/* full size current fps : 24fps for PIP,
				 * 30fps for Normal or ZSD
				 */

	/* auto flicker enable: KAL_FALSE for disable auto flicker,
	 * KAL_TRUE for enable auto flicker
	 */
	.autoflicker_en = KAL_FALSE,

		/* test pattern mode or not.
		 * KAL_FALSE for in test pattern mode,
		 * KAL_TRUE for normal output
		 */
	.test_pattern = KAL_FALSE,

	/* current scenario id */
	.current_scenario_id = MSDK_SCENARIO_ID_CAMERA_PREVIEW,

	/* sensor need support LE, SE with HDR feature */
	.ihdr_en = KAL_FALSE,	//sensor need support LE, SE with HDR feature
	.pdaf_mode = KAL_FALSE,
	.i2c_write_id = 0x20,	/* record current sensor's i2c write id */

	//Long exposure
	.current_ae_effective_frame = 1,

};

/* Sensor output window information */
static struct SENSOR_WINSIZE_INFO_STRUCT imgsensor_winsize_info[6] = {
	{ 6560, 4936,    0,    4, 6560, 4928, 3280, 2464, 0,    0, 3280, 2464,    0,    0, 3280, 2464},//pre
	{ 6560, 4936,    0,    4, 6560, 4928, 3280, 2464, 0,    0, 3280, 2464,    0,    0, 3280, 2464},//cap
	{ 6560, 4936,    0,    4, 6560, 4928, 3280, 2464, 0,    0, 3280, 2464,    0,    0, 3280, 2464},//video
	{ 6560, 4936,    0,    4, 6560, 4928, 3280, 2464, 0,    0, 3280, 2464,    0,    0, 3280, 2464},
	{ 6560, 4936,    0,    4, 6560, 4928, 3280, 2464, 0,    0, 3280, 2464,    0,    0, 3280, 2464},
	{ 6560, 4936,    0,    0, 6560, 4936, 6560, 4936, 0,    0, 6560, 4936,    0,    0, 6560, 4936},/*custom1  */
};

static void write_cmos_sensor(kal_uint16 addr, kal_uint16 para)
{
	char pusendcmd[4] = {
		(char)(addr >> 8), (char)(addr & 0xFF),
		(char)(para >> 8), (char)(para & 0xFF) };
	iWriteRegI2C(pusendcmd, 4, imgsensor.i2c_write_id);
}

static kal_uint16 read_cmos_sensor_8(kal_uint16 addr)
{
	kal_uint16 get_byte = 0;
	char pusendcmd[2] = { (char)(addr >> 8), (char)(addr & 0xFF) };

	iReadRegI2C(pusendcmd, 2, (u8 *) &get_byte, 1, imgsensor.i2c_write_id);
	return get_byte;
}

static void write_cmos_sensor_8(kal_uint16 addr, kal_uint8 para)
{
	char pusendcmd[3] = {
		(char)(addr >> 8), (char)(addr & 0xFF), (char)(para & 0xFF) };

	iWriteRegI2C(pusendcmd, 3, imgsensor.i2c_write_id);
}

static void set_dummy(void)
{
	pr_debug("dummyline = %d, dummypixels = %d\n", imgsensor.dummy_line, imgsensor.dummy_pixel);
	write_cmos_sensor(0x0340, imgsensor.frame_length & 0xFFFF);
	write_cmos_sensor(0x0342, imgsensor.line_length & 0xFFFF);
}

static void set_max_framerate(UINT16 framerate, kal_bool min_framelength_en)
{

	kal_uint32 frame_length = imgsensor.frame_length;

	pr_debug("framerate = %d, min framelength should enable %d\n", framerate, min_framelength_en);

	frame_length = imgsensor.pclk / framerate * 10 / imgsensor.line_length;
	spin_lock(&imgsensor_drv_lock);
	imgsensor.frame_length = (frame_length > imgsensor.min_frame_length) ? frame_length : imgsensor.min_frame_length;
	imgsensor.dummy_line = imgsensor.frame_length - imgsensor.min_frame_length;

	if (imgsensor.frame_length > imgsensor_info.max_frame_length) {
		imgsensor.frame_length = imgsensor_info.max_frame_length;
		imgsensor.dummy_line = imgsensor.frame_length - imgsensor.min_frame_length;
	}

	if (min_framelength_en)
		imgsensor.min_frame_length = imgsensor.frame_length;
	spin_unlock(&imgsensor_drv_lock);
	set_dummy();
}

int bNeedSetNormalMode = 0;
static kal_uint32 streaming_control(kal_bool enable);
static void write_shutter(kal_uint32 shutter)
{
	unsigned long flags;
	kal_uint16 realtime_fps = 0;

	spin_lock_irqsave(&imgsensor_drv_lock, flags);
	imgsensor.shutter = shutter;
	spin_unlock_irqrestore(&imgsensor_drv_lock, flags);

	spin_lock(&imgsensor_drv_lock);
	if (shutter > imgsensor.min_frame_length - imgsensor_info.margin)
		imgsensor.frame_length = shutter + imgsensor_info.margin;
	else
		imgsensor.frame_length = imgsensor.min_frame_length;
	if (imgsensor.frame_length > imgsensor_info.max_frame_length)
		imgsensor.frame_length = imgsensor_info.max_frame_length;
	spin_unlock(&imgsensor_drv_lock);
	shutter = (shutter < imgsensor_info.min_shutter) ? imgsensor_info.min_shutter : shutter;

	if (imgsensor.autoflicker_en) {
		realtime_fps = imgsensor.pclk / imgsensor.line_length * 10 / imgsensor.frame_length;
		if (realtime_fps >= 297 && realtime_fps <= 305)
			set_max_framerate(296, 0);
		else if (realtime_fps >= 147 && realtime_fps <= 150)
			set_max_framerate(146, 0);
		else {
			/* Extend frame length */
			write_cmos_sensor(0x0340, imgsensor.frame_length & 0xFFFF);
		}
	} else {
		/* Extend frame length */
		write_cmos_sensor(0x0340, imgsensor.frame_length & 0xFFFF);
		pr_debug("(else)imgsensor.frame_length = %d\n", imgsensor.frame_length);
	}
     pr_debug("shutter = %d, max_frame_length = %d, margin = %d, bNeedSetNormalMode = %d",
		shutter, imgsensor_info.max_frame_length, imgsensor_info.margin, bNeedSetNormalMode);

	/* long exposure */
	if (shutter >= (imgsensor_info.max_frame_length - imgsensor_info.margin))
	{
		pr_debug("Enter long exposure mode");
		bNeedSetNormalMode = 1;

		streaming_control(KAL_FALSE);
		if (shutter <= 89928) //1s
		{
			pr_debug("1s");
			write_cmos_sensor(0x0340, 0x57DC);
			write_cmos_sensor(0x0202, 0x57D2);
			write_cmos_sensor(0x0702, 0x0200);
			write_cmos_sensor(0x0704, 0x0200);
		}
		else if (shutter <= 179856) //2s
		{
			pr_debug("2s");
			write_cmos_sensor(0x0340, 0x57DC);
			write_cmos_sensor(0x0202, 0x57D2);
			write_cmos_sensor(0x0702, 0x0300);
			write_cmos_sensor(0x0704, 0x0300);
		}
		else if (shutter <= 359712) //4s
		{
			pr_debug("4s");
			write_cmos_sensor(0x0340, 0x57DC);
			write_cmos_sensor(0x0202, 0x57D2);
			write_cmos_sensor(0x0702, 0x0400);
			write_cmos_sensor(0x0704, 0x0400);
		}
		else if (shutter <= 719424) //8s
		{
			pr_debug("8s");
			write_cmos_sensor(0x0340, 0x57DC);
			write_cmos_sensor(0x0202, 0x57D2);
			write_cmos_sensor(0x0702, 0x0500);
			write_cmos_sensor(0x0704, 0x0500);
		}
		else // 16
		{
			pr_debug("16s");
			write_cmos_sensor(0x0340, 0x57DC);
			write_cmos_sensor(0x0202, 0x57D2);
			write_cmos_sensor(0x0702, 0x0600);
			write_cmos_sensor(0x0704, 0x0600);
		}
		streaming_control(KAL_TRUE);

		/* Frame exposure mode customization for LE*/
		imgsensor.ae_frm_mode.frame_mode_1 = IMGSENSOR_AE_MODE_SE;
		imgsensor.ae_frm_mode.frame_mode_2 = IMGSENSOR_AE_MODE_SE;
		imgsensor.current_ae_effective_frame = 1;
	}
	else
	{
		if (bNeedSetNormalMode == 1)
		{
			pr_debug("Exit long exposure mode");

			streaming_control(KAL_FALSE);
			write_cmos_sensor(0x0340, imgsensor.frame_length & 0xFFFF);
			write_cmos_sensor(0X0202, shutter & 0xFFFF);
			write_cmos_sensor(0x0702, 0x0000); //**** shifter for shutter
			write_cmos_sensor(0x0704, 0x0000); //**** shifter for shutter
			streaming_control(KAL_TRUE);

			bNeedSetNormalMode = 0;
		}
		else
		{
			// Update Shutter in nomal case
			write_cmos_sensor(0X0202, shutter & 0xFFFF);
			imgsensor.current_ae_effective_frame = 1;
		}
	}
	pr_debug("shutter =%d, framelength =%d\n", shutter, imgsensor.frame_length);
}

/*************************************************************************
 * FUNCTION
 *	set_shutter
 *
 * DESCRIPTION
 *	This function set e-shutter of sensor to change exposure time.
 *
 * PARAMETERS
 *	iShutter : exposured lines
 *
 * RETURNS
 *	None
 *
 * GLOBALS AFFECTED
 *
 *************************************************************************/
static void set_shutter(kal_uint32 shutter)
{
	unsigned long flags;

	spin_lock_irqsave(&imgsensor_drv_lock, flags);
	imgsensor.shutter = shutter;
	spin_unlock_irqrestore(&imgsensor_drv_lock, flags);

	write_shutter(shutter);
}

static kal_uint16 gain2reg(const kal_uint16 gain)
{
	kal_uint16 reg_gain = 0x0;

	reg_gain = gain / 2;
	return (kal_uint16) reg_gain;
}

/*************************************************************************
 * FUNCTION
 *	set_shutter_frame_length
 *
 * DESCRIPTION
 *	for frame & 3A sync
 *
 *************************************************************************/
static void set_shutter_frame_length(kal_uint16 shutter,
				     kal_uint16 frame_length,
				     kal_bool auto_extend_en)
{
	unsigned long flags;
	kal_uint16 realtime_fps = 0;
	spin_lock_irqsave(&imgsensor_drv_lock, flags);
	imgsensor.shutter = shutter;
	spin_unlock_irqrestore(&imgsensor_drv_lock, flags);

	spin_lock(&imgsensor_drv_lock);
	if (shutter > imgsensor.min_frame_length - imgsensor_info.margin)
		imgsensor.frame_length = shutter + imgsensor_info.margin;
	else
		imgsensor.frame_length = imgsensor.min_frame_length;
	if (imgsensor.frame_length > imgsensor_info.max_frame_length)
		imgsensor.frame_length = imgsensor_info.max_frame_length;
	spin_unlock(&imgsensor_drv_lock);
	shutter = (shutter < imgsensor_info.min_shutter) ? imgsensor_info.min_shutter : shutter;
	shutter = (shutter > (imgsensor_info.max_frame_length - imgsensor_info.margin)) ? (imgsensor_info.max_frame_length - imgsensor_info.margin) : shutter;

	if (imgsensor.autoflicker_en) {
		realtime_fps = imgsensor.pclk / imgsensor.line_length * 10 / imgsensor.frame_length;
		if (realtime_fps >= 297 && realtime_fps <= 305)
			set_max_framerate(296, 0);
		else if (realtime_fps >= 147 && realtime_fps <= 150)
			set_max_framerate(146, 0);
		else {
		// Extend frame length
		write_cmos_sensor(0x0340, imgsensor.frame_length & 0xFFFF);
		}
	} else {
		// Extend frame length
		write_cmos_sensor(0x0340, imgsensor.frame_length & 0xFFFF);
	}

	// Update Shutter
	write_cmos_sensor(0X0202, shutter & 0xFFFF);
	pr_debug("Exit! shutter =%d, framelength =%d\n", shutter, imgsensor.frame_length);

}	/* set_shutter_frame_length */

/*************************************************************************
 * FUNCTION
 *	set_gain
 *
 * DESCRIPTION
 *	This function is to set global gain to sensor.
 *
 * PARAMETERS
 *	iGain : sensor global gain(base: 0x40)
 *
 * RETURNS
 *	the actually gain set to sensor.
 *
 * GLOBALS AFFECTED
 *
 *************************************************************************/
static kal_uint16 set_gain(kal_uint16 gain)
{
	kal_uint16 reg_gain;

	if(custom_status == 2)
	{
		gain = gain*4;
		pr_debug("capture remoasic set_gain %d\n", gain);
	}
	if (gain < BASEGAIN || gain > 16 * BASEGAIN) {
		pr_debug("Error gain setting");

		if (gain < BASEGAIN)
			gain = BASEGAIN;
		else if (gain > 16 * BASEGAIN)
			gain = 16 * BASEGAIN;
	}

	reg_gain = gain2reg(gain);
	spin_lock(&imgsensor_drv_lock);
	imgsensor.gain = reg_gain;
	spin_unlock(&imgsensor_drv_lock);
	pr_debug("gain = %d , reg_gain = 0x%x\n ", gain, reg_gain);

	write_cmos_sensor(0x0204, reg_gain);

	return gain;
}

static void set_mirror_flip(kal_uint8 image_mirror)
{
	kal_uint8 itemp;

	pr_debug("image_mirror = %d\n", image_mirror);
	itemp = read_cmos_sensor_8(0x0101);
	itemp &= ~0x03;

	switch (image_mirror) {
	case IMAGE_NORMAL:
		write_cmos_sensor_8(0x0101, itemp);
		break;
	case IMAGE_V_MIRROR:
		write_cmos_sensor_8(0x0101, itemp | 0x02);
		break;
	case IMAGE_H_MIRROR:
		write_cmos_sensor_8(0x0101, itemp | 0x01);
		break;
	case IMAGE_HV_MIRROR:
		write_cmos_sensor_8(0x0101, itemp | 0x03);
		break;
	}
}

static kal_uint32 streaming_control(kal_bool enable)
{
	int i = 0;
	int framecnt = 0;
	int isStreamOn = 0;

	pr_debug("streaming_enable(0=Sw Standby,1=streaming): %d\n", enable);
	if (enable)
	{
		for (i = 0; i < 1000; i++)
		{
			write_cmos_sensor(0x0100, 0X0103);
			isStreamOn = read_cmos_sensor_8(0x0100); /* waiting for sensor to  stop output  then  set the  setting */
			pr_debug("isStreamOn %d ", isStreamOn);

			if ((isStreamOn & 0x1) == 0x01)
			{
				return ERROR_NONE;
			}
			else
			{
				mdelay(1);
			}
		}
	}
	else
	{
		for (i = 0; i < 1000; i++)
		{
			write_cmos_sensor(0x0100, 0x0000);
			framecnt = read_cmos_sensor_8(0x0005);
			if ((framecnt & 0xff) == 0xFF)
			{
				pr_debug("StreamOff OK at framecnt=%d.\n", framecnt);
				break;
			}
			else
			{
				pr_debug("StreamOFF is not on, %d, i=%d", framecnt, i);
				mdelay(1);
			}
		}
	}
	return ERROR_NONE;
}


static kal_uint16 table_write_cmos_sensor(kal_uint16 *para, kal_uint32 len)
{
	char puSendCmd[I2C_BUFFER_LEN];
	kal_uint32 tosend, IDX;
	kal_uint16 addr = 0, addr_last = 0, data;

	tosend = 0;
	IDX = 0;

	while (len > IDX) {
		addr = para[IDX];
		{
			puSendCmd[tosend++] = (char)(addr >> 8);
			puSendCmd[tosend++] = (char)(addr & 0xFF);
			data = para[IDX + 1];
			puSendCmd[tosend++] = (char)(data >> 8);
			puSendCmd[tosend++] = (char)(data & 0xFF);
			IDX += 2;
			addr_last = addr;

		}

#if MULTI_WRITE
	if ((I2C_BUFFER_LEN - tosend) < 4 || IDX == len || addr != addr_last) {
		iBurstWriteReg_multi(puSendCmd, tosend,
			imgsensor.i2c_write_id, 4, imgsensor_info.i2c_speed);

			tosend = 0;
	}
#else
		iWriteRegI2CTiming(puSendCmd, 4,
			imgsensor.i2c_write_id, imgsensor_info.i2c_speed);

		tosend = 0;
#endif
	}
	return 0;
}


static kal_uint16 addr_data_pair_init_gd2sp[] = {
	//SWpage
	0x6028, 0x2001,
	0x602A, 0xD604,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0348,
	0x6F12, 0x90F8,
	0x6F12, 0xDA0C,
	0x6F12, 0x0128,
	0x6F12, 0x01D0,
	0x6F12, 0x00F0,
	0x6F12, 0x09B8,
	0x6F12, 0x7047,
	0x6F12, 0x2000,
	0x6F12, 0x12E0,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0548,
	0x6F12, 0x0449,
	0x6F12, 0x054A,
	0x6F12, 0xC0F8,
	0x6F12, 0x8C17,
	0x6F12, 0x511A,
	0x6F12, 0xC0F8,
	0x6F12, 0x9017,
	0x6F12, 0x00F0,
	0x6F12, 0x2AB9,
	0x6F12, 0x2001,
	0x6F12, 0xD9E8,
	0x6F12, 0x2001,
	0x6F12, 0x8400,
	0x6F12, 0x2002,
	0x6F12, 0x3600,
	0x6F12, 0x2DE9,
	0x6F12, 0xF041,
	0x6F12, 0xA44D,
	0x6F12, 0x0646,
	0x6F12, 0xA44F,
	0x6F12, 0xAC89,
	0x6F12, 0x2007,
	0x6F12, 0x08D5,
	0x6F12, 0xB7F8,
	0x6F12, 0xFE08,
	0x6F12, 0x28B1,
	0x6F12, 0x0022,
	0x6F12, 0x0821,
	0x6F12, 0x46F2,
	0x6F12, 0x1420,
	0x6F12, 0x00F0,
	0x6F12, 0x53F9,
	0x6F12, 0x24F0,
	0x6F12, 0x0100,
	0x6F12, 0x9E4C,
	0x6F12, 0xA4F8,
	0x6F12, 0x1802,
	0x6F12, 0x4FF4,
	0x6F12, 0x8068,
	0x6F12, 0x0022,
	0x6F12, 0x4146,
	0x6F12, 0x46F2,
	0x6F12, 0x3020,
	0x6F12, 0x00F0,
	0x6F12, 0x46F9,
	0x6F12, 0x9748,
	0x6F12, 0x7830,
	0x6F12, 0x00F0,
	0x6F12, 0x47F9,
	0x6F12, 0x00B1,
	0x6F12, 0x3EB1,
	0x6F12, 0xE888,
	0x6F12, 0xA4F8,
	0x6F12, 0x0E02,
	0x6F12, 0x00F0,
	0x6F12, 0x45F9,
	0x6F12, 0x3864,
	0x6F12, 0xBDE8,
	0x6F12, 0xF081,
	0x6F12, 0x46F2,
	0x6F12, 0x0E24,
	0x6F12, 0x0022,
	0x6F12, 0x4146,
	0x6F12, 0x2046,
	0x6F12, 0x00F0,
	0x6F12, 0x31F9,
	0x6F12, 0x0120,
	0x6F12, 0x00F0,
	0x6F12, 0x3DF9,
	0x6F12, 0x0122,
	0x6F12, 0x8021,
	0x6F12, 0x2046,
	0x6F12, 0x00F0,
	0x6F12, 0x29F9,
	0x6F12, 0xEA88,
	0x6F12, 0x4FF6,
	0x6F12, 0xFF51,
	0x6F12, 0x2046,
	0x6F12, 0x00F0,
	0x6F12, 0x37F9,
	0x6F12, 0xE4E7,
	0x6F12, 0x2DE9,
	0x6F12, 0xF04F,
	0x6F12, 0x8648,
	0x6F12, 0x91B0,
	0x6F12, 0x0568,
	0x6F12, 0x95F8,
	0x6F12, 0x3C73,
	0x6F12, 0x05F5,
	0x6F12, 0x4F75,
	0x6F12, 0xF807,
	0x6F12, 0x78D0,
	0x6F12, 0x0220,
	0x6F12, 0x00EA,
	0x6F12, 0x5704,
	0x6F12, 0x6888,
	0x6F12, 0x1090,
	0x6F12, 0xB0F5,
	0x6F12, 0x806F,
	0x6F12, 0x01D3,
	0x6F12, 0x44F0,
	0x6F12, 0x1004,
	0x6F12, 0x002C,
	0x6F12, 0x60D1,
	0x6F12, 0xB807,
	0x6F12, 0x52D5,
	0x6F12, 0x7C49,
	0x6F12, 0xB1F8,
	0x6F12, 0x0A01,
	0x6F12, 0x38B1,
	0x6F12, 0x0128,
	0x6F12, 0x0AD0,
	0x6F12, 0x0228,
	0x6F12, 0x16D0,
	0x6F12, 0x54F0,
	0x6F12, 0x8004,
	0x6F12, 0x0AD0,
	0x6F12, 0x52E0,
	0x6F12, 0x4FF0,
	0x6F12, 0x000B,
	0x6F12, 0x4FF4,
	0x6F12, 0x8030,
	0x6F12, 0x03E0,
	0x6F12, 0xB1F8,
	0x6F12, 0x0CB1,
	0x6F12, 0xB1F8,
	0x6F12, 0x0E01,
	0x6F12, 0x8246,
	0x6F12, 0x6946,
	0x6F12, 0x1098,
	0x6F12, 0x00F0,
	0x6F12, 0x08F9,
	0x6F12, 0x4028,
	0x6F12, 0x07D0,
	0x6F12, 0x44F0,
	0x6F12, 0x0104,
	0x6F12, 0x3FE0,
	0x6F12, 0xB1F8,
	0x6F12, 0x10B1,
	0x6F12, 0xB1F8,
	0x6F12, 0x1201,
	0x6F12, 0xF0E7,
	0x6F12, 0xE946,
	0x6F12, 0x05F1,
	0x6F12, 0x0408,
	0x6F12, 0x0026,
	0x6F12, 0xD8F8,
	0x6F12, 0x0010,
	0x6F12, 0xC1B1,
	0x6F12, 0xD9F8,
	0x6F12, 0x0000,
	0x6F12, 0x20B1,
	0x6F12, 0x8842,
	0x6F12, 0x13D0,
	0x6F12, 0x44F0,
	0x6F12, 0x0404,
	0x6F12, 0x10E0,
	0x6F12, 0x1098,
	0x6F12, 0x06EB,
	0x6F12, 0x8010,
	0x6F12, 0x8345,
	0x6F12, 0x09D8,
	0x6F12, 0x5045,
	0x6F12, 0x07D2,
	0x6F12, 0x4246,
	0x6F12, 0x0421,
	0x6F12, 0x00F0,
	0x6F12, 0xE9F8,
	0x6F12, 0x20B9,
	0x6F12, 0x44F0,
	0x6F12, 0x4004,
	0x6F12, 0x01E0,
	0x6F12, 0x44F0,
	0x6F12, 0x2004,
	0x6F12, 0x361D,
	0x6F12, 0x09F1,
	0x6F12, 0x0409,
	0x6F12, 0x08F1,
	0x6F12, 0x0408,
	0x6F12, 0x402E,
	0x6F12, 0xDCD3,
	0x6F12, 0x3806,
	0x6F12, 0x11D5,
	0x6F12, 0x4022,
	0x6F12, 0x0021,
	0x6F12, 0x281D,
	0x6F12, 0x00F0,
	0x6F12, 0xDAF8,
	0x6F12, 0x0BE0,
	0x6F12, 0x291D,
	0x6F12, 0x1098,
	0x6F12, 0x00F0,
	0x6F12, 0xCBF8,
	0x6F12, 0x4028,
	0x6F12, 0x05D0,
	0x6F12, 0x4022,
	0x6F12, 0x0021,
	0x6F12, 0x281D,
	0x6F12, 0x00F0,
	0x6F12, 0xCEF8,
	0x6F12, 0xBCE7,
	0x6F12, 0x00F0,
	0x6F12, 0xD0F8,
	0x6F12, 0x08B1,
	0x6F12, 0x44F0,
	0x6F12, 0x0804,
	0x6F12, 0x0CB1,
	0x6F12, 0x47F0,
	0x6F12, 0x0407,
	0x6F12, 0x27F0,
	0x6F12, 0x0100,
	0x6F12, 0x6C70,
	0x6F12, 0x2870,
	0x6F12, 0x11B0,
	0x6F12, 0xBDE8,
	0x6F12, 0xF08F,
	0x6F12, 0x70B5,
	0x6F12, 0x0446,
	0x6F12, 0x4548,
	0x6F12, 0x0022,
	0x6F12, 0x8068,
	0x6F12, 0x86B2,
	0x6F12, 0x050C,
	0x6F12, 0x3146,
	0x6F12, 0x2846,
	0x6F12, 0x00F0,
	0x6F12, 0x90F8,
	0x6F12, 0x2046,
	0x6F12, 0x00F0,
	0x6F12, 0xBAF8,
	0x6F12, 0x0122,
	0x6F12, 0x3146,
	0x6F12, 0x2846,
	0x6F12, 0x00F0,
	0x6F12, 0x88F8,
	0x6F12, 0x2188,
	0x6F12, 0x4FF0,
	0x6F12, 0x8040,
	0x6F12, 0xB0FB,
	0x6F12, 0xF1F1,
	0x6F12, 0x01F5,
	0x6F12, 0x0071,
	0x6F12, 0x890A,
	0x6F12, 0xE180,
	0x6F12, 0x6188,
	0x6F12, 0xB0FB,
	0x6F12, 0xF1F1,
	0x6F12, 0x01F5,
	0x6F12, 0x0071,
	0x6F12, 0x890A,
	0x6F12, 0x2181,
	0x6F12, 0xA188,
	0x6F12, 0xB0FB,
	0x6F12, 0xF1F0,
	0x6F12, 0x00F5,
	0x6F12, 0x0070,
	0x6F12, 0x800A,
	0x6F12, 0x6081,
	0x6F12, 0x70BD,
	0x6F12, 0x2DE9,
	0x6F12, 0xF84F,
	0x6F12, 0x8246,
	0x6F12, 0x2F48,
	0x6F12, 0x8946,
	0x6F12, 0x9046,
	0x6F12, 0xC168,
	0x6F12, 0x1E46,
	0x6F12, 0x0D0C,
	0x6F12, 0x8FB2,
	0x6F12, 0x0A9C,
	0x6F12, 0x0022,
	0x6F12, 0x3946,
	0x6F12, 0x2846,
	0x6F12, 0x00F0,
	0x6F12, 0x60F8,
	0x6F12, 0x3346,
	0x6F12, 0x4246,
	0x6F12, 0x4946,
	0x6F12, 0x5046,
	0x6F12, 0x0094,
	0x6F12, 0x00F0,
	0x6F12, 0x8BF8,
	0x6F12, 0x00F0,
	0x6F12, 0x8EF8,
	0x6F12, 0x10B1,
	0x6F12, 0xA088,
	0x6F12, 0x401E,
	0x6F12, 0xA080,
	0x6F12, 0x3946,
	0x6F12, 0x2846,
	0x6F12, 0xBDE8,
	0x6F12, 0xF84F,
	0x6F12, 0x0122,
	0x6F12, 0x00F0,
	0x6F12, 0x4CB8,
	0x6F12, 0x10B5,
	0x6F12, 0x0721,
	0x6F12, 0x0220,
	0x6F12, 0x00F0,
	0x6F12, 0x83F8,
	0x6F12, 0xBDE8,
	0x6F12, 0x1040,
	0x6F12, 0x4FF6,
	0x6F12, 0xFF71,
	0x6F12, 0x0A20,
	0x6F12, 0x00F0,
	0x6F12, 0x7CB8,
	0x6F12, 0x70B5,
	0x6F12, 0x194C,
	0x6F12, 0x0022,
	0x6F12, 0xAFF2,
	0x6F12, 0x4F21,
	0x6F12, 0x236A,
	0x6F12, 0x1748,
	0x6F12, 0x9847,
	0x6F12, 0x144D,
	0x6F12, 0x0022,
	0x6F12, 0x2860,
	0x6F12, 0xAFF2,
	0x6F12, 0xDB11,
	0x6F12, 0x236A,
	0x6F12, 0x1448,
	0x6F12, 0x9847,
	0x6F12, 0x6860,
	0x6F12, 0x0022,
	0x6F12, 0xAFF2,
	0x6F12, 0xDD01,
	0x6F12, 0x236A,
	0x6F12, 0x1248,
	0x6F12, 0x9847,
	0x6F12, 0xA860,
	0x6F12, 0x0022,
	0x6F12, 0xAFF2,
	0x6F12, 0x9301,
	0x6F12, 0x236A,
	0x6F12, 0x0F48,
	0x6F12, 0x9847,
	0x6F12, 0xE860,
	0x6F12, 0x0022,
	0x6F12, 0xAFF2,
	0x6F12, 0x5B01,
	0x6F12, 0x236A,
	0x6F12, 0x0D48,
	0x6F12, 0x9847,
	0x6F12, 0x2861,
	0x6F12, 0x70BD,
	0x6F12, 0x0000,
	0x6F12, 0x2000,
	0x6F12, 0x12B0,
	0x6F12, 0x2001,
	0x6F12, 0x8400,
	0x6F12, 0x4000,
	0x6F12, 0x6000,
	0x6F12, 0x2000,
	0x6F12, 0x0C10,
	0x6F12, 0x2000,
	0x6F12, 0x12E0,
	0x6F12, 0x2001,
	0x6F12, 0xD9B0,
	0x6F12, 0x2000,
	0x6F12, 0x8A80,
	0x6F12, 0x0001,
	0x6F12, 0x9C21,
	0x6F12, 0x0001,
	0x6F12, 0x8907,
	0x6F12, 0x0000,
	0x6F12, 0x4305,
	0x6F12, 0x0000,
	0x6F12, 0x11C9,
	0x6F12, 0x0001,
	0x6F12, 0x7D6F,
	0x6F12, 0x4BF2,
	0x6F12, 0x390C,
	0x6F12, 0xC0F2,
	0x6F12, 0x010C,
	0x6F12, 0x6047,
	0x6F12, 0x48F6,
	0x6F12, 0x8B5C,
	0x6F12, 0xC0F2,
	0x6F12, 0x010C,
	0x6F12, 0x6047,
	0x6F12, 0x44F6,
	0x6F12, 0x9B6C,
	0x6F12, 0xC0F2,
	0x6F12, 0x010C,
	0x6F12, 0x6047,
	0x6F12, 0x4AF6,
	0x6F12, 0xCF4C,
	0x6F12, 0xC0F2,
	0x6F12, 0x010C,
	0x6F12, 0x6047,
	0x6F12, 0x4BF2,
	0x6F12, 0x550C,
	0x6F12, 0xC0F2,
	0x6F12, 0x010C,
	0x6F12, 0x6047,
	0x6F12, 0x48F6,
	0x6F12, 0xFF0C,
	0x6F12, 0xC0F2,
	0x6F12, 0x010C,
	0x6F12, 0x6047,
	0x6F12, 0x46F6,
	0x6F12, 0x2B2C,
	0x6F12, 0xC0F2,
	0x6F12, 0x020C,
	0x6F12, 0x6047,
	0x6F12, 0x4BF2,
	0x6F12, 0x633C,
	0x6F12, 0xC0F2,
	0x6F12, 0x010C,
	0x6F12, 0x6047,
	0x6F12, 0x47F2,
	0x6F12, 0x2D4C,
	0x6F12, 0xC0F2,
	0x6F12, 0x010C,
	0x6F12, 0x6047,
	0x6F12, 0x44F2,
	0x6F12, 0x053C,
	0x6F12, 0xC0F2,
	0x6F12, 0x000C,
	0x6F12, 0x6047,
	0x6F12, 0x41F2,
	0x6F12, 0xC91C,
	0x6F12, 0xC0F2,
	0x6F12, 0x000C,
	0x6F12, 0x6047,
	0x6F12, 0x49F2,
	0x6F12, 0x374C,
	0x6F12, 0xC0F2,
	0x6F12, 0x020C,
	0x6F12, 0x6047,
	0x6F12, 0x47F2,
	0x6F12, 0x032C,
	0x6F12, 0xC0F2,
	0x6F12, 0x010C,
	0x6F12, 0x6047,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0842,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x01B1,
	//Global
	0x6028, 0x2000,
	0x602A, 0x13BE,
	0x6F12, 0x0200,
	0x602A, 0x2A04,
	0x6F12, 0x5608,
	0x602A, 0x31EA,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x602A, 0x422A,
	0x6F12, 0x0008,
	0x6F12, 0x0808,
	0x6F12, 0x0808,
	0x6F12, 0x0808,
	0x6F12, 0x0801,
	0x6028, 0x2000,
	0x602A, 0x4208,
	0x6F12, 0x0004,
	0x6F12, 0x0C8D,
	0x602A, 0x219A,
	0x6F12, 0x6401,
	0x602A, 0x2B96,
	0x6F12, 0x0033,
	0x6F12, 0x0033,
	0x6F12, 0x0033,
	0x6F12, 0x0033,
	0x6F12, 0x0033,
	0x6F12, 0x0033,
	0x6F12, 0x0033,
	0x6F12, 0x0033,
	0x6F12, 0x0033,
	0x6F12, 0x0033,
	0x6F12, 0x0033,
	0x6F12, 0x0033,
	0x6F12, 0x0033,
	0x6F12, 0x0033,
	0x6F12, 0x0033,
	0x6F12, 0x0033,
	0x6F12, 0x0033,
	0x6F12, 0x0033,
	0x6F12, 0x0033,
	0x6F12, 0x0033,
	0x602A, 0x2BFC,
	0x6F12, 0x033F,
	0x6F12, 0x033F,
	0x6F12, 0x033F,
	0x6F12, 0x033F,
	0x6F12, 0x033F,
	0x6F12, 0x033F,
	0x6F12, 0x033F,
	0x6F12, 0x033F,
	0x6F12, 0x033F,
	0x6F12, 0x033F,
	0x6F12, 0x033F,
	0x6F12, 0x033F,
	0x6F12, 0x033F,
	0x6F12, 0x033F,
	0x6F12, 0x033F,
	0x6F12, 0x033F,
	0x6F12, 0x033F,
	0x6F12, 0x033F,
	0x6F12, 0x033F,
	0x6F12, 0x033F,
	0x602A, 0x31C8,
	0x6F12, 0x0101,
	0x602A, 0x2A00,
	0x6F12, 0x0100,
	0x6F12, 0x0480,
	0x602A, 0x29F8,
	0x6F12, 0x090F,
	0x602A, 0x29F2,
	0x6F12, 0x0305,
	0x602A, 0x29FA,
	0x6F12, 0x0203,
	0x602A, 0x5AC4,
	0x6F12, 0x0001,
	0x602A, 0x5B30,
	0x6F12, 0x0000,
	0x602A, 0x5B18,
	0x6F12, 0x0000,
	0x602A, 0x6CA6,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x602A, 0x6C96,
	0x6F12, 0x04B1,
	0x6F12, 0x04B1,
	0x6F12, 0x04B1,
	0x6F12, 0x04B1,
	0x6F12, 0x04B1,
	0x6F12, 0x04B1,
	0x602A, 0x6B16,
	0x6F12, 0x0021,
	0x6F12, 0x0021,
	0x6F12, 0x0021,
	0x6F12, 0x0021,
	0x6F12, 0x0021,
	0x6F12, 0x0021,
	0x602A, 0x6B06,
	0x6F12, 0x0015,
	0x6F12, 0x0015,
	0x6F12, 0x0015,
	0x6F12, 0x0015,
	0x6F12, 0x0015,
	0x6F12, 0x0015,
	0x602A, 0x6B36,
	0x6F12, 0x004C,
	0x6F12, 0x004C,
	0x6F12, 0x004C,
	0x6F12, 0x004C,
	0x6F12, 0x004C,
	0x6F12, 0x004C,
	0x602A, 0x6B26,
	0x6F12, 0x0001,
	0x6F12, 0x0001,
	0x6F12, 0x0001,
	0x6F12, 0x0001,
	0x6F12, 0x0001,
	0x6F12, 0x0001,
	0x602A, 0x6AF6,
	0x6F12, 0x06FA,
	0x6F12, 0x06FA,
	0x6F12, 0x06FA,
	0x6F12, 0x06FA,
	0x6F12, 0x06FA,
	0x6F12, 0x06FA,
	0x602A, 0x6AE6,
	0x6F12, 0x1753,
	0x6F12, 0x1753,
	0x6F12, 0x1753,
	0x6F12, 0x1753,
	0x6F12, 0x1753,
	0x6F12, 0x1753,
	0x602A, 0x6BD6,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x602A, 0x6BC6,
	0x6F12, 0x001B,
	0x6F12, 0x001B,
	0x6F12, 0x001B,
	0x6F12, 0x001B,
	0x6F12, 0x001B,
	0x6F12, 0x001B,
	0x602A, 0x6BF6,
	0x6F12, 0x0027,
	0x6F12, 0x0027,
	0x6F12, 0x0027,
	0x6F12, 0x0027,
	0x6F12, 0x0027,
	0x6F12, 0x0027,
	0x602A, 0x6BE6,
	0x6F12, 0x0075,
	0x6F12, 0x0075,
	0x6F12, 0x0075,
	0x6F12, 0x0075,
	0x6F12, 0x0075,
	0x6F12, 0x0075,
	0x602A, 0x6BB6,
	0x6F12, 0x0054,
	0x6F12, 0x0054,
	0x6F12, 0x0054,
	0x6F12, 0x0054,
	0x6F12, 0x0054,
	0x6F12, 0x0054,
	0x602A, 0x6BA6,
	0x6F12, 0x0579,
	0x6F12, 0x0579,
	0x6F12, 0x0579,
	0x6F12, 0x0579,
	0x6F12, 0x0579,
	0x6F12, 0x0579,
	0x602A, 0x6B76,
	0x6F12, 0x0010,
	0x6F12, 0x0010,
	0x6F12, 0x0010,
	0x6F12, 0x0010,
	0x6F12, 0x0010,
	0x6F12, 0x0010,
	0x602A, 0x6B66,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x602A, 0x6B96,
	0x6F12, 0x0001,
	0x6F12, 0x0001,
	0x6F12, 0x0001,
	0x6F12, 0x0001,
	0x6F12, 0x0001,
	0x6F12, 0x0001,
	0x602A, 0x6B86,
	0x6F12, 0x007D,
	0x6F12, 0x007D,
	0x6F12, 0x007D,
	0x6F12, 0x007D,
	0x6F12, 0x007D,
	0x6F12, 0x007D,
	0x602A, 0x6B56,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x602A, 0x6B46,
	0x6F12, 0x0082,
	0x6F12, 0x0082,
	0x6F12, 0x0082,
	0x6F12, 0x0082,
	0x6F12, 0x0082,
	0x6F12, 0x0082,
	0x602A, 0x6C36,
	0x6F12, 0x00B0,
	0x6F12, 0x00B0,
	0x6F12, 0x00B0,
	0x6F12, 0x00B0,
	0x6F12, 0x00B0,
	0x6F12, 0x00B0,
	0x602A, 0x6C46,
	0x6F12, 0x0025,
	0x6F12, 0x0025,
	0x6F12, 0x0025,
	0x6F12, 0x0025,
	0x6F12, 0x0025,
	0x6F12, 0x0025,
	0x602A, 0x5B3C,
	0x6F12, 0x0000,
	0x602A, 0x5B48,
	0x6F12, 0x0000,
	0x602A, 0x5B42,
	0x6F12, 0x012C,
	0x602A, 0x5B4E,
	0x6F12, 0x0000,
	0x602A, 0x5B5A,
	0x6F12, 0x0003,
	0x602A, 0x5B54,
	0x6F12, 0x0004,
	0x602A, 0x5B66,
	0x6F12, 0x0001,
	0x602A, 0x5B60,
	0x6F12, 0x0001,
	0x602A, 0x6C76,
	0x6F12, 0x0FFF,
	0x6F12, 0x0FFF,
	0x6F12, 0x0FFF,
	0x6F12, 0x0FFF,
	0x6F12, 0x0FFF,
	0x6F12, 0x0FFF,
	0x602A, 0x6C86,
	0x6F12, 0x0FFF,
	0x6F12, 0x0FFF,
	0x6F12, 0x0FFF,
	0x6F12, 0x0FFF,
	0x6F12, 0x0FFF,
	0x6F12, 0x0FFF,
	0x602A, 0x6C56,
	0x6F12, 0x0176,
	0x6F12, 0x0176,
	0x6F12, 0x0176,
	0x6F12, 0x0176,
	0x6F12, 0x0176,
	0x6F12, 0x0176,
	0x602A, 0x6C66,
	0x6F12, 0x0025,
	0x6F12, 0x0025,
	0x6F12, 0x0025,
	0x6F12, 0x0025,
	0x6F12, 0x0025,
	0x6F12, 0x0025,
	0x602A, 0x6AD6,
	0x6F12, 0x0190,
	0x6F12, 0x0190,
	0x6F12, 0x0190,
	0x6F12, 0x0190,
	0x6F12, 0x0190,
	0x6F12, 0x0190,
	0x602A, 0x5A40,
	0x6F12, 0x0001,
	0x602A, 0x5B36,
	0x6F12, 0x0000,
	0x602A, 0x5B78,
	0x6F12, 0x0000,
	0x602A, 0x5B7E,
	0x6F12, 0x0001,
	0x602A, 0x5B1E,
	0x6F12, 0x01B3,
	0x602A, 0x5B24,
	0x6F12, 0x0363,
	0x602A, 0x5B2A,
	0x6F12, 0x0007,
	0x602A, 0x5B0C,
	0x6F12, 0x0333,
	0x602A, 0x5B12,
	0x6F12, 0x0000,
	0x602A, 0x5B06,
	0x6F12, 0x0000,
	0x602A, 0x5AFA,
	0x6F12, 0x0000,
	0x602A, 0x5B00,
	0x6F12, 0x0000,
	0x602A, 0x5AF4,
	0x6F12, 0x00FC,
	0x602A, 0x6C16,
	0x6F12, 0x03EC,
	0x6F12, 0x03EC,
	0x6F12, 0x03EC,
	0x6F12, 0x03EC,
	0x6F12, 0x03EC,
	0x6F12, 0x03EC,
	0x602A, 0x5B72,
	0x6F12, 0x0007,
	0x602A, 0x5B8A,
	0x6F12, 0x0000,
	0x602A, 0x6C26,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x602A, 0x6C06,
	0x6F12, 0x035D,
	0x6F12, 0x035D,
	0x6F12, 0x035D,
	0x6F12, 0x035D,
	0x6F12, 0x035D,
	0x6F12, 0x035D,
	0x602A, 0x5AE8,
	0x6F12, 0x03FC,
	0x602A, 0x5AEE,
	0x6F12, 0x0000,
	0x602A, 0x5AE2,
	0x6F12, 0x012C,
	0x602A, 0x5AD6,
	0x6F12, 0x1984,
	0x602A, 0x5ADC,
	0x6F12, 0x3FFD,
	0x602A, 0x5AD0,
	0x6F12, 0x0035,
	0x602A, 0x5AB8,
	0x6F12, 0x014C,
	0x602A, 0x5ABE,
	0x6F12, 0x0001,
	0x602A, 0x5AB2,
	0x6F12, 0x0009,
	0x602A, 0x5AA6,
	0x6F12, 0x3FFE,
	0x602A, 0x5AAC,
	0x6F12, 0x00A2,
	0x602A, 0x5AA0,
	0x6F12, 0x184F,
	0x602A, 0x5A8E,
	0x6F12, 0x01D7,
	0x602A, 0x5A94,
	0x6F12, 0x03FF,
	0x602A, 0x5A88,
	0x6F12, 0x03FF,
	0x602A, 0x5A7C,
	0x6F12, 0x0002,
	0x602A, 0x5A82,
	0x6F12, 0x3FFE,
	0x602A, 0x5A76,
	0x6F12, 0x1ED7,
	0x602A, 0x5A64,
	0x6F12, 0x0004,
	0x602A, 0x5A6A,
	0x6F12, 0x0000,
	0x602A, 0x5A5E,
	0x6F12, 0x0000,
	0x602A, 0x5A52,
	0x6F12, 0x0000,
	0x602A, 0x5A58,
	0x6F12, 0x3E68,
	0x602A, 0x5A4C,
	0x6F12, 0x0000,
	0x602A, 0x5B84,
	0x6F12, 0x0000,
	0x602A, 0x5B72,
	0x6F12, 0x0000,
	0x602A, 0x2186,
	0x6F12, 0x0100,
	0x602A, 0x1310,
	0x6F12, 0x0F00,
	0x602A, 0x2188,
	0x6F12, 0x0650,
	0x602A, 0x218E,
	0x6F12, 0x0650,
	0x602A, 0x2194,
	0x6F12, 0x0100,
	0x602A, 0x3450,
	0x6F12, 0x193C,
	0x602A, 0x43C8,
	0x6F12, 0x0014,
	0x6F12, 0x000E,
	0x6F12, 0x003A,
	0x6F12, 0x0033,
	0x6F12, 0x0003,
	0x6F12, 0x0004,
	0x602A, 0x43DA,
	0x6F12, 0x19A0,
	0x6F12, 0x1348,
	0x6F12, 0x0138,
	0x6F12, 0x0140,
	0x6F12, 0x0110,
	0x6F12, 0x011C,
	0x6F12, 0x012C,
	0x6F12, 0x013C,
	0x6F12, 0x0150,
	0x6F12, 0x0168,
	0x6F12, 0x0180,
	0x6F12, 0x019C,
	0x6F12, 0x01BC,
	0x6F12, 0x01E4,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x012C,
	0x6F12, 0x0144,
	0x6F12, 0x0160,
	0x6F12, 0x0180,
	0x6F12, 0x01A8,
	0x6F12, 0x0214,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6028, 0x4000,
	0x0FE8, 0x4180
};

static void sensor_init(void)
{
	pr_debug("%s +\n", __func__);
	/* initial sequence */
	write_cmos_sensor(0x6028, 0x4000);
	write_cmos_sensor(0x0000, 0x0005);
	write_cmos_sensor(0x0000, 0x0842);
	write_cmos_sensor(0x001E, 0x0203);

	write_cmos_sensor(0x6028, 0x4000);
	write_cmos_sensor(0x6010, 0x0001);
	mdelay(4);
	write_cmos_sensor(0x6214, 0xFF7D);
	write_cmos_sensor(0x6218, 0x0000);
	mdelay(10);

	table_write_cmos_sensor(addr_data_pair_init_gd2sp,
		sizeof(addr_data_pair_init_gd2sp) / sizeof(kal_uint16));
	pr_debug("%s -\n", __func__);
}



static kal_uint16 addr_data_pair_pre_gd2sp[] = {
	0x6028, 0x2000,
	0x602A, 0x2A08,
	0x6F12, 0x0002,
	0x6F12, 0x49F0,
	0x602A, 0x21F8,
	0x6F12, 0x1358,
	0x602A, 0x30CE,
	0x6F12, 0x01E4,
	0x602A, 0x8A00,
	0x6F12, 0x0001,
	0x602A, 0x3770,
	0x6F12, 0x0100,
	0x602A, 0x220E,
	0x6F12, 0x0005,
	0x6F12, 0x0005,
	0x602A, 0x31CC,
	0x6F12, 0x0019,
	0x602A, 0x31D2,
	0x6F12, 0x0012,
	0x602A, 0x31CA,
	0x6F12, 0x000A,
	0x602A, 0x221C,
	0x6F12, 0x0101,
	0x602A, 0x2A18,
	0x6F12, 0x0801,
	0x602A, 0x5090,
	0x6F12, 0x0E00,
	0x602A, 0x84F8,
	0x6F12, 0x0002,
	0x602A, 0x8508,
	0x6F12, 0x0200,
	0x6F12, 0x0000,
	0x602A, 0x5530,
	0x6F12, 0x0100,
	0x602A, 0x43B0,
	0x6F12, 0x0101,
	0x602A, 0x43D4,
	0x6F12, 0x1300,
	0x6F12, 0x0004,
	0x6F12, 0x13E8,
	0x602A, 0x2A0E,
	0x6F12, 0x1FCD,
	0x602A, 0x1346,
	0x6F12, 0x0018,
	0x602A, 0x2308,
	0x6F12, 0x005B,
	0x602A, 0x381A,
	0x6F12, 0x0008,
	0x6F12, 0x0897,
	0x602A, 0x3812,
	0x6F12, 0x0001,
	0x602A, 0x345E,
	0x6F12, 0x1F40,
	0x6F12, 0x1F47,
	0x602A, 0x3B1A,
	0x6F12, 0x000F,
	0x6F12, 0x0000,
	0x6F12, 0x0007,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0007,
	0x6F12, 0x0000,
	0x6F12, 0x000F,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x602A, 0x3A9A,
	0x6F12, 0x0080,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0080,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x602A, 0x39EE,
	0x6F12, 0x0100,
	0x6F12, 0x0100,
	0x6F12, 0x0200,
	0x6F12, 0x1020,
	0x6F12, 0x1000,
	0x602A, 0x2A1E,
	0x6F12, 0x0108,
	0x602A, 0x134C,
	0x6F12, 0x0002,
	0x602A, 0x30B8,
	0x6F12, 0xD020,
	0x602A, 0x2580,
	0x6F12, 0x000E,
	0x602A, 0x2584,
	0x6F12, 0x0226,
	0x602A, 0x8A10,
	0x6F12, 0x0000,
	0x602A, 0x8A6E,
	0x6F12, 0x0FA0,
	0x6F12, 0x0FA7,
	0x602A, 0x8A20,
	0x6F12, 0x0008,
	0x602A, 0x8A26,
	0x6F12, 0x0008,
	0x602A, 0x8A1E,
	0x6F12, 0x0080,
	0x602A, 0x8A68,
	0x6F12, 0x0200,
	0x602A, 0x8A3E,
	0x6F12, 0x03FF,
	0x602A, 0x8A44,
	0x6F12, 0x0080,
	0x602A, 0x1370,
	0x6F12, 0x0000,
	0x602A, 0x1378,
	0x6F12, 0x0003,
	0x602A, 0x2134,
	0x6F12, 0xFFFF,
	0x602A, 0x1FC0,
	0x6F12, 0x0100,
	0x6F12, 0x005F,
	0x602A, 0x1FC6,
	0x6F12, 0x0060,
	0x602A, 0x1FCC,
	0x6F12, 0x1C80,
	0x6F12, 0x1C82,
	0x6F12, 0x4000,
	0x6F12, 0xF4B2,
	0x6028, 0x4000,
	0xF484, 0x0080,
	0xF498, 0x0343,
	0xF496, 0x0200,
	0xF4B2, 0x1C82,
	0xF41C, 0x4116,
	0xF418, 0x001F,
	0xB606, 0x0400,
	0xF482, 0x800D,
	0x6028, 0x4000,
	0x623C, 0x0080,
	0x623E, 0x0000,
	0x6240, 0x0000,
	0xF4A6, 0x0016,
	0x0344, 0x0008,
	0x0346, 0x0008,
	0x0348, 0x19A7,
	0x034A, 0x134F,
	0x034C, 0x0CD0,
	0x034E, 0x09A0,
	0x0350, 0x0000,
	0x0352, 0x0002,
	0x0900, 0x2222,
	0x0380, 0x0002,
	0x0382, 0x0002,
	0x0384, 0x0002,
	0x0386, 0x0002,
	0x0400, 0x1010,
	0x0408, 0x0100,
	0x040A, 0x0100,
	0x040C, 0x0000,
	0x0110, 0x1002,
	0x0114, 0x0300,
	0x0116, 0x3000,
	0x0118, 0x0001,
	0x011A, 0x0000,
	0x011C, 0x0000,
	0x0136, 0x1800,
	0x013E, 0x0000,
	0x0300, 0x0007,
	0x0302, 0x0001,
	0x0304, 0x0003,
	0x0306, 0x00AF,
	0x0308, 0x000B,
	0x030A, 0x0001,
	0x030C, 0x0000,
	0x030E, 0x0003,
	0x0310, 0x00C8,
	0x0312, 0x0002,
	0x0340, 0x0BB0,
	0x0342, 0x4580,
	0x0702, 0x0000,
	0x0202, 0x0100,
	0x0200, 0x0100,
	0x022C, 0x0100,
	0x0226, 0x0100,
	0x021E, 0x0000,
	0x080E, 0x0000,
	0x0B00, 0x0080,
	0x0B08, 0x0100,
	0x0D00, 0x0000
};


static void preview_setting(void)
{
	pr_debug("%s +\n", __func__);
	/* preview sequence */
	table_write_cmos_sensor(addr_data_pair_pre_gd2sp,
			sizeof(addr_data_pair_pre_gd2sp) / sizeof(kal_uint16));
	pr_debug("%s -\n", __func__);
}



static kal_uint16 addr_data_pair_custom1_gd2sp[] = {
	0x6028, 0x2000,
	0x602A, 0x2A08,
	0x6F12, 0x0002,
	0x6F12, 0x49F0,
	0x602A, 0x21F8,
	0x6F12, 0x1358,
	0x602A, 0x30CE,
	0x6F12, 0x01E4,
	0x602A, 0x8A00,
	0x6F12, 0x0001,
	0x602A, 0x3770,
	0x6F12, 0x0100,
	0x602A, 0x220E,
	0x6F12, 0x0009,
	0x6F12, 0x0009,
	0x602A, 0x31CC,
	0x6F12, 0x0019,
	0x602A, 0x31D2,
	0x6F12, 0x0012,
	0x602A, 0x31CA,
	0x6F12, 0x000A,
	0x602A, 0x221C,
	0x6F12, 0x0100,
	0x602A, 0x2A18,
	0x6F12, 0x0801,
	0x602A, 0x5090,
	0x6F12, 0x0600,
	0x602A, 0x84F8,
	0x6F12, 0x0002,
	0x602A, 0x8508,
	0x6F12, 0x0200,
	0x6F12, 0x0000,
	0x602A, 0x5530,
	0x6F12, 0x0000,
	0x602A, 0x43B0,
	0x6F12, 0x0101,
	0x602A, 0x43D4,
	0x6F12, 0x1300,
	0x6F12, 0x0004,
	0x6F12, 0x13E8,
	0x602A, 0x2A0E,
	0x6F12, 0x1FCD,
	0x602A, 0x1346,
	0x6F12, 0x0018,
	0x602A, 0x2308,
	0x6F12, 0x005B,
	0x602A, 0x381A,
	0x6F12, 0x0000,
	0x6F12, 0x3FFF,
	0x602A, 0x3812,
	0x6F12, 0x0003,
	0x602A, 0x345E,
	0x6F12, 0x1F38,
	0x6F12, 0x1F3F,
	0x602A, 0x3B1A,
	0x6F12, 0x004F,
	0x6F12, 0x004F,
	0x6F12, 0x0050,
	0x6F12, 0x0050,
	0x6F12, 0x004F,
	0x6F12, 0x004F,
	0x6F12, 0x0050,
	0x6F12, 0x0050,
	0x6F12, 0x0050,
	0x6F12, 0x0050,
	0x6F12, 0x004F,
	0x6F12, 0x004F,
	0x6F12, 0x0050,
	0x6F12, 0x0050,
	0x6F12, 0x004F,
	0x6F12, 0x004F,
	0x602A, 0x3A9A,
	0x6F12, 0x0040,
	0x6F12, 0x0040,
	0x6F12, 0xFFE0,
	0x6F12, 0xFFE0,
	0x6F12, 0x0040,
	0x6F12, 0x0040,
	0x6F12, 0xFFE0,
	0x6F12, 0xFFE0,
	0x6F12, 0xFFE0,
	0x6F12, 0xFFE0,
	0x6F12, 0x0040,
	0x6F12, 0x0040,
	0x6F12, 0xFFE0,
	0x6F12, 0xFFE0,
	0x6F12, 0x0040,
	0x6F12, 0x0040,
	0x602A, 0x39EE,
	0x6F12, 0x0100,
	0x6F12, 0x0100,
	0x6F12, 0x0200,
	0x6F12, 0x1020,
	0x6F12, 0x1000,
	0x602A, 0x2A1E,
	0x6F12, 0x0108,
	0x602A, 0x134C,
	0x6F12, 0x0002,
	0x602A, 0x30B8,
	0x6F12, 0xD020,
	0x602A, 0x2580,
	0x6F12, 0x000E,
	0x602A, 0x2584,
	0x6F12, 0x0226,
	0x602A, 0x8A10,
	0x6F12, 0x0000,
	0x602A, 0x8A6E,
	0x6F12, 0x1F38,
	0x6F12, 0x1F3F,
	0x602A, 0x8A20,
	0x6F12, 0x0008,
	0x602A, 0x8A26,
	0x6F12, 0x0008,
	0x602A, 0x8A1E,
	0x6F12, 0x0080,
	0x602A, 0x8A68,
	0x6F12, 0x0200,
	0x602A, 0x8A3E,
	0x6F12, 0x03FF,
	0x602A, 0x8A44,
	0x6F12, 0x0080,
	0x602A, 0x1370,
	0x6F12, 0x0400,
	0x602A, 0x1378,
	0x6F12, 0x0084,
	0x602A, 0x2134,
	0x6F12, 0x00C6,
	0x602A, 0x1FC0,
	0x6F12, 0x0100,
	0x6F12, 0x005F,
	0x602A, 0x1FC6,
	0x6F12, 0x0060,
	0x602A, 0x1FCC,
	0x6F12, 0x1C80,
	0x6F12, 0x1C82,
	0x6F12, 0x4000,
	0x6F12, 0xF4B2,
	0x6028, 0x4000,
	0xF484, 0x0080,
	0xF498, 0x06A3,
	0xF496, 0x0200,
	0xF4B2, 0x1C82,
	0xF41C, 0x4916,
	0xF418, 0x001F,
	0xB606, 0x0300,
	0xF482, 0x800D,
	0x6028, 0x4000,
	0x623C, 0x8080,
	0x623E, 0x830D,
	0x6240, 0x0000,
	0xF4A6, 0x001D,
	0x0344, 0x0008,
	0x0346, 0x0008,
	0x0348, 0x19A7,
	0x034A, 0x134F,
	0x034C, 0x19A0,
	0x034E, 0x1348,
	0x0350, 0x0000,
	0x0352, 0x0000,
	0x0900, 0x0011,
	0x0380, 0x0001,
	0x0382, 0x0001,
	0x0384, 0x0001,
	0x0386, 0x0001,
	0x0400, 0x1010,
	0x0408, 0x0100,
	0x040A, 0x0100,
	0x040C, 0x0000,
	0x0110, 0x1002,
	0x0114, 0x0300,
	0x0116, 0x3000,
	0x0118, 0x0001,
	0x011A, 0x0000,
	0x011C, 0x0000,
	0x0136, 0x1800,
	0x013E, 0x0000,
	0x0300, 0x0007,
	0x0302, 0x0001,
	0x0304, 0x0003,
	0x0306, 0x00AF,
	0x0308, 0x000B,
	0x030A, 0x0001,
	0x030C, 0x0000,
	0x030E, 0x0002,
	0x0310, 0x0087,
	0x0312, 0x0000,
	0x0340, 0x143E,
	0x0342, 0x27A0,
	0x0702, 0x0000,
	0x0202, 0x0100,
	0x0200, 0x0100,
	0x022C, 0x0100,
	0x0226, 0x0100,
	0x021E, 0x0000,
	0x080E, 0x0000,
	0x0B00, 0x0080,
	0x0B08, 0x0101,
	0x0D00, 0x0100
};


static void custom1_setting(void)
{
	pr_debug("%s +\n", __func__);
	/* capture sequence */
	table_write_cmos_sensor(addr_data_pair_custom1_gd2sp,
			sizeof(addr_data_pair_custom1_gd2sp) / sizeof(kal_uint16));


	pr_debug("%s -\n", __func__);
}

static void capture_setting(kal_uint16 currefps)
{
	pr_debug("%s +\n", __func__);
	/* capture sequence */
	table_write_cmos_sensor(addr_data_pair_pre_gd2sp,
			sizeof(addr_data_pair_pre_gd2sp) / sizeof(kal_uint16));

	pr_debug("%s -\n", __func__);
}

static kal_uint16 addr_data_pair_video_gd2sp[] = {
	0x6028, 0x2000,
	0x602A, 0x2A08,
	0x6F12, 0x0002,
	0x6F12, 0x49F0,
	0x602A, 0x21F8,
	0x6F12, 0x1358,
	0x602A, 0x30CE,
	0x6F12, 0x01E4,
	0x602A, 0x8A00,
	0x6F12, 0x0001,
	0x602A, 0x3770,
	0x6F12, 0x0100,
	0x602A, 0x220E,
	0x6F12, 0x0005,
	0x6F12, 0x0005,
	0x602A, 0x31CC,
	0x6F12, 0x0019,
	0x602A, 0x31D2,
	0x6F12, 0x0012,
	0x602A, 0x31CA,
	0x6F12, 0x000A,
	0x602A, 0x221C,
	0x6F12, 0x0101,
	0x602A, 0x2A18,
	0x6F12, 0x0801,
	0x602A, 0x5090,
	0x6F12, 0x0E00,
	0x602A, 0x84F8,
	0x6F12, 0x0002,
	0x602A, 0x8508,
	0x6F12, 0x0200,
	0x6F12, 0x0000,
	0x602A, 0x5530,
	0x6F12, 0x0100,
	0x602A, 0x43B0,
	0x6F12, 0x0101,
	0x602A, 0x43D4,
	0x6F12, 0x1300,
	0x6F12, 0x0004,
	0x6F12, 0x13E8,
	0x602A, 0x2A0E,
	0x6F12, 0x1FCD,
	0x602A, 0x1346,
	0x6F12, 0x0018,
	0x602A, 0x2308,
	0x6F12, 0x005B,
	0x602A, 0x381A,
	0x6F12, 0x0008,
	0x6F12, 0x0897,
	0x602A, 0x3812,
	0x6F12, 0x0001,
	0x602A, 0x345E,
	0x6F12, 0x1F40,
	0x6F12, 0x1F47,
	0x602A, 0x3B1A,
	0x6F12, 0x000F,
	0x6F12, 0x0000,
	0x6F12, 0x0007,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0007,
	0x6F12, 0x0000,
	0x6F12, 0x000F,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x602A, 0x3A9A,
	0x6F12, 0x0080,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0080,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x602A, 0x39EE,
	0x6F12, 0x0100,
	0x6F12, 0x0100,
	0x6F12, 0x0200,
	0x6F12, 0x1020,
	0x6F12, 0x1000,
	0x602A, 0x2A1E,
	0x6F12, 0x0108,
	0x602A, 0x134C,
	0x6F12, 0x0002,
	0x602A, 0x30B8,
	0x6F12, 0xD020,
	0x602A, 0x2580,
	0x6F12, 0x000E,
	0x602A, 0x2584,
	0x6F12, 0x0226,
	0x602A, 0x8A10,
	0x6F12, 0x0000,
	0x602A, 0x8A6E,
	0x6F12, 0x0FA0,
	0x6F12, 0x0FA7,
	0x602A, 0x8A20,
	0x6F12, 0x0008,
	0x602A, 0x8A26,
	0x6F12, 0x0008,
	0x602A, 0x8A1E,
	0x6F12, 0x0080,
	0x602A, 0x8A68,
	0x6F12, 0x0200,
	0x602A, 0x8A3E,
	0x6F12, 0x03FF,
	0x602A, 0x8A44,
	0x6F12, 0x0080,
	0x602A, 0x1370,
	0x6F12, 0x0000,
	0x602A, 0x1378,
	0x6F12, 0x0003,
	0x602A, 0x2134,
	0x6F12, 0xFFFF,
	0x602A, 0x1FC0,
	0x6F12, 0x0100,
	0x6F12, 0x005F,
	0x602A, 0x1FC6,
	0x6F12, 0x0060,
	0x602A, 0x1FCC,
	0x6F12, 0x1C80,
	0x6F12, 0x1C82,
	0x6F12, 0x4000,
	0x6F12, 0xF4B2,
	0x6028, 0x4000,
	0xF484, 0x0080,
	0xF498, 0x0343,
	0xF496, 0x0200,
	0xF4B2, 0x1C82,
	0xF41C, 0x4116,
	0xF418, 0x001F,
	0xB606, 0x0400,
	0xF482, 0x800D,
	0x6028, 0x4000,
	0x623C, 0x0080,
	0x623E, 0x0000,
	0x6240, 0x0000,
	0xF4A6, 0x0016,
	0x0344, 0x0008,
	0x0346, 0x0008,
	0x0348, 0x19A7,
	0x034A, 0x134F,
	0x034C, 0x0CD0,
	0x034E, 0x09A0,
	0x0350, 0x0000,
	0x0352, 0x0002,
	0x0900, 0x2222,
	0x0380, 0x0002,
	0x0382, 0x0002,
	0x0384, 0x0002,
	0x0386, 0x0002,
	0x0400, 0x1010,
	0x0408, 0x0100,
	0x040A, 0x0100,
	0x040C, 0x0000,
	0x0110, 0x1002,
	0x0114, 0x0300,
	0x0116, 0x3000,
	0x0118, 0x0001,
	0x011A, 0x0000,
	0x011C, 0x0000,
	0x0136, 0x1800,
	0x013E, 0x0000,
	0x0300, 0x0007,
	0x0302, 0x0001,
	0x0304, 0x0003,
	0x0306, 0x00AF,
	0x0308, 0x000B,
	0x030A, 0x0001,
	0x030C, 0x0000,
	0x030E, 0x0003,
	0x0310, 0x00C8,
	0x0312, 0x0002,
	0x0340, 0x0BB0,
	0x0342, 0x4580,
	0x0702, 0x0000,
	0x0202, 0x0100,
	0x0200, 0x0100,
	0x022C, 0x0100,
	0x0226, 0x0100,
	0x021E, 0x0000,
	0x080E, 0x0000,
	0x0B00, 0x0080,
	0x0B08, 0x0100,
	0x0D00, 0x0000
};

static void normal_video_setting(kal_uint16 currefps)
{
	pr_debug("%s +\n", __func__);
	/* normal video sequence */
	table_write_cmos_sensor(addr_data_pair_video_gd2sp,
		sizeof(addr_data_pair_video_gd2sp) / sizeof(kal_uint16));
	pr_debug("%s -\n", __func__);
}

static kal_uint16 addr_data_pair_hs_gd2sp[] = {
	0x6028, 0x2000,
	0x602A, 0x2A08,
	0x6F12, 0x0002,
	0x6F12, 0x49F0,
	0x602A, 0x21F8,
	0x6F12, 0x1358,
	0x602A, 0x30CE,
	0x6F12, 0x01E4,
	0x602A, 0x8A00,
	0x6F12, 0x0001,
	0x602A, 0x3770,
	0x6F12, 0x0100,
	0x602A, 0x220E,
	0x6F12, 0x0005,
	0x6F12, 0x0005,
	0x602A, 0x31CC,
	0x6F12, 0x0019,
	0x602A, 0x31D2,
	0x6F12, 0x0012,
	0x602A, 0x31CA,
	0x6F12, 0x000A,
	0x602A, 0x221C,
	0x6F12, 0x0101,
	0x602A, 0x2A18,
	0x6F12, 0x0801,
	0x602A, 0x5090,
	0x6F12, 0x0E00,
	0x602A, 0x84F8,
	0x6F12, 0x0002,
	0x602A, 0x8508,
	0x6F12, 0x0200,
	0x6F12, 0x0000,
	0x602A, 0x5530,
	0x6F12, 0x0100,
	0x602A, 0x43B0,
	0x6F12, 0x0101,
	0x602A, 0x43D4,
	0x6F12, 0x1300,
	0x6F12, 0x0004,
	0x6F12, 0x13E8,
	0x602A, 0x2A0E,
	0x6F12, 0x1FCD,
	0x602A, 0x1346,
	0x6F12, 0x0018,
	0x602A, 0x2308,
	0x6F12, 0x005B,
	0x602A, 0x381A,
	0x6F12, 0x0008,
	0x6F12, 0x0897,
	0x602A, 0x3812,
	0x6F12, 0x0001,
	0x602A, 0x345E,
	0x6F12, 0x1F40,
	0x6F12, 0x1F47,
	0x602A, 0x3B1A,
	0x6F12, 0x000F,
	0x6F12, 0x000F,
	0x6F12, 0x000A,
	0x6F12, 0x000A,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x000A,
	0x6F12, 0x000A,
	0x6F12, 0x000F,
	0x6F12, 0x000F,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x602A, 0x3A9A,
	0x6F12, 0x0050,
	0x6F12, 0x0050,
	0x6F12, 0xFFC0,
	0x6F12, 0xFFC0,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0xFFC0,
	0x6F12, 0xFFC0,
	0x6F12, 0x0050,
	0x6F12, 0x0050,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x602A, 0x39EE,
	0x6F12, 0x0100,
	0x6F12, 0x0100,
	0x6F12, 0x0200,
	0x6F12, 0x1020,
	0x6F12, 0x1000,
	0x602A, 0x2A1E,
	0x6F12, 0x0108,
	0x602A, 0x134C,
	0x6F12, 0x0002,
	0x602A, 0x30B8,
	0x6F12, 0xD020,
	0x602A, 0x2580,
	0x6F12, 0x000E,
	0x602A, 0x2584,
	0x6F12, 0x0226,
	0x602A, 0x8A10,
	0x6F12, 0x0000,
	0x602A, 0x8A6E,
	0x6F12, 0x0FA0,
	0x6F12, 0x0FA7,
	0x602A, 0x8A20,
	0x6F12, 0x0008,
	0x602A, 0x8A26,
	0x6F12, 0x0008,
	0x602A, 0x8A1E,
	0x6F12, 0x0080,
	0x602A, 0x8A68,
	0x6F12, 0x0200,
	0x602A, 0x8A3E,
	0x6F12, 0x03FF,
	0x602A, 0x8A44,
	0x6F12, 0x0080,
	0x602A, 0x1370,
	0x6F12, 0x0000,
	0x602A, 0x1378,
	0x6F12, 0x0003,
	0x602A, 0x2134,
	0x6F12, 0xFFFF,
	0x602A, 0x1FC0,
	0x6F12, 0x0100,
	0x6F12, 0x005F,
	0x602A, 0x1FC6,
	0x6F12, 0x0060,
	0x602A, 0x1FCC,
	0x6F12, 0x1C80,
	0x6F12, 0x1C82,
	0x6F12, 0x4000,
	0x6F12, 0xF4B2,
	0x6028, 0x4000,
	0xF484, 0x0080,
	0xF498, 0x0343,
	0xF496, 0x0200,
	0xF4B2, 0x1C82,
	0xF41C, 0x4116,
	0xF418, 0x001F,
	0xB606, 0x0400,
	0xF482, 0x800D,
	0x6028, 0x4000,
	0x623C, 0x0080,
	0x623E, 0x0000,
	0x6240, 0x0000,
	0xF4A6, 0x0016,
	0x0344, 0x0008,
	0x0346, 0x0008,
	0x0348, 0x19A7,
	0x034A, 0x134F,
	0x034C, 0x0CD0,
	0x034E, 0x09A0,
	0x0350, 0x0000,
	0x0352, 0x0002,
	0x0900, 0x2222,
	0x0380, 0x0002,
	0x0382, 0x0002,
	0x0384, 0x0002,
	0x0386, 0x0002,
	0x0400, 0x1010,
	0x0408, 0x0100,
	0x040A, 0x0100,
	0x040C, 0x0000,
	0x0110, 0x1002,
	0x0114, 0x0300,
	0x0116, 0x3000,
	0x0118, 0x0001,
	0x011A, 0x0000,
	0x011C, 0x0000,
	0x0136, 0x1800,
	0x013E, 0x0000,
	0x0300, 0x0007,
	0x0302, 0x0001,
	0x0304, 0x0003,
	0x0306, 0x00AF,
	0x0308, 0x000B,
	0x030A, 0x0001,
	0x030C, 0x0000,
	0x030E, 0x0003,
	0x0310, 0x00E1,
	0x0312, 0x0000,
	0x0340, 0x0B64,
	0x0342, 0x11D0,
	0x0702, 0x0000,
	0x0202, 0x0100,
	0x0200, 0x0100,
	0x022C, 0x0100,
	0x0226, 0x0100,
	0x021E, 0x0000,
	0x080E, 0x0000,
	0x0B00, 0x0080,
	0x0B08, 0x0100,
	0x0D00, 0x0000
};

static void hs_video_setting(void)
{
	pr_debug("%s +\n", __func__);
	/* HS video sequence */
	table_write_cmos_sensor(addr_data_pair_hs_gd2sp,
			sizeof(addr_data_pair_hs_gd2sp) / sizeof(kal_uint16));
	pr_debug("%s -\n", __func__);
}

static kal_uint16 addr_data_pair_slim_gd2sp[] = {
	0x6028, 0x2000,
	0x602A, 0x2A08,
	0x6F12, 0x0002,
	0x6F12, 0x49F0,
	0x602A, 0x21F8,
	0x6F12, 0x1358,
	0x602A, 0x30CE,
	0x6F12, 0x01E4,
	0x602A, 0x8A00,
	0x6F12, 0x0001,
	0x602A, 0x3770,
	0x6F12, 0x0100,
	0x602A, 0x220E,
	0x6F12, 0x0005,
	0x6F12, 0x0005,
	0x602A, 0x31CC,
	0x6F12, 0x0019,
	0x602A, 0x31D2,
	0x6F12, 0x0012,
	0x602A, 0x31CA,
	0x6F12, 0x000A,
	0x602A, 0x221C,
	0x6F12, 0x0101,
	0x602A, 0x2A18,
	0x6F12, 0x0801,
	0x602A, 0x5090,
	0x6F12, 0x0E00,
	0x602A, 0x84F8,
	0x6F12, 0x0002,
	0x602A, 0x8508,
	0x6F12, 0x0200,
	0x6F12, 0x0000,
	0x602A, 0x5530,
	0x6F12, 0x0100,
	0x602A, 0x43B0,
	0x6F12, 0x0101,
	0x602A, 0x43D4,
	0x6F12, 0x1300,
	0x6F12, 0x0004,
	0x6F12, 0x13E8,
	0x602A, 0x2A0E,
	0x6F12, 0x1FCD,
	0x602A, 0x1346,
	0x6F12, 0x0018,
	0x602A, 0x2308,
	0x6F12, 0x005B,
	0x602A, 0x381A,
	0x6F12, 0x0008,
	0x6F12, 0x0897,
	0x602A, 0x3812,
	0x6F12, 0x0001,
	0x602A, 0x345E,
	0x6F12, 0x1F40,
	0x6F12, 0x1F47,
	0x602A, 0x3B1A,
	0x6F12, 0x000F,
	0x6F12, 0x0000,
	0x6F12, 0x0007,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0007,
	0x6F12, 0x0000,
	0x6F12, 0x000F,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x602A, 0x3A9A,
	0x6F12, 0x0080,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0080,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x602A, 0x39EE,
	0x6F12, 0x0100,
	0x6F12, 0x0100,
	0x6F12, 0x0200,
	0x6F12, 0x1020,
	0x6F12, 0x1000,
	0x602A, 0x2A1E,
	0x6F12, 0x0108,
	0x602A, 0x134C,
	0x6F12, 0x0002,
	0x602A, 0x30B8,
	0x6F12, 0xD020,
	0x602A, 0x2580,
	0x6F12, 0x000E,
	0x602A, 0x2584,
	0x6F12, 0x0226,
	0x602A, 0x8A10,
	0x6F12, 0x0000,
	0x602A, 0x8A6E,
	0x6F12, 0x0FA0,
	0x6F12, 0x0FA7,
	0x602A, 0x8A20,
	0x6F12, 0x0008,
	0x602A, 0x8A26,
	0x6F12, 0x0008,
	0x602A, 0x8A1E,
	0x6F12, 0x0080,
	0x602A, 0x8A68,
	0x6F12, 0x0200,
	0x602A, 0x8A3E,
	0x6F12, 0x03FF,
	0x602A, 0x8A44,
	0x6F12, 0x0080,
	0x602A, 0x1370,
	0x6F12, 0x0000,
	0x602A, 0x1378,
	0x6F12, 0x0003,
	0x602A, 0x2134,
	0x6F12, 0xFFFF,
	0x602A, 0x1FC0,
	0x6F12, 0x0100,
	0x6F12, 0x005F,
	0x602A, 0x1FC6,
	0x6F12, 0x0060,
	0x602A, 0x1FCC,
	0x6F12, 0x1C80,
	0x6F12, 0x1C82,
	0x6F12, 0x4000,
	0x6F12, 0xF4B2,
	0x6028, 0x4000,
	0xF484, 0x0080,
	0xF498, 0x0343,
	0xF496, 0x0200,
	0xF4B2, 0x1C82,
	0xF41C, 0x4116,
	0xF418, 0x001F,
	0xB606, 0x0400,
	0xF482, 0x800D,
	0x6028, 0x4000,
	0x623C, 0x0080,
	0x623E, 0x0000,
	0x6240, 0x0000,
	0xF4A6, 0x0016,
	0x0344, 0x0008,
	0x0346, 0x0008,
	0x0348, 0x19A7,
	0x034A, 0x134F,
	0x034C, 0x0CD0,
	0x034E, 0x09A0,
	0x0350, 0x0000,
	0x0352, 0x0002,
	0x0900, 0x2222,
	0x0380, 0x0002,
	0x0382, 0x0002,
	0x0384, 0x0002,
	0x0386, 0x0002,
	0x0400, 0x1010,
	0x0408, 0x0100,
	0x040A, 0x0100,
	0x040C, 0x0000,
	0x0110, 0x1002,
	0x0114, 0x0300,
	0x0116, 0x3000,
	0x0118, 0x0001,
	0x011A, 0x0000,
	0x011C, 0x0000,
	0x0136, 0x1800,
	0x013E, 0x0000,
	0x0300, 0x0007,
	0x0302, 0x0001,
	0x0304, 0x0003,
	0x0306, 0x00AF,
	0x0308, 0x000B,
	0x030A, 0x0001,
	0x030C, 0x0000,
	0x030E, 0x0003,
	0x0310, 0x00C8,
	0x0312, 0x0002,
	0x0340, 0x0BB0,
	0x0342, 0x4580,
	0x0702, 0x0000,
	0x0202, 0x0100,
	0x0200, 0x0100,
	0x022C, 0x0100,
	0x0226, 0x0100,
	0x021E, 0x0000,
	0x080E, 0x0000,
	0x0B00, 0x0080,
	0x0B08, 0x0100,
	0x0D00, 0x0000
};

static void slim_video_setting(void)
{
	pr_debug("%s +\n", __func__);
	/* slim video sequence */
	table_write_cmos_sensor(addr_data_pair_slim_gd2sp,
		sizeof(addr_data_pair_slim_gd2sp) / sizeof(kal_uint16));
	pr_debug("%s -\n", __func__);
}


/*************************************************************************
 * FUNCTION
 *	get_imgsensor_id
 *
 * DESCRIPTION
 *	This function get the sensor ID
 *
 * PARAMETERS
 *	*sensorID : return the sensor ID
 *
 * RETURNS
 *	None
 *
 * GLOBALS AFFECTED
 *
 *************************************************************************/
static kal_uint32 get_imgsensor_id(UINT32 *sensor_id)
{
	kal_uint8 i = 0;
	kal_uint8 retry = 2;

	while (imgsensor_info.i2c_addr_table[i] != 0xff) {
		spin_lock(&imgsensor_drv_lock);
		imgsensor.i2c_write_id = imgsensor_info.i2c_addr_table[i];
		spin_unlock(&imgsensor_drv_lock);
		do {
			*sensor_id = ((read_cmos_sensor_8(0x0000) << 8)
				      | read_cmos_sensor_8(0x0001));

			if (*sensor_id == imgsensor_info.sensor_id) {
				pr_debug("i2c write id: 0x%x, sensor id: 0x%x\n",
					imgsensor.i2c_write_id, *sensor_id);
				up_otp_state_sub=s5kgd2sm_get_otp_data();
				return ERROR_NONE;
			}
			pr_debug("Read sensor id fail, id: 0x%x\n",
				imgsensor.i2c_write_id);
			retry--;
		} while (retry > 0);
		i++;
		retry = 2;
	}

	if (*sensor_id != imgsensor_info.sensor_id) {
	/* if Sensor ID is not correct, Must set *sensor_id to 0xFFFFFFFF */
		*sensor_id = 0xFFFFFFFF;
		return ERROR_SENSOR_CONNECT_FAIL;
	}
	return ERROR_NONE;
}

/*************************************************************************
 * FUNCTION
 *	open
 *
 * DESCRIPTION
 *	This function initialize the registers of CMOS sensor
 *
 * PARAMETERS
 *	None
 *
 * RETURNS
 *	None
 *
 * GLOBALS AFFECTED
 *
 *************************************************************************/
static kal_uint32 open(void)
{
	kal_uint8 i = 0;
	kal_uint8 retry = 2;
	kal_uint16 sensor_id = 0;

	pr_debug("%s", __func__);

	while (imgsensor_info.i2c_addr_table[i] != 0xff) {
		spin_lock(&imgsensor_drv_lock);
		imgsensor.i2c_write_id = imgsensor_info.i2c_addr_table[i];
		spin_unlock(&imgsensor_drv_lock);
		do {
			sensor_id = (
		(read_cmos_sensor_8(0x0000) << 8) | read_cmos_sensor_8(0x0001));

			if (sensor_id == imgsensor_info.sensor_id) {
				pr_debug("i2c write id: 0x%x, sensor id: 0x%x\n",
					imgsensor.i2c_write_id, sensor_id);
				break;
			}

			pr_debug("Read sensor id fail, id: 0x%x\n",
				imgsensor.i2c_write_id);
			retry--;
		} while (retry > 0);
		i++;
		if (sensor_id == imgsensor_info.sensor_id)
			break;
		retry = 2;
	}
	if (imgsensor_info.sensor_id != sensor_id)
		return ERROR_SENSOR_CONNECT_FAIL;

	/* initail sequence write in  */
	
	sensor_init();
	load_s5kgd2sm_awb();
	spin_lock(&imgsensor_drv_lock);

	imgsensor.autoflicker_en = KAL_FALSE;
	imgsensor.sensor_mode = IMGSENSOR_MODE_INIT;
	imgsensor.shutter = 0x3D0;
	imgsensor.gain = 0x100;
	imgsensor.pclk = imgsensor_info.pre.pclk;
	imgsensor.frame_length = imgsensor_info.pre.framelength;
	imgsensor.line_length = imgsensor_info.pre.linelength;
	imgsensor.min_frame_length = imgsensor_info.pre.framelength;
	imgsensor.dummy_pixel = 0;
	imgsensor.dummy_line = 0;
	imgsensor.ihdr_mode = 0;
	imgsensor.test_pattern = KAL_FALSE;
	imgsensor.current_fps = imgsensor_info.pre.max_framerate;
	spin_unlock(&imgsensor_drv_lock);

	return ERROR_NONE;
}				/*      open  */

/*************************************************************************
 * FUNCTION
 *	close
 *
 * DESCRIPTION
 *
 *
 * PARAMETERS
 *	None
 *
 * RETURNS
 *	None
 *
 * GLOBALS AFFECTED
 *
 *************************************************************************/
static kal_uint32 close(void)
{
	pr_debug("E\n");

	return ERROR_NONE;
}				/*      close  */

/*************************************************************************
 * FUNCTION
 * preview
 *
 * DESCRIPTION
 *	This function start the sensor preview.
 *
 * PARAMETERS
 *	*image_window : address pointer of pixel numbers in one period of HSYNC
 *  *sensor_config_data : address pointer of line numbers in one period of VSYNC
 *
 * RETURNS
 *	None
 *
 * GLOBALS AFFECTED
 *
 *************************************************************************/
static kal_uint32 preview(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
			  MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	pr_debug("%s E\n", __func__);
	custom_status = 1;
	spin_lock(&imgsensor_drv_lock);
	imgsensor.sensor_mode = IMGSENSOR_MODE_PREVIEW;
	imgsensor.pclk = imgsensor_info.pre.pclk;
	imgsensor.line_length = imgsensor_info.pre.linelength;
	imgsensor.frame_length = imgsensor_info.pre.framelength;
	imgsensor.min_frame_length = imgsensor_info.pre.framelength;
	imgsensor.autoflicker_en = KAL_FALSE;
	spin_unlock(&imgsensor_drv_lock);

	preview_setting();
	set_mirror_flip(imgsensor.mirror);

	return ERROR_NONE;
}				/*      preview   */

/*************************************************************************
 * FUNCTION
 *	capture
 *
 * DESCRIPTION
 *	This function setup the CMOS sensor in capture MY_OUTPUT mode
 *
 * PARAMETERS
 *
 * RETURNS
 *	None
 *
 * GLOBALS AFFECTED
 *
 *************************************************************************/
static kal_uint32 capture(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
			  MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	pr_debug("%s E\n", __func__);
	custom_status = 1;
	spin_lock(&imgsensor_drv_lock);
	imgsensor.sensor_mode = IMGSENSOR_MODE_CAPTURE;


		if (imgsensor.current_fps != imgsensor_info.cap.max_framerate) {
			pr_debug("Warning: current_fps %d fps is not support, so use cap's setting: %d fps!\n",
				imgsensor.current_fps,
				imgsensor_info.cap.max_framerate / 10);
		}

		imgsensor.pclk = imgsensor_info.cap.pclk;
		imgsensor.line_length = imgsensor_info.cap.linelength;
		imgsensor.frame_length = imgsensor_info.cap.framelength;
		imgsensor.min_frame_length = imgsensor_info.cap.framelength;
		imgsensor.autoflicker_en = KAL_FALSE;
	
	spin_unlock(&imgsensor_drv_lock);

	capture_setting(imgsensor.current_fps);
	set_mirror_flip(imgsensor.mirror);

	return ERROR_NONE;
}				/* capture() */

static kal_uint32 normal_video(
	MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
	MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	pr_debug("%s E\n", __func__);
	custom_status = 1;
	spin_lock(&imgsensor_drv_lock);
	imgsensor.sensor_mode = IMGSENSOR_MODE_VIDEO;
	imgsensor.pclk = imgsensor_info.normal_video.pclk;
	imgsensor.line_length = imgsensor_info.normal_video.linelength;
	imgsensor.frame_length = imgsensor_info.normal_video.framelength;
	imgsensor.min_frame_length = imgsensor_info.normal_video.framelength;
	imgsensor.autoflicker_en = KAL_FALSE;
	spin_unlock(&imgsensor_drv_lock);

	normal_video_setting(imgsensor.current_fps);
	set_mirror_flip(imgsensor.mirror);

	return ERROR_NONE;
}				/*      normal_video   */

static kal_uint32 hs_video(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
			   MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	pr_debug("%s E\n", __func__);
	custom_status = 1;
	spin_lock(&imgsensor_drv_lock);
	imgsensor.sensor_mode = IMGSENSOR_MODE_HIGH_SPEED_VIDEO;
	imgsensor.pclk = imgsensor_info.hs_video.pclk;
	imgsensor.line_length = imgsensor_info.hs_video.linelength;
	imgsensor.frame_length = imgsensor_info.hs_video.framelength;
	imgsensor.min_frame_length = imgsensor_info.hs_video.framelength;
	imgsensor.dummy_line = 0;
	imgsensor.dummy_pixel = 0;
	imgsensor.autoflicker_en = KAL_FALSE;
	spin_unlock(&imgsensor_drv_lock);
	hs_video_setting();
	set_mirror_flip(imgsensor.mirror);

	return ERROR_NONE;
}				/*      hs_video   */

static kal_uint32 slim_video(
	MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
	MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	pr_debug("%s E\n", __func__);
	custom_status = 1;
	spin_lock(&imgsensor_drv_lock);
	imgsensor.sensor_mode = IMGSENSOR_MODE_SLIM_VIDEO;
	imgsensor.pclk = imgsensor_info.slim_video.pclk;
	imgsensor.line_length = imgsensor_info.slim_video.linelength;
	imgsensor.frame_length = imgsensor_info.slim_video.framelength;
	imgsensor.min_frame_length = imgsensor_info.slim_video.framelength;
	imgsensor.dummy_line = 0;
	imgsensor.dummy_pixel = 0;
	imgsensor.autoflicker_en = KAL_FALSE;
	spin_unlock(&imgsensor_drv_lock);
	slim_video_setting();
	set_mirror_flip(imgsensor.mirror);

	return ERROR_NONE;
}				/*      slim_video       */


static kal_uint32 custom1(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
			  MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	printk("E\n");
	if(custom_status == 1){
		custom_status = 2;
	}else{
		custom_status = 3;
	}
	spin_lock(&imgsensor_drv_lock);
	imgsensor.sensor_mode = IMGSENSOR_MODE_CUSTOM1;
	imgsensor.pclk = imgsensor_info.custom1.pclk;
	imgsensor.line_length = imgsensor_info.custom1.linelength;
	imgsensor.frame_length = imgsensor_info.custom1.framelength;
	imgsensor.min_frame_length = imgsensor_info.custom1.framelength;
	imgsensor.autoflicker_en = KAL_FALSE;

	spin_unlock(&imgsensor_drv_lock);
	custom1_setting();
	mdelay(30);
	set_mirror_flip(imgsensor.mirror);
	return ERROR_NONE;
}
static kal_uint32 get_resolution(
	MSDK_SENSOR_RESOLUTION_INFO_STRUCT(*sensor_resolution))
{
	pr_debug("%s E\n", __func__);
	sensor_resolution->SensorFullWidth = imgsensor_info.cap.grabwindow_width;
	sensor_resolution->SensorFullHeight = imgsensor_info.cap.grabwindow_height;

	sensor_resolution->SensorPreviewWidth = imgsensor_info.pre.grabwindow_width;
	sensor_resolution->SensorPreviewHeight = imgsensor_info.pre.grabwindow_height;

	sensor_resolution->SensorVideoWidth = imgsensor_info.normal_video.grabwindow_width;
	sensor_resolution->SensorVideoHeight = imgsensor_info.normal_video.grabwindow_height;

	sensor_resolution->SensorHighSpeedVideoWidth = imgsensor_info.hs_video.grabwindow_width;
	sensor_resolution->SensorHighSpeedVideoHeight = imgsensor_info.hs_video.grabwindow_height;

	sensor_resolution->SensorSlimVideoWidth = imgsensor_info.slim_video.grabwindow_width;
	sensor_resolution->SensorSlimVideoHeight = imgsensor_info.slim_video.grabwindow_height;

	sensor_resolution->SensorCustom1Width = imgsensor_info.custom1.grabwindow_width;
	sensor_resolution->SensorCustom1Height = imgsensor_info.custom1.grabwindow_height;

	return ERROR_NONE;
}				/*      get_resolution  */

static kal_uint32 get_info(enum MSDK_SCENARIO_ID_ENUM scenario_id,
			   MSDK_SENSOR_INFO_STRUCT *sensor_info,
			   MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	sensor_info->SensorClockPolarity = SENSOR_CLOCK_POLARITY_LOW;

	/* Not used */
	sensor_info->SensorClockFallingPolarity = SENSOR_CLOCK_POLARITY_LOW;

	/* inverse with datasheet */
	sensor_info->SensorHsyncPolarity = SENSOR_CLOCK_POLARITY_LOW;

	sensor_info->SensorVsyncPolarity = SENSOR_CLOCK_POLARITY_LOW;
	sensor_info->SensorInterruptDelayLines = 4;	/* Not used */
	sensor_info->SensorResetActiveHigh = FALSE;	/* Not used */
	sensor_info->SensorResetDelayCount = 5;	/* Not used */

	sensor_info->SensroInterfaceType = imgsensor_info.sensor_interface_type;
	sensor_info->MIPIsensorType = imgsensor_info.mipi_sensor_type;
	sensor_info->SettleDelayMode = imgsensor_info.mipi_settle_delay_mode;

	sensor_info->SensorOutputDataFormat = imgsensor_info.sensor_output_dataformat;

	sensor_info->CaptureDelayFrame = imgsensor_info.cap_delay_frame;
	sensor_info->PreviewDelayFrame = imgsensor_info.pre_delay_frame;
	sensor_info->VideoDelayFrame = imgsensor_info.video_delay_frame;

	sensor_info->HighSpeedVideoDelayFrame = imgsensor_info.hs_video_delay_frame;

	sensor_info->SlimVideoDelayFrame = imgsensor_info.slim_video_delay_frame;
	sensor_info->Custom1DelayFrame = imgsensor_info.custom1_delay_frame;
	sensor_info->SensorMasterClockSwitch = 0;	/* Not used */
	sensor_info->SensorDrivingCurrent = imgsensor_info.isp_driving_current;

	/* The frame of setting shutter default 0 for TG int */
	sensor_info->AEShutDelayFrame = imgsensor_info.ae_shut_delay_frame;

	/* The frame of setting sensor gain*/
	sensor_info->AESensorGainDelayFrame = imgsensor_info.ae_sensor_gain_delay_frame;

	sensor_info->AEISPGainDelayFrame = imgsensor_info.ae_ispGain_delay_frame;

	sensor_info->IHDR_Support = imgsensor_info.ihdr_support;
	sensor_info->IHDR_LE_FirstLine = imgsensor_info.ihdr_le_firstline;
	sensor_info->SensorModeNum = imgsensor_info.sensor_mode_num;

	/* change pdaf support mode to pdaf VC mode */
	sensor_info->PDAF_Support = 0;
	sensor_info->SensorMIPILaneNumber = imgsensor_info.mipi_lane_num;
	sensor_info->SensorClockFreq = imgsensor_info.mclk;
	sensor_info->SensorClockDividCount = 3;	/* Not used */
	sensor_info->SensorClockRisingCount = 0;
	sensor_info->SensorClockFallingCount = 2;	/* Not used */
	sensor_info->SensorPixelClockCount = 3;	/* Not used */
	sensor_info->SensorDataLatchCount = 2;	/* Not used */

	sensor_info->MIPIDataLowPwr2HighSpeedTermDelayCount = 0;
	sensor_info->MIPICLKLowPwr2HighSpeedTermDelayCount = 0;
	sensor_info->SensorWidthSampling = 0;	/* 0 is default 1x */
	sensor_info->SensorHightSampling = 0;	/* 0 is default 1x */
	sensor_info->SensorPacketECCOrder = 1;

	switch (scenario_id) {
	case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
		sensor_info->SensorGrabStartX = imgsensor_info.pre.startx;
		sensor_info->SensorGrabStartY = imgsensor_info.pre.starty;

		sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount =
			imgsensor_info.pre.mipi_data_lp2hs_settle_dc;
		break;
	case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
		sensor_info->SensorGrabStartX = imgsensor_info.cap.startx;
		sensor_info->SensorGrabStartY = imgsensor_info.cap.starty;

		sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount =
			imgsensor_info.cap.mipi_data_lp2hs_settle_dc;

		break;
	case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
		sensor_info->SensorGrabStartX = imgsensor_info.normal_video.startx;
		sensor_info->SensorGrabStartY = imgsensor_info.normal_video.starty;

		sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount =
			imgsensor_info.normal_video.mipi_data_lp2hs_settle_dc;

		break;
	case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
		sensor_info->SensorGrabStartX = imgsensor_info.hs_video.startx;
		sensor_info->SensorGrabStartY = imgsensor_info.hs_video.starty;

		sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount =
			imgsensor_info.hs_video.mipi_data_lp2hs_settle_dc;

		break;
	case MSDK_SCENARIO_ID_SLIM_VIDEO:
		sensor_info->SensorGrabStartX = imgsensor_info.slim_video.startx;
		sensor_info->SensorGrabStartY = imgsensor_info.slim_video.starty;

		sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount =
			imgsensor_info.slim_video.mipi_data_lp2hs_settle_dc;

		break;

	case MSDK_SCENARIO_ID_CUSTOM1:
		sensor_info->SensorGrabStartX = imgsensor_info.custom1.startx;
		sensor_info->SensorGrabStartY = imgsensor_info.custom1.starty;
		sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.custom1.mipi_data_lp2hs_settle_dc;

		break;

	default:
		sensor_info->SensorGrabStartX = imgsensor_info.pre.startx;
		sensor_info->SensorGrabStartY = imgsensor_info.pre.starty;

		sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount =
			imgsensor_info.pre.mipi_data_lp2hs_settle_dc;
		break;
	}

	return ERROR_NONE;
}				/*      get_info  */


static kal_uint32 control(enum MSDK_SCENARIO_ID_ENUM scenario_id,
			  MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
			  MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	spin_lock(&imgsensor_drv_lock);
	imgsensor.current_scenario_id = scenario_id;
	spin_unlock(&imgsensor_drv_lock);
	switch (scenario_id) {
	case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
		preview(image_window, sensor_config_data);
		break;
	case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
		capture(image_window, sensor_config_data);
		break;
	case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
		normal_video(image_window, sensor_config_data);
		break;
	case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
		hs_video(image_window, sensor_config_data);
		break;
	case MSDK_SCENARIO_ID_SLIM_VIDEO:
		slim_video(image_window, sensor_config_data);
		break;
	case MSDK_SCENARIO_ID_CUSTOM1:
		custom1(image_window, sensor_config_data);
		break;
	default:
		pr_debug("Error ScenarioId setting");
		preview(image_window, sensor_config_data);
		return ERROR_INVALID_SCENARIO_ID;
	}
	return ERROR_NONE;
}				/* control() */

static kal_uint32 set_video_mode(UINT16 framerate)
{
	pr_debug("framerate = %d\n ", framerate);
	if (framerate == 0)
		/* Dynamic frame rate */
		return ERROR_NONE;
	spin_lock(&imgsensor_drv_lock);
	if ((framerate == 300) && (imgsensor.autoflicker_en == KAL_TRUE))
		imgsensor.current_fps = 296;
	else if ((framerate == 150) && (imgsensor.autoflicker_en == KAL_TRUE))
		imgsensor.current_fps = 146;
	else
		imgsensor.current_fps = framerate;
	spin_unlock(&imgsensor_drv_lock);
	set_max_framerate(imgsensor.current_fps, 1);

	return ERROR_NONE;
}

static kal_uint32 set_auto_flicker_mode(
	kal_bool enable, UINT16 framerate)
{
	pr_debug("enable = %d, framerate = %d\n", enable, framerate);
	spin_lock(&imgsensor_drv_lock);
	if (enable)		/* enable auto flicker */
		imgsensor.autoflicker_en = KAL_TRUE;
	else			/* Cancel Auto flick */
		imgsensor.autoflicker_en = KAL_FALSE;
	spin_unlock(&imgsensor_drv_lock);
	return ERROR_NONE;
}


static kal_uint32 set_max_framerate_by_scenario(
	enum MSDK_SCENARIO_ID_ENUM scenario_id,	MUINT32 framerate)
{
	kal_uint32 frame_length;

	pr_debug("scenario_id = %d, framerate = %d\n", scenario_id, framerate);

	switch (scenario_id) {
	case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
		frame_length = imgsensor_info.pre.pclk
			/ framerate * 10 / imgsensor_info.pre.linelength;
		spin_lock(&imgsensor_drv_lock);

		imgsensor.dummy_line =
			(frame_length > imgsensor_info.pre.framelength)
			? (frame_length - imgsensor_info.pre.framelength) : 0;

		imgsensor.frame_length =
			imgsensor_info.pre.framelength + imgsensor.dummy_line;

		imgsensor.min_frame_length = imgsensor.frame_length;
		spin_unlock(&imgsensor_drv_lock);
		if (imgsensor.frame_length > imgsensor.shutter)
			set_dummy();
		else {
			/*No need to set*/
			pr_debug("frame_length %d < shutter %d",
				imgsensor.frame_length, imgsensor.shutter);
		}
		break;
	case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
		if (framerate == 0)
			return ERROR_NONE;
		frame_length = imgsensor_info.normal_video.pclk
		    / framerate * 10 / imgsensor_info.normal_video.linelength;

		spin_lock(&imgsensor_drv_lock);
		imgsensor.dummy_line =
	    (frame_length > imgsensor_info.normal_video.framelength)
	  ? (frame_length - imgsensor_info.normal_video.  framelength) : 0;

		imgsensor.frame_length =
		 imgsensor_info.normal_video.framelength + imgsensor.dummy_line;

		imgsensor.min_frame_length = imgsensor.frame_length;
		spin_unlock(&imgsensor_drv_lock);
		if (imgsensor.frame_length > imgsensor.shutter)
			set_dummy();
		else {
			/*No need to set*/
			pr_debug("frame_length %d < shutter %d",
				imgsensor.frame_length, imgsensor.shutter);
		}
		break;

	case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
		{
			if (imgsensor.current_fps != imgsensor_info.cap.max_framerate)
				pr_debug("Warning: current_fps %d fps is not support, so use cap's setting: %d fps!\n",
					framerate,
					imgsensor_info.cap.max_framerate / 10);

			frame_length = imgsensor_info.cap.pclk
				/ framerate * 10 / imgsensor_info.cap.linelength;
			spin_lock(&imgsensor_drv_lock);
			imgsensor.dummy_line =
				(frame_length > imgsensor_info.cap.framelength)
				? (frame_length - imgsensor_info.cap.framelength) : 0;
			imgsensor.frame_length =
				imgsensor_info.cap.framelength + imgsensor.dummy_line;
			imgsensor.min_frame_length = imgsensor.frame_length;
			spin_unlock(&imgsensor_drv_lock);
		}
		if (imgsensor.frame_length > imgsensor.shutter)
			set_dummy();
		else {
			/*No need to set*/
			pr_debug("frame_length %d < shutter %d",
				imgsensor.frame_length, imgsensor.shutter);
		}
		break;

	case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
		frame_length = imgsensor_info.hs_video.pclk
			/ framerate * 10 / imgsensor_info.hs_video.linelength;
		spin_lock(&imgsensor_drv_lock);
		imgsensor.dummy_line =
		  (frame_length > imgsensor_info.hs_video.framelength)
		? (frame_length - imgsensor_info.hs_video.  framelength) : 0;

		imgsensor.frame_length =
		    imgsensor_info.hs_video.framelength + imgsensor.dummy_line;

		imgsensor.min_frame_length = imgsensor.frame_length;
		spin_unlock(&imgsensor_drv_lock);
		if (imgsensor.frame_length > imgsensor.shutter)
			set_dummy();
		else {
			/*No need to set*/
			pr_debug("frame_length %d < shutter %d",
				imgsensor.frame_length, imgsensor.shutter);
		}
		break;

	case MSDK_SCENARIO_ID_SLIM_VIDEO:
		frame_length = imgsensor_info.slim_video.pclk
			/ framerate * 10 / imgsensor_info.slim_video.linelength;

		spin_lock(&imgsensor_drv_lock);
		imgsensor.dummy_line =
		  (frame_length > imgsensor_info.slim_video.framelength)
		? (frame_length - imgsensor_info.slim_video.  framelength) : 0;

		imgsensor.frame_length =
		  imgsensor_info.slim_video.framelength + imgsensor.dummy_line;

		imgsensor.min_frame_length = imgsensor.frame_length;
		spin_unlock(&imgsensor_drv_lock);
		if (imgsensor.frame_length > imgsensor.shutter)
			set_dummy();
		else {
			/*No need to set*/
			pr_debug("frame_length %d < shutter %d",
				imgsensor.frame_length, imgsensor.shutter);
		}
		break;


	case MSDK_SCENARIO_ID_CUSTOM1:
		frame_length = imgsensor_info.custom1.pclk / framerate * 10 / imgsensor_info.custom1.linelength;
		spin_lock(&imgsensor_drv_lock);
		imgsensor.dummy_line = (frame_length > imgsensor_info.custom1.framelength) ? (frame_length - imgsensor_info.custom1.framelength) : 0;
		if (imgsensor.dummy_line < 0)
			imgsensor.dummy_line = 0;
		imgsensor.frame_length = imgsensor_info.custom1.framelength + imgsensor.dummy_line;
		imgsensor.min_frame_length = imgsensor.frame_length;
		spin_unlock(&imgsensor_drv_lock);
		if (imgsensor.frame_length > imgsensor.shutter)
			set_dummy();
		break;


	default:		/* coding with  preview scenario by default */
		frame_length = imgsensor_info.pre.pclk
			/ framerate * 10 / imgsensor_info.pre.linelength;

		spin_lock(&imgsensor_drv_lock);
		imgsensor.dummy_line =
			(frame_length > imgsensor_info.pre.framelength)
			? (frame_length - imgsensor_info.pre.framelength) : 0;

		imgsensor.frame_length =
			imgsensor_info.pre.framelength + imgsensor.dummy_line;

		imgsensor.min_frame_length = imgsensor.frame_length;
		spin_unlock(&imgsensor_drv_lock);
		if (imgsensor.frame_length > imgsensor.shutter)
			set_dummy();
		else {
			/*No need to set*/
			pr_debug("frame_length %d < shutter %d",
				imgsensor.frame_length, imgsensor.shutter);
		}
		pr_debug("error scenario_id = %d, we use preview scenario\n",
		scenario_id);
		break;
	}
	return ERROR_NONE;
}


static kal_uint32 get_default_framerate_by_scenario(
	enum MSDK_SCENARIO_ID_ENUM scenario_id, MUINT32 *framerate)
{
	switch (scenario_id) {
	case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
		*framerate = imgsensor_info.pre.max_framerate;
		break;
	case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
		*framerate = imgsensor_info.normal_video.max_framerate;
		break;
	case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
		*framerate = imgsensor_info.cap.max_framerate;
		break;
	case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
		*framerate = imgsensor_info.hs_video.max_framerate;
		break;
	case MSDK_SCENARIO_ID_SLIM_VIDEO:
		*framerate = imgsensor_info.slim_video.max_framerate;
		break;
	case MSDK_SCENARIO_ID_CUSTOM1:
		*framerate = imgsensor_info.custom1.max_framerate;
		break;
	default:
		break;
	}

	return ERROR_NONE;
}

static kal_uint32 set_test_pattern_mode(kal_bool enable)
{
	pr_debug("enable: %d\n", enable);

	if (enable) {
	/* 0 : Normal, 1 : Solid Color, 2 : Color Bar, 3 : Shade Color Bar, 4 : PN9 */
		write_cmos_sensor(0x0602,  0x0000);
		write_cmos_sensor(0x0604,  0x0000);
		write_cmos_sensor(0x0606,  0x0000);
		write_cmos_sensor(0x0608,  0x0000);
		write_cmos_sensor(0x0600, 0x0001);
	} else {
		write_cmos_sensor(0x0600, 0x0000);
	}
	spin_lock(&imgsensor_drv_lock);
	imgsensor.test_pattern = enable;
	spin_unlock(&imgsensor_drv_lock);
	return ERROR_NONE;
}
static kal_uint32 get_sensor_temperature(void)
{
	UINT8 temperature;
	INT32 temperature_convert;

	temperature = read_cmos_sensor_8(0x013a);

	if (temperature >= 0x0 && temperature <= 0x78)
		temperature_convert = temperature;
	else
		temperature_convert = -1;

	return temperature_convert;
}

static kal_uint32 feature_control(MSDK_SENSOR_FEATURE_ENUM feature_id,
				  UINT8 *feature_para, UINT32 *feature_para_len)
{
	UINT16 *feature_return_para_16 = (UINT16 *) feature_para;
	UINT16 *feature_data_16 = (UINT16 *) feature_para;
	UINT32 *feature_return_para_32 = (UINT32 *) feature_para;
	UINT32 *feature_data_32 = (UINT32 *) feature_para;
	INT32 *feature_return_para_i32 = (INT32 *) feature_para;
	unsigned long long *feature_data = (unsigned long long *)feature_para;

	/* SET_PD_BLOCK_INFO_T *PDAFinfo; */
	/* SENSOR_VC_INFO_STRUCT *pvcinfo; */
	struct SENSOR_WINSIZE_INFO_STRUCT *wininfo;

	MSDK_SENSOR_REG_INFO_STRUCT *sensor_reg_data =
		(MSDK_SENSOR_REG_INFO_STRUCT *) feature_para;

	pr_debug("feature_id = %d\n", feature_id);
	switch (feature_id) {
	case SENSOR_FEATURE_GET_AWB_REQ_BY_SCENARIO:
		switch (*feature_data) {
		case MSDK_SCENARIO_ID_CUSTOM3:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) = 1;
			break;
		default:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) = 0;
			break;
		}
		break;

	case SENSOR_FEATURE_GET_GAIN_RANGE_BY_SCENARIO:
		*(feature_data + 1) = imgsensor_info.min_gain;
		*(feature_data + 2) = imgsensor_info.max_gain;
		break;
	case SENSOR_FEATURE_GET_BASE_GAIN_ISO_AND_STEP:
		*(feature_data + 0) = imgsensor_info.min_gain_iso;
		*(feature_data + 1) = imgsensor_info.gain_step;
		*(feature_data + 2) = imgsensor_info.gain_type;
		break;
	case SENSOR_FEATURE_GET_MIN_SHUTTER_BY_SCENARIO:
		*(feature_data + 1) = imgsensor_info.min_shutter;
		*(feature_data + 2) = imgsensor_info.exp_step;
		break;
    case SENSOR_FEATURE_GET_AE_EFFECTIVE_FRAME_FOR_LE:
		*feature_return_para_32 = imgsensor.current_ae_effective_frame;
		break;
	case SENSOR_FEATURE_GET_AE_FRAME_MODE_FOR_LE:
		memcpy(feature_return_para_32, &imgsensor.ae_frm_mode,
			sizeof(struct IMGSENSOR_AE_FRM_MODE));
		break;
	case SENSOR_FEATURE_GET_PIXEL_CLOCK_FREQ_BY_SCENARIO:
		switch (*feature_data) {
		case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1))
				= imgsensor_info.cap.pclk;
			break;
		case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1))
				= imgsensor_info.normal_video.pclk;
			break;
		case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1))
				= imgsensor_info.hs_video.pclk;
			break;
		case MSDK_SCENARIO_ID_SLIM_VIDEO:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1))
				= imgsensor_info.slim_video.pclk;
			break;
		case MSDK_SCENARIO_ID_CUSTOM1:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1))
				= imgsensor_info.custom1.pclk;
			break;
		case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
		default:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1))
				= imgsensor_info.pre.pclk;
			break;
		}
		break;

	case SENSOR_FEATURE_GET_PERIOD_BY_SCENARIO:
		switch (*feature_data) {
		case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1))
			= (imgsensor_info.cap.framelength << 16)
				+ imgsensor_info.cap.linelength;
			break;
		case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1))
			= (imgsensor_info.normal_video.framelength << 16)
				+ imgsensor_info.normal_video.linelength;
			break;
		case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1))
			= (imgsensor_info.hs_video.framelength << 16)
				+ imgsensor_info.hs_video.linelength;
			break;
		case MSDK_SCENARIO_ID_SLIM_VIDEO:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1))
			= (imgsensor_info.slim_video.framelength << 16)
				+ imgsensor_info.slim_video.linelength;
			break;
		case MSDK_SCENARIO_ID_CUSTOM1:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1))
			= (imgsensor_info.custom1.framelength << 16)
				+ imgsensor_info.custom1.linelength;
			break;
		case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
		default:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1))
			= (imgsensor_info.pre.framelength << 16)
				+ imgsensor_info.pre.linelength;
			break;
		}
		break;

	case SENSOR_FEATURE_GET_PERIOD:
		*feature_return_para_16++ = imgsensor.line_length;
		*feature_return_para_16 = imgsensor.frame_length;
		*feature_para_len = 4;
		break;
	case SENSOR_FEATURE_GET_PIXEL_CLOCK_FREQ:
		*feature_return_para_32 = imgsensor.pclk;
		*feature_para_len = 4;
		break;
	case SENSOR_FEATURE_GET_OFFSET_TO_START_OF_EXPOSURE:
		*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) = 1500000;
		break;
	case SENSOR_FEATURE_SET_ESHUTTER:
		set_shutter(*feature_data);
		break;
	case SENSOR_FEATURE_SET_NIGHTMODE:
		/* night_mode((BOOL) *feature_data); no need to implement this mode */
		break;
	case SENSOR_FEATURE_SET_GAIN:
		set_gain((UINT16) *feature_data);
		break;
	case SENSOR_FEATURE_SET_FLASHLIGHT:
		break;
	case SENSOR_FEATURE_SET_ISP_MASTER_CLOCK_FREQ:
		break;
	case SENSOR_FEATURE_SET_REGISTER:
		write_cmos_sensor_8(sensor_reg_data->RegAddr,
			sensor_reg_data->RegData);
		break;
	case SENSOR_FEATURE_GET_REGISTER:
		sensor_reg_data->RegData =
			read_cmos_sensor_8(sensor_reg_data->RegAddr);
		break;
	case SENSOR_FEATURE_GET_LENS_DRIVER_ID:
		*feature_return_para_32 = LENS_DRIVER_ID_DO_NOT_CARE;
		*feature_para_len = 4;
		break;
	case SENSOR_FEATURE_SET_VIDEO_MODE:
		set_video_mode(*feature_data);
		break;
	case SENSOR_FEATURE_CHECK_SENSOR_ID:
		get_imgsensor_id(feature_return_para_32);
		break;
	case SENSOR_FEATURE_SET_AUTO_FLICKER_MODE:
		set_auto_flicker_mode((BOOL) (*feature_data_16),
					*(feature_data_16 + 1));
		break;
	case SENSOR_FEATURE_SET_MAX_FRAME_RATE_BY_SCENARIO:
		set_max_framerate_by_scenario(
	    (enum MSDK_SCENARIO_ID_ENUM) *feature_data, *(feature_data + 1));
		break;
	case SENSOR_FEATURE_GET_DEFAULT_FRAME_RATE_BY_SCENARIO:
		get_default_framerate_by_scenario(
			(enum MSDK_SCENARIO_ID_ENUM) *(feature_data),
			  (MUINT32 *) (uintptr_t) (*(feature_data + 1)));
		break;
	case SENSOR_FEATURE_SET_TEST_PATTERN:
		set_test_pattern_mode((BOOL) (*feature_data));
		break;
	/* for factory mode auto testing */
	case SENSOR_FEATURE_GET_TEST_PATTERN_CHECKSUM_VALUE:
		*feature_return_para_32 = imgsensor_info.checksum_value;
		*feature_para_len = 4;
		break;
	case SENSOR_FEATURE_SET_FRAMERATE:
		pr_debug("current fps :%d\n", *feature_data_32);
		spin_lock(&imgsensor_drv_lock);
		imgsensor.current_fps = (UINT16)*feature_data_32;
		spin_unlock(&imgsensor_drv_lock);
		break;
	case SENSOR_FEATURE_SET_HDR:
		pr_debug("ihdr enable :%d\n", *feature_data_32);
		spin_lock(&imgsensor_drv_lock);
		imgsensor.ihdr_mode = (UINT8)*feature_data_32;
		spin_unlock(&imgsensor_drv_lock);
		break;
	case SENSOR_FEATURE_GET_CROP_INFO:
		wininfo = (struct SENSOR_WINSIZE_INFO_STRUCT *) (uintptr_t) (*(feature_data + 1));
		switch (*feature_data_32) {
		case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
			memcpy((void *)wininfo,
				(void *)&imgsensor_winsize_info[1],
			       sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
			break;
		case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
			memcpy((void *)wininfo,
				(void *)&imgsensor_winsize_info[2],
			       sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
			break;
		case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
			memcpy((void *)wininfo,
				(void *)&imgsensor_winsize_info[3],
			       sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
			break;
		case MSDK_SCENARIO_ID_SLIM_VIDEO:
			memcpy((void *)wininfo,
				(void *)&imgsensor_winsize_info[4],
			       sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
			break;
		case MSDK_SCENARIO_ID_CUSTOM1:
			memcpy((void *)wininfo, (void *)&imgsensor_winsize_info[5],
				sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
			break;
		case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
		default:
			memcpy((void *)wininfo,
				(void *)&imgsensor_winsize_info[0],
			       sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
			break;
		}
		break;

	case SENSOR_FEATURE_SET_IHDR_SHUTTER_GAIN:
		pr_debug("SENSOR_SET_SENSOR_IHDR LE=%d, SE=%d, Gain=%d\n",
			(UINT16) *feature_data,
			(UINT16) *(feature_data + 1),
			(UINT16) *(feature_data + 2));
		break;
	case SENSOR_FEATURE_SET_SHUTTER_FRAME_TIME:
		set_shutter_frame_length((UINT16) (*feature_data),
				(UINT16) (*(feature_data + 1)),
				(BOOL) (*(feature_data + 2)));
	case SENSOR_FEATURE_GET_FRAME_CTRL_INFO_BY_SCENARIO:
		*(feature_data + 1) = 1;
		/* margin info by scenario */
		*(feature_data + 2) = imgsensor_info.margin;
		break;
	case SENSOR_FEATURE_SET_AWB_GAIN:
		break;
	case SENSOR_FEATURE_SET_LSC_TBL:
		break;
	case SENSOR_FEATURE_SET_HDR_SHUTTER:
		pr_debug("SENSOR_FEATURE_SET_HDR_SHUTTER LE=%d, SE=%d\n",
			(UINT16) *feature_data,
			(UINT16) *(feature_data + 1));
		break;
	case SENSOR_FEATURE_SET_STREAMING_SUSPEND:
		pr_debug("SENSOR_FEATURE_SET_STREAMING_SUSPEND\n");
		streaming_control(KAL_FALSE);
		break;
	case SENSOR_FEATURE_SET_STREAMING_RESUME:
		pr_debug("SENSOR_FEATURE_SET_STREAMING_RESUME, shutter:%llu\n",
			*feature_data);
		if (*feature_data != 0)
			set_shutter(*feature_data);
		streaming_control(KAL_TRUE);
		break;
	case SENSOR_FEATURE_GET_BINNING_TYPE:
		switch (*(feature_data + 1)) {
		case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
			*feature_return_para_32 = 1; /*BINNING_NONE*/
			break;
		case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
		case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
		case MSDK_SCENARIO_ID_SLIM_VIDEO:
		case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
		default:
			*feature_return_para_32 = 2; /*BINNING_AVERAGED*/
			break;
		}
		pr_debug("SENSOR_FEATURE_GET_BINNING_TYPE AE_binning_type:%d,\n",
			*feature_return_para_32);
		*feature_para_len = 4;
		break;

	case SENSOR_FEATURE_GET_TEMPERATURE_VALUE:
		*feature_return_para_i32 = get_sensor_temperature();
		*feature_para_len = 4;
		break;
	case SENSOR_FEATURE_GET_PIXEL_RATE:
		switch (*feature_data) {
		case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) =
			(imgsensor_info.cap.pclk /
			(imgsensor_info.cap.linelength - 80))*
			imgsensor_info.cap.grabwindow_width;

			break;
		case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) =
			(imgsensor_info.normal_video.pclk /
			(imgsensor_info.normal_video.linelength - 80))*
			imgsensor_info.normal_video.grabwindow_width;

			break;
		case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) =
			(imgsensor_info.hs_video.pclk /
			(imgsensor_info.hs_video.linelength - 80))*
			imgsensor_info.hs_video.grabwindow_width;

			break;
		case MSDK_SCENARIO_ID_SLIM_VIDEO:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) =
			(imgsensor_info.slim_video.pclk /
			(imgsensor_info.slim_video.linelength - 80))*
			imgsensor_info.slim_video.grabwindow_width;

			break;
		case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
		default:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) =
			(imgsensor_info.pre.pclk /
			(imgsensor_info.pre.linelength - 80))*
			imgsensor_info.pre.grabwindow_width;
			break;
		}
		break;

	case SENSOR_FEATURE_GET_MIPI_PIXEL_RATE:

		switch (*feature_data) {
		case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) =
				imgsensor_info.cap.mipi_pixel_rate;
			break;
		case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) =
				imgsensor_info.normal_video.mipi_pixel_rate;
			break;
		case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) =
				imgsensor_info.hs_video.mipi_pixel_rate;
			break;
		case MSDK_SCENARIO_ID_SLIM_VIDEO:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) =
				imgsensor_info.slim_video.mipi_pixel_rate;
			break;
		case MSDK_SCENARIO_ID_CUSTOM1:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) =
				imgsensor_info.custom1.mipi_pixel_rate;
			break;
		case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
		default:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) =
				imgsensor_info.pre.mipi_pixel_rate;
			break;
		}
		break;
	default:
		break;
	}

	return ERROR_NONE;
}				/*      feature_control()  */

static struct SENSOR_FUNCTION_STRUCT sensor_func = {
	open,
	get_info,
	get_resolution,
	feature_control,
	control,
	close
};

UINT32 S5KGD2SM_MIPI_RAW_SensorInit(
	struct SENSOR_FUNCTION_STRUCT **pfFunc)
{
	/* To Do : Check Sensor status here */
	if (pfFunc != NULL)
		*pfFunc = &sensor_func;
	return ERROR_NONE;
}
