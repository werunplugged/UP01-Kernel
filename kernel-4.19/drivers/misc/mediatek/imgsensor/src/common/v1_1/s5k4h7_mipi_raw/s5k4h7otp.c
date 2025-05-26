/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */



#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <asm/atomic.h>
#include <linux/slab.h>


//#include "kd_camera_hw.h"
#include "kd_imgsensor.h"
#include "kd_imgsensor_define.h"
#include "kd_imgsensor_errcode.h"
#include "s5k4h7mipiraw_Sensor.h"

#define USHORT             unsigned short
#define BYTE               unsigned char
#define S5K4H7SUB_OTP_LSCFLAG_ADDR	0x0A3D
#define S5K4H7SUB_OTP_FLAGFLAG_ADDR	0x0A04
#define S5K4H7SUB_OTP_AWBFLAG_ADDR	0x0A04
#define S5K4H7SUB_LSC_PAGE		0
#define S5K4H7SUB_FLAG_PAGE		21
#define S5K4H7SUB_AWB_PAGE		22

typedef struct {
	unsigned short	infoflag;
	unsigned short	lsc_infoflag;
	unsigned short	flag_infoflag;
	unsigned short	flag_module_integrator_id;
	int		awb_offset;
	int		flag_offset;
	int		lsc_offset;
	int		lsc_group;
	int		flag_group;
	int		group;
	unsigned short	frgcur;
	unsigned short	fbgcur;
	unsigned int	nr_gain;
	unsigned int	ng_gain;
	unsigned int	nb_gain;
	unsigned int	ngrcur;
	unsigned int	ngbcur;
	unsigned int	ngcur;
	unsigned int	nrcur;
	unsigned int	nbcur;
	unsigned int	nggolden;
	unsigned int	nrgolden;
	unsigned int	nbgolden;
	unsigned int	ngrgolden;
	unsigned int	ngbgolden;
	unsigned int	frggolden;
	unsigned int	fbggolden;
	unsigned int	awb_flag_sum;
	unsigned int	lsc_sum;
	unsigned int	lsc_check_flag;
} OTP;

OTP otp_4h7_data_info = {0};

/**********************************************************
 * get_4h7_page_data
 * get page data
 * return true or false
 * ***********************************************************/
void get_4h7_page_data(int pageidx, unsigned char *pdata)
{
	unsigned short get_byte = 0;
	unsigned int addr = 0x0A04;
	int i = 0;

	otp_4h7_write_cmos_sensor_8(0x0A02, pageidx);
	otp_4h7_write_cmos_sensor_8(0x3B41, 0x01);
	otp_4h7_write_cmos_sensor_8(0x3B42, 0x03);
	otp_4h7_write_cmos_sensor_8(0x3B40, 0x01);
	otp_4h7_write_cmos_sensor_8(0x0A00, 0x01);

	do {
		mdelay(1);
		get_byte = otp_4h7_read_cmos_sensor(0x0A01);
	} while ((get_byte & 0x01) != 1);

	for (i = 0; i < 64; i++) {
		pdata[i] = otp_4h7_read_cmos_sensor(addr);
		addr++;
	}
	otp_4h7_write_cmos_sensor_8(0x0A00, 0x04);
	otp_4h7_write_cmos_sensor_8(0x0A00, 0x00);
}

unsigned short selective_4h7_read_region(int pageidx, unsigned int addr)
{
	unsigned short get_byte = 0;

	otp_4h7_write_cmos_sensor_8(0x0A02, pageidx);
	otp_4h7_write_cmos_sensor_8(0x0A00, 0x01);
	do {
		mdelay(1);
		get_byte = otp_4h7_read_cmos_sensor(0x0A01);
	} while ((get_byte & 0x01) != 1);

	get_byte = otp_4h7_read_cmos_sensor(addr);
	otp_4h7_write_cmos_sensor_8(0x0A00, 0x00);

	return get_byte;
}

unsigned int selective_4h7_read_region_16(int pageidx, unsigned int addr)
{
	unsigned int get_byte = 0;
	static int old_pageidx;

	if (pageidx != old_pageidx) {
		otp_4h7_write_cmos_sensor_8(0x0A00, 0x00);
		otp_4h7_write_cmos_sensor_8(0x0A02, pageidx);
		otp_4h7_write_cmos_sensor_8(0x0A00, 0x01);
		do {
			mdelay(1);
			get_byte = otp_4h7_read_cmos_sensor(0x0A01);
		} while ((get_byte & 0x01) != 1);
	}

	get_byte = ((otp_4h7_read_cmos_sensor(addr) << 8) | otp_4h7_read_cmos_sensor(addr+1));
	old_pageidx = pageidx;
	return get_byte;
}

unsigned int selective_4h7_read_region_8(int pageidx, unsigned int addr)
{
	unsigned int get_byte = 0;
	static int old_pageidx;

	if (pageidx != old_pageidx) {
		otp_4h7_write_cmos_sensor_8(0x0A00, 0x00);
		otp_4h7_write_cmos_sensor_8(0x0A02, pageidx);
		otp_4h7_write_cmos_sensor_8(0x0A00, 0x01);
		do {
			mdelay(1);
			get_byte = otp_4h7_read_cmos_sensor(0x0A01);
		} while ((get_byte & 0x01) != 1);
	}

	get_byte = otp_4h7_read_cmos_sensor(addr);
	old_pageidx = pageidx;
	return get_byte;
}

/*****************************************************
 * cal_4h7_rgb_gain
 * **************************************************/
 #if 0
void cal_4h7_rgb_gain(int *r_gain, int *g_gain, int *b_gain, unsigned int r_ration, unsigned int b_ration)
{
	int gain_default = 0x0100;

	if (r_ration >= 1) {
		if (b_ration >= 1) {
			*g_gain = gain_default;
			*r_gain = (int)((gain_default*1000 * r_ration + 500)/1000);
			*b_gain = (int)((gain_default*1000 * b_ration + 500)/1000);
		} else {
			*b_gain = gain_default;
			*g_gain = (int)((gain_default * 1000 / b_ration + 500)/1000);
			*r_gain = (int)((gain_default * r_ration * 1000 / b_ration + 500)/1000);
		}
	} else {
		if (b_ration >= 1) {
			*r_gain = gain_default;
			*g_gain = (int)((gain_default * 1000 / r_ration + 500)/1000);
			*b_gain = (int)((gain_default * b_ration*1000 / r_ration + 500) / 1000);
		} else {
			if (r_ration >= b_ration) {
				*b_gain = gain_default;
				*g_gain = (int)((gain_default * 1000 / b_ration + 500) / 1000);
				*r_gain = (int)((gain_default * r_ration * 1000 / b_ration + 500) / 1000);
			} else {
				*r_gain = gain_default;
				*g_gain = (int)((gain_default * 1000 / r_ration + 500)/1000);
				*b_gain = (int)((gain_default * b_ration * 1000 / r_ration + 500) / 1000);
			}
		}
	}
}
#endif
/**********************************************************
 * apply_4h7_otp_awb
 * apply otp
 * *******************************************************/
#if 1
void apply_4h7_otp_awb(void)
{
	unsigned char r_gain_h, r_gain_l, gr_gain_h, gr_gain_l, gb_gain_h, gb_gain_l,b_gain_h, b_gain_l;
	unsigned int Gr_gain, Gb_gain, R_gain, B_gain, Base_gain;

	Gr_gain = 1000;
	Gb_gain = 1000* otp_4h7_data_info.ngrgolden/otp_4h7_data_info.ngrcur;
	R_gain = 1000 * otp_4h7_data_info.nrgolden/otp_4h7_data_info.nrcur;
	B_gain = 1000 * otp_4h7_data_info.nbgolden/otp_4h7_data_info.nbcur;

	Base_gain = (R_gain < B_gain) ? R_gain : B_gain;
	Base_gain = (Base_gain < Gr_gain) ? Base_gain : Gr_gain;
	Base_gain = (Base_gain < Gb_gain) ? Base_gain : Gb_gain;

	//printk("OTP apply_4h7_otp_awb11, Gr_gain:0x%x ,Gb_gain:0x%x ,R_gain:0x%x ,B_gain:0x%x ,Base_gain:0x%x\n",Gr_gain,Gb_gain,R_gain,B_gain,Base_gain);

	R_gain = 0x100 * R_gain / Base_gain;
	Gr_gain = 0x100 * Gr_gain / Base_gain;
	Gb_gain = 0x100 * Gb_gain / Base_gain;
	B_gain = 0x100 * B_gain / Base_gain;

	//printk("OTP apply_4h7_otp_awb22, Gr_gain:0x%x ,Gb_gain:0x%x ,R_gain:0x%x ,B_gain:0x%x ,Base_gain:0x%x\n",Gr_gain,Gb_gain,R_gain,B_gain,Base_gain);

	r_gain_h = (R_gain >> 8) & 0xff;
	r_gain_l = (R_gain >> 0) & 0xff;

	gr_gain_h = (Gr_gain >> 8) & 0xff;
	gr_gain_l = (Gr_gain >> 0) & 0xff;
	
	gb_gain_h = (Gb_gain >> 8) & 0xff;
	gb_gain_l = (Gb_gain >> 0) & 0xff;

	b_gain_h = (B_gain >> 8) & 0xff;
	b_gain_l = (B_gain >> 0) & 0xff;

	otp_4h7_write_cmos_sensor_8(0x3C0F, 0x00);

	otp_4h7_write_cmos_sensor_8(0x0210, r_gain_h);
	otp_4h7_write_cmos_sensor_8(0x0211, r_gain_l);

	otp_4h7_write_cmos_sensor_8(0x020E, gr_gain_h);
	otp_4h7_write_cmos_sensor_8(0x020F, gr_gain_l);

	otp_4h7_write_cmos_sensor_8(0x0214, gb_gain_h);
	otp_4h7_write_cmos_sensor_8(0x0215, gb_gain_l);

	otp_4h7_write_cmos_sensor_8(0x0212, b_gain_h);
	otp_4h7_write_cmos_sensor_8(0x0213, b_gain_l);
	printk("OTP apply_4h7_otp_awb33, r_gain:0x%x ,g_gain:0x%x ,b_gain:0x%x\n",r_gain_h<<8|r_gain_l,gr_gain_h<<8|gr_gain_l,b_gain_h<<8|b_gain_l);
}
#endif
/*********************************************************
 *apply_4h7_otp_lsc
 * ******************************************************/

void apply_4h7_otp_enb_lsc(void)
{
	printk("OTP enable lsc\n");
	otp_4h7_write_cmos_sensor_8(0x0B00, 0x01);
}

/*********************************************************
 * otp_group_info_4h7
 * *****************************************************/
int otp_group_info_4h7(void)
{
	memset(&otp_4h7_data_info, 0, sizeof(OTP));

	otp_4h7_data_info.lsc_infoflag =
		selective_4h7_read_region(S5K4H7SUB_LSC_PAGE, S5K4H7SUB_OTP_LSCFLAG_ADDR);

	if (otp_4h7_data_info.lsc_infoflag == 0x01) {
		otp_4h7_data_info.lsc_offset = 0;
		otp_4h7_data_info.lsc_group = 1;
		//otp_4h7_data_info.lsc_sum = selective_4h7_read_region_16(S5K4H7SUB_AWB_PAGE, 0x0A32);
	} else if (otp_4h7_data_info.lsc_infoflag == 0x03) {
		otp_4h7_data_info.lsc_offset = 1;
		otp_4h7_data_info.lsc_group = 2;
		//tp_4h7_data_info.lsc_sum = selective_4h7_read_region_16(S5K4H7SUB_AWB_PAGE, 0x0A34);
	} else {
		printk("4H7 OTP read data fail lsc empty!!!\n");
		goto error;
	}

	otp_4h7_data_info.flag_infoflag =
		selective_4h7_read_region(S5K4H7SUB_FLAG_PAGE, S5K4H7SUB_OTP_FLAGFLAG_ADDR);

	if ((otp_4h7_data_info.flag_infoflag>>4 & 0x0c) == 0x04) {
		otp_4h7_data_info.flag_offset = 0;
		otp_4h7_data_info.flag_group = 1;
	} else if ((otp_4h7_data_info.flag_infoflag>>4 & 0x03) == 0x01) {
		otp_4h7_data_info.flag_offset = 4;
		otp_4h7_data_info.flag_group = 2;
	} else {
		printk("4h7 OTP read data fail flag empty!!!\n");
		goto error;
	}



	otp_4h7_data_info.infoflag = selective_4h7_read_region(S5K4H7SUB_AWB_PAGE, S5K4H7SUB_OTP_AWBFLAG_ADDR);



	if ((otp_4h7_data_info.infoflag>>4 & 0x0c) == 0x04) {
		otp_4h7_data_info.group = 1;
		otp_4h7_data_info.nrcur = selective_4h7_read_region_16(S5K4H7SUB_AWB_PAGE, 0x0A05);
		otp_4h7_data_info.nbcur = selective_4h7_read_region_16(S5K4H7SUB_AWB_PAGE, 0x0A07);
		otp_4h7_data_info.ngrcur = selective_4h7_read_region_16(S5K4H7SUB_AWB_PAGE, 0x0A09);
		otp_4h7_data_info.ngbcur = otp_4h7_data_info.ngrcur;

		otp_4h7_data_info.nrgolden = selective_4h7_read_region_16(S5K4H7SUB_AWB_PAGE, 0x0A0B);
		otp_4h7_data_info.nbgolden = selective_4h7_read_region_16(S5K4H7SUB_AWB_PAGE, 0x0A0D);
		otp_4h7_data_info.ngrgolden = selective_4h7_read_region_16(S5K4H7SUB_AWB_PAGE, 0x0A0F);
		otp_4h7_data_info.ngbgolden = otp_4h7_data_info.ngrgolden;
		otp_4h7_data_info.awb_flag_sum = selective_4h7_read_region_8(S5K4H7SUB_AWB_PAGE, 0x0A19);

	} else if ((otp_4h7_data_info.infoflag>>4 & 0x03) == 0x01) {
		otp_4h7_data_info.group = 2;
		otp_4h7_data_info.nrcur = selective_4h7_read_region_16(S5K4H7SUB_AWB_PAGE, 0x0A1A);
		otp_4h7_data_info.nbcur = selective_4h7_read_region_16(S5K4H7SUB_AWB_PAGE, 0x0A1C);
		otp_4h7_data_info.ngrcur = selective_4h7_read_region_16(S5K4H7SUB_AWB_PAGE, 0x0A1E);
		otp_4h7_data_info.ngbcur = otp_4h7_data_info.ngrcur;

		otp_4h7_data_info.nrgolden = selective_4h7_read_region_16(S5K4H7SUB_AWB_PAGE, 0x0A20);
		otp_4h7_data_info.nbgolden = selective_4h7_read_region_16(S5K4H7SUB_AWB_PAGE, 0x0A22);
		otp_4h7_data_info.ngrgolden = selective_4h7_read_region_16(S5K4H7SUB_AWB_PAGE, 0x0A24);
		otp_4h7_data_info.ngbgolden = otp_4h7_data_info.ngrgolden;
		otp_4h7_data_info.awb_flag_sum = selective_4h7_read_region_8(S5K4H7SUB_AWB_PAGE, 0x0A2E);
	} else if ((otp_4h7_data_info.infoflag>>2 & 0x03) == 0x01) {
		otp_4h7_data_info.group = 3;
		otp_4h7_data_info.nrcur = selective_4h7_read_region_16(S5K4H7SUB_AWB_PAGE, 0x0A2F);
		otp_4h7_data_info.nbcur = selective_4h7_read_region_16(S5K4H7SUB_AWB_PAGE, 0x0A31);
		otp_4h7_data_info.ngrcur = selective_4h7_read_region_16(S5K4H7SUB_AWB_PAGE, 0x0A33);
		otp_4h7_data_info.ngbcur = otp_4h7_data_info.ngrcur;

		otp_4h7_data_info.nrgolden = selective_4h7_read_region_16(S5K4H7SUB_AWB_PAGE, 0x0A35);
		otp_4h7_data_info.nbgolden = selective_4h7_read_region_16(S5K4H7SUB_AWB_PAGE, 0x0A37);
		otp_4h7_data_info.ngrgolden = selective_4h7_read_region_16(S5K4H7SUB_AWB_PAGE, 0x0A39);
		otp_4h7_data_info.ngbgolden = otp_4h7_data_info.ngrgolden;
		otp_4h7_data_info.awb_flag_sum = selective_4h7_read_region_8(S5K4H7SUB_AWB_PAGE, 0x0A43);
	} else {
		printk("4h7 OTP read data fail otp_4h7_data_info.empty!!!\n");
		goto error;
	}
	printk("4h7 OTP read data  r %x gr %x gb %x b %x\n",otp_4h7_data_info.nrcur,otp_4h7_data_info.ngrcur,otp_4h7_data_info.ngbcur,otp_4h7_data_info.nbcur);
	printk("4h7 OTP read data1  r %x gr %x gb %x b %x\n",otp_4h7_data_info.nrgolden,otp_4h7_data_info.ngrgolden,otp_4h7_data_info.ngbgolden,otp_4h7_data_info.nbgolden);	
	

	return  0;
error:
	return  -1;
}
/*********************************************************
 * read_4h7_page
 * read_Page1~Page21 of data
 * return true or false
 ********************************************************/
bool read_4h7_page(int page_start, int page_end, unsigned char *pdata)
{
	bool bresult = true;
	int st_page_start = page_start;

	if (page_start <= 0 || page_end > 22) {
		bresult = false;
		printk(" OTP page_end is large!");
		return bresult;
	}
	for (; st_page_start <= page_end; st_page_start++) {
		get_4h7_page_data(st_page_start, pdata);
	}
	return bresult;
}

unsigned int sum_4h7_awb_flag_lsc(unsigned int sum_start, unsigned int sum_end, unsigned char *pdata)
{
	int i = 0;
	unsigned int start;
	unsigned int re_sum = 0;

	for (start = 0x0A04; i < 64; i++, start++) {
		if ((start >= sum_start) && (start <= sum_end)) {
			re_sum += pdata[i];
		}
	}
	return  re_sum;
}

bool check_4h7_sum_flag_awb(void)
{
	int page_start = 22, page_end = 22;

	unsigned char data_p[22][64] = {};
	bool bresult = true;
	unsigned int  sum_awbfg = 0;

	bresult &= read_4h7_page(page_start, page_end, data_p[page_start-1]);

	if (otp_4h7_data_info.group == 1) {
		sum_awbfg = sum_4h7_awb_flag_lsc(0x0A05, 0X0A18, data_p[page_start-1]);
	} else if (otp_4h7_data_info.group == 2) {
		sum_awbfg = sum_4h7_awb_flag_lsc(0x0A1A, 0X0A2D, data_p[page_start-1]);
	} else if (otp_4h7_data_info.group == 3) {
		sum_awbfg = sum_4h7_awb_flag_lsc(0x0A2F, 0X0A42, data_p[page_start-1]);
	}  

	printk("OTP 4h7 awb data %d %d\n",sum_awbfg,otp_4h7_data_info.awb_flag_sum);

	if (sum_awbfg%0xFF +1 == otp_4h7_data_info.awb_flag_sum) {
		apply_4h7_otp_awb();
		printk("OTP 4h7 check awb flag sum success!!!");
	} else {
		printk("OTP 4h7 check awb flag sum fail!!!");
		bresult &= 0;
	}
	return  bresult;
}

bool  check_4h7_sum_flag_lsc(void)
{
	int page_start = 21, page_end = 21;

	unsigned char data_p[22][64] = {};
	bool bresult = true;
	unsigned int  sum_slc = 0;

	if (otp_4h7_data_info.lsc_group == 1) {
		for (page_start = 1, page_end = 6; page_start <= page_end; page_start++) {
			bresult &= read_4h7_page(page_start, page_start, data_p[page_start-1]);
			if (page_start == 6) {
				sum_slc += sum_4h7_awb_flag_lsc(0x0A04, 0x0A2B, data_p[page_start-1]);
				continue;
			}
			sum_slc += sum_4h7_awb_flag_lsc(0x0A04, 0X0A43, data_p[page_start-1]);
		}
	} else if (otp_4h7_data_info.lsc_group == 2) {
		for (page_start = 6, page_end = 12; page_start <= page_end; page_start++) {
			bresult &= read_4h7_page(page_start, page_start, data_p[page_start-1]);
			if (page_start == 6) {
				sum_slc += sum_4h7_awb_flag_lsc(0x0A2C, 0x0A43, data_p[page_start-1]);
				continue;
			} else if (page_start < 12) {
				sum_slc += sum_4h7_awb_flag_lsc(0x0A04, 0X0A43, data_p[page_start-1]);
			} else {
				sum_slc += sum_4h7_awb_flag_lsc(0x0A04, 0X0A13, data_p[page_start-1]);
			}
		}
	}


	apply_4h7_otp_enb_lsc();
	otp_4h7_data_info.lsc_check_flag = 1;

	return  bresult;
}

int update_4h7_otp(void)
{
	int result = 1;
	if(otp_group_info_4h7() == -1){
		printk("OTP read data fail  empty!!!\n");
		result = 0;;
	}
	else {
		if(check_4h7_sum_flag_awb() == 0){
			printk("OTP 4h7 check sum fail!!!\n");
			result = 0;
		}
		else {
			printk("OTP 4h7 check ok\n");
			check_4h7_sum_flag_lsc();
		}
		
	}
	return  result;
}
