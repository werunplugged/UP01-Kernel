/*
 * Copyright (C) 2016 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */

#define PFX "S5KHM6SP_pdafotp"
#define LOG_INF(format, args...) printk(PFX "[%s] " format, __func__, ##args)


#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/atomic.h>
#include <linux/slab.h>

#include "kd_camera_typedef.h"
#include "kd_imgsensor.h"
#include "kd_imgsensor_define.h"
#include "kd_imgsensor_errcode.h"
#include "s5khm6spmipiraw_Sensor.h"
#include <linux/cust_include/cust_project_all_config.h>

#define Sleep(ms) mdelay(ms)





extern int iReadRegI2C(u8 *a_pSendData , u16 a_sizeSendData, u8 * a_pRecvData, u16 a_sizeRecvData, u16 i2cId);
extern int iWriteRegI2C(u8 *a_pSendData , u16 a_sizeSendData, u16 i2cId);

//BYTE s5khm6sp_DCC_data[96]= {0};
//BYTE s5khm6sp_LRC_data[384]= {0};
//YTE s5khm6sp_QSC_data[2304]= {0};
BYTE s5khm6sp_step_data[1500]={0};
static BYTE s5khm6sp_AWB_data[12] = {0};
//kal_uint8 qsc_flag = 0;
//kal_uint8 lrc_flag = 0;

#define USHORT             unsigned short
#define BYTE               unsigned char

#define EEPROM_WRITE_ID         0xA0

#define OTP_DATA                s5khm6sp_eeprom_data
#define OTP_PLATFORM_CHOICE     0x0A     //bit0:awb, bit1:af, bit3:lsc  1010
#define OTP_FLAG_ADDR           0x0000
#define AWB_ADDR                0x0021
#define AWB_CHECK_ADDR          0x002D
#define LSC_ADDR                0x0031
#define LSC_CHECK_ADDR          0x077D
#define AF_ADDR                 0x0011
#define AF_CHECK_ADDR           0x0017





#define CHECKSUM_METHOD(x,addr)  \
( ((x) % 255 +1) == read_cmos_sensor_8(addr) )

BYTE OTP_DATA[2048]= {0};
BYTE s5khm6sp_PDAF_data[1411] = {0};

static kal_uint16 read_cmos_sensor_8(kal_uint16 addr)
{
    kal_uint16 get_byte=0;
    char pusendcmd[2] = {(char)(addr >> 8) , (char)(addr & 0xFF) };
    iReadRegI2C(pusendcmd , 2, (u8*)&get_byte,1,EEPROM_WRITE_ID);
    return get_byte;
}
static void write_cmos_sensor_8(kal_uint16 addr, kal_uint8 para)
{
	char pusendcmd[3] = {(char)(addr >> 8), (char)(addr & 0xFF), (char)(para & 0xFF)};

	iWriteRegI2C(pusendcmd, 3, 0x20);
}

static void write_cmos_sensor(kal_uint16 addr, kal_uint16 para)
{
	char pusendcmd[4] = {(char)(addr >> 8), (char)(addr & 0xFF), (char)(para >> 8), (char)(para & 0xFF)};

	//kdSetI2CSpeed(imgsensor_info.i2c_speed); // Add this func to set i2c speed by each sensor
	iWriteRegI2C(pusendcmd, 4, 0x20);
}

static int read_s5khm6sp_MTK(void)
{
	int i = 0;
	kal_uint32 checksum = 0;
	int step1_addr = 0x77F;
	int step1_check_addr = 0x96F;
	int step2_addr = 0x971;
	int step2_check_addr = 0xD5D;
	int ret = 0;


	for(i = 0; i < step1_check_addr - step1_addr; i++)
	{
		s5khm6sp_step_data[i] = read_cmos_sensor_8(step1_addr + i);
		checksum += s5khm6sp_step_data[i];
		//LOG_INF("s5khm6sp_step_data[%d] = %u \n", i, s5khm6sp_step_data[i]);
	}
	if(CHECKSUM_METHOD(checksum, step1_check_addr))
	{
		LOG_INF("step1 Checksum Success");
		ret = 1;
	}
	else
	{
		LOG_INF("step1 Checksum Failed!!!");
		ret = 0;
	}
	checksum=0;
	for(; i < step1_check_addr - step1_addr + step2_check_addr - step2_addr; i++)
	{
		s5khm6sp_step_data[i] = read_cmos_sensor_8(step2_addr + i-(step1_check_addr - step1_addr));
		checksum += s5khm6sp_step_data[i];
		//LOG_INF("s5khm6sp_step_data[%d] = %u \n", i, s5khm6sp_step_data[i]);
	}
	if(CHECKSUM_METHOD(checksum, step2_check_addr))
	{
		LOG_INF("step2 Checksum Success");
		ret = 1;
	}
	else
	{
		LOG_INF("step2 Checksum Failed!!!");
		ret = 0;
	}	


	return ret;
	//memcpy(data, s5khm6sp_DCC_data , size);
}

int s5khm6sp_get_otp_data(void)
{
	int i;
	int ret = 1;
	kal_uint32 checksum = 0;

    if(0x01 != read_cmos_sensor_8(OTP_FLAG_ADDR))
	{
        LOG_INF("OTP DATA Invalid!!!\n");
        return 0;
    }

//0x010b00ff
	OTP_DATA[0] = 0xff;
	OTP_DATA[1] = 0x00;
	OTP_DATA[2] = 0x0b;
	OTP_DATA[3] = 0x01;

	OTP_DATA[4] = OTP_PLATFORM_CHOICE;

	//AF
    if(OTP_PLATFORM_CHOICE & 0x2)
    {
	checksum += read_cmos_sensor_8(AF_ADDR);
    	for(i = 0; i < 4; i++)
    	{
    		OTP_DATA[i + 6] = read_cmos_sensor_8(AF_ADDR+1 + i);
			checksum += OTP_DATA[i + 6];
	}
	checksum += read_cmos_sensor_8(AF_ADDR+1+i);

    	if(CHECKSUM_METHOD(checksum, AF_CHECK_ADDR))
    	{
    		LOG_INF("AF Checksum OK\n");
			OTP_DATA[6] ^= OTP_DATA[7];
			OTP_DATA[7] ^= OTP_DATA[6];
			OTP_DATA[6] ^= OTP_DATA[7];
			
			OTP_DATA[8] ^= OTP_DATA[9];
			OTP_DATA[9] ^= OTP_DATA[8];
			OTP_DATA[8] ^= OTP_DATA[9];

			OTP_DATA[7] ^= OTP_DATA[9];
			OTP_DATA[9] ^= OTP_DATA[7];
			OTP_DATA[7] ^= OTP_DATA[9];
			
			OTP_DATA[8] ^= OTP_DATA[6];
			OTP_DATA[6] ^= OTP_DATA[8];
			OTP_DATA[8] ^= OTP_DATA[6];
			
            LOG_INF("AFInf = %u, AFMarco = %u\n", OTP_DATA[7] << 8 | OTP_DATA[6], OTP_DATA[9] << 8 | OTP_DATA[8]);
    	}
    	else
    	{
    		LOG_INF("AF Checksum Failed!!!\n");
    		OTP_DATA[4] = OTP_DATA[4] & (~0x02);
			ret &= 0;
    	}
    }


//AWB Platform
    if(OTP_PLATFORM_CHOICE & 0x1)
    {
        checksum = 0;

	OTP_DATA[10] = read_cmos_sensor_8(AWB_ADDR + 1);
	OTP_DATA[11] = read_cmos_sensor_8(AWB_ADDR + 5);
	OTP_DATA[12] = read_cmos_sensor_8(AWB_ADDR + 7);
	OTP_DATA[13] = read_cmos_sensor_8(AWB_ADDR + 3);

	OTP_DATA[14] = read_cmos_sensor_8(AWB_ADDR + 9);
	OTP_DATA[15] = read_cmos_sensor_8(AWB_ADDR + 13);
	OTP_DATA[16] = read_cmos_sensor_8(AWB_ADDR + 15);
	OTP_DATA[17] = read_cmos_sensor_8(AWB_ADDR + 11);
//1  3  5    7
//r  b  gr   gb
//9  11  13  15 
        for(i=0; i < 8; i++){
			checksum += OTP_DATA[i + 10];
			printk("AWB  OTP_DATA[%d] = %x",i+10,OTP_DATA[i + 10]);
		}
        if(CHECKSUM_METHOD(checksum, AWB_CHECK_ADDR))
        {
			LOG_INF("AWB OTP Checksum OK\n");
			LOG_INF("Unit_R = 0x%x, Unit_Gr = 0x%x, Unit_Gb = 0x%x, Unit_B = 0x%x\n", OTP_DATA[10], OTP_DATA[11], OTP_DATA[12], OTP_DATA[13]);
			LOG_INF("Golden_R = 0x%x, Golden_Gr = 0x%x, Golden_Gb = 0x%x, Golden_B = 0x%x\n", OTP_DATA[14], OTP_DATA[15], OTP_DATA[16], OTP_DATA[17]);
        }
        else
        {
			LOG_INF("AWB OTP Checksum Failed!!! %u %u\n",checksum % 256,read_cmos_sensor_8(AWB_CHECK_ADDR));
			OTP_DATA[4] = OTP_DATA[4] & (~0x01);
			ret &= 0;
        }
    }
	else	//AWB Sensor
	{
		checksum = 0;
		for(i = 0; i < 12; i++)
		{
			s5khm6sp_AWB_data[i] = read_cmos_sensor_8(AWB_ADDR + i);
			checksum += s5khm6sp_AWB_data[i];
		}

		if(CHECKSUM_METHOD(checksum, AWB_CHECK_ADDR))
			LOG_INF("Sensor AWB Checksum OK\n");
		else
		{
			LOG_INF("Sensor AWB Checksum Failed!!!\n");
			ret &= 0;
		}
	}

	//LSC
    if(OTP_PLATFORM_CHOICE & 0x8)
    {
    	checksum = 0;
    	for(i = 0; i < 1868; i++)
    	{
    		OTP_DATA[20 + i] = read_cmos_sensor_8(LSC_ADDR + i);
    		checksum += OTP_DATA[20 + i];
			//LOG_INF("LSC data[%d] = 0x%x\n", i, OTP_DATA[20 + i]);
    	}

        if(CHECKSUM_METHOD(checksum, LSC_CHECK_ADDR))
    		LOG_INF("LSC Checksum OK\n");
    	else
    	{
    		LOG_INF("LSC Checksum Failed!!!\n");
    		OTP_DATA[4] = OTP_DATA[4] & (~0x08);
			ret &= 0;
    	}
    }
#if 0	
	if(!read_s5khm6sp_DCC())
		ret &= 0;

	if(!read_s5khm6sp_QSC())
		ret &= 0;

	if(!read_s5khm6sp_LRC())
		ret &= 0;
#endif
	if(!read_s5khm6sp_MTK())
		ret &= 0;
	//read_s5khm6sp_PD_data();
	return ret;
}


void load_s5khm6sp_awb(void)
{
	kal_uint32 Unit_RG, Unit_BG, Unit_GG, Golden_RG, Golden_BG, Golden_GG;
	kal_uint32 R_Gain, B_Gain, G_Gain, G_Gain_R, G_Gain_B;
	kal_uint32 r_ratio, b_ratio;
	#define GAIN_DEFAULT 0x0100

	Unit_RG   = ((s5khm6sp_AWB_data[0] << 8) | s5khm6sp_AWB_data[1]);
	Unit_BG   = ((s5khm6sp_AWB_data[2] << 8) | s5khm6sp_AWB_data[3]);
	Unit_GG   = ((s5khm6sp_AWB_data[4] << 8) | s5khm6sp_AWB_data[5]);
	Golden_RG = ((s5khm6sp_AWB_data[6] << 8) | s5khm6sp_AWB_data[7]);
	Golden_BG = ((s5khm6sp_AWB_data[8] << 8) | s5khm6sp_AWB_data[9]);
	Golden_GG = ((s5khm6sp_AWB_data[10] << 8) | s5khm6sp_AWB_data[11]);

	LOG_INF("Unit_RG = 0x%x, Unit_BG = 0x%x, Unit_GG = 0x%x\n", Unit_RG, Unit_BG, Unit_GG);
	LOG_INF("Golden_RG = 0x%x, Golden_BG = 0x%x, Golden_GG = 0x%x\n", Golden_RG, Golden_BG, Golden_GG);

	r_ratio = 1024 * (Golden_RG) / (Unit_RG);
    b_ratio = 1024 * (Golden_BG) / (Unit_BG);

    if(r_ratio >= 1024 )
    {
        if(b_ratio>=1024) 
        {
            R_Gain = GAIN_DEFAULT * r_ratio / 1024;
            G_Gain = GAIN_DEFAULT;    
            B_Gain = GAIN_DEFAULT * b_ratio / 1024;
        }
        else
        {
            R_Gain = GAIN_DEFAULT * r_ratio / b_ratio;
            G_Gain = GAIN_DEFAULT * 1024 / b_ratio;
            B_Gain = GAIN_DEFAULT;    
        }
    }
    else             
    {
        if(b_ratio >= 1024)
        {
            R_Gain = GAIN_DEFAULT;    
            G_Gain = GAIN_DEFAULT * 1024 / r_ratio;
            B_Gain = GAIN_DEFAULT *  b_ratio / r_ratio;

        } 
        else 
        {
            G_Gain_R = GAIN_DEFAULT * 1024 / r_ratio;
            G_Gain_B = GAIN_DEFAULT * 1024 / b_ratio;

            if(G_Gain_R >= G_Gain_B)
            {
                R_Gain = GAIN_DEFAULT;
                G_Gain = GAIN_DEFAULT * 1024 / r_ratio;
                B_Gain = GAIN_DEFAULT * b_ratio / r_ratio;
            } 
            else
            {
                R_Gain = GAIN_DEFAULT * r_ratio / b_ratio;
                G_Gain = GAIN_DEFAULT * 1024 / b_ratio;
                B_Gain = GAIN_DEFAULT;
            }
        }    
    }

	LOG_INF("R_Gain=0x%x, B_Gain=0x%x, G_Gain=0x%x\n", R_Gain, B_Gain, G_Gain);
	
	write_cmos_sensor(0xFCFC, 0x4000);
	write_cmos_sensor_8(0x0210, (R_Gain & 0xff00)>>8);
	write_cmos_sensor_8(0x0211, (R_Gain & 0x00ff)); //R

	write_cmos_sensor_8(0x020e, (G_Gain & 0xff00)>>8);
	write_cmos_sensor_8(0x020f, (G_Gain & 0x00ff));//GR

	write_cmos_sensor_8(0x0214, (G_Gain & 0xff00)>>8);
	write_cmos_sensor_8(0x0215, (G_Gain & 0x00ff));//GB

	write_cmos_sensor_8(0x0212, (B_Gain & 0xff00)>>8);
	write_cmos_sensor_8(0x0213, (B_Gain & 0x00ff)); //B

	write_cmos_sensor_8(0x020D, 1);
}
