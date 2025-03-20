// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Awinic Inc.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/debugfs.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <linux/syscalls.h>
#include <linux/power_supply.h>
#include <linux/pm_qos.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/control.h>
#include <sound/soc.h>
#include <linux/vmalloc.h>
#include <linux/proc_fs.h>
#include <linux/mman.h>
#include "aw8622x_reg.h"
#include "aw8622x.h"
#include "haptic.h"

#define VIB_DEVICE                              "mtk_vibrator"
#define VIB_TAG                                 "[vibrator]"

/******************************************************
 *
 * Value
 *
 ******************************************************/
static char *aw8622x_ram_name = "aw8622x_haptic.bin";
static char aw8622x_rtp_name[][AW8622X_RTP_NAME_MAX] = {
	{"aw8622x_osc_rtp_12K_10s.bin"},
	{"aw8622x_rtp.bin"},
	{"aw8622x_rtp_lighthouse.bin"},
	{"aw8622x_rtp_silk.bin"},
};

struct pm_qos_request aw8622x_pm_qos_req_vb;

/******************************************************
*
* functions
*
******************************************************/

static int aw8622x_analyse_duration_range(struct aw8622x *aw8622x);
static int aw8622x_haptic_set_pwm(struct aw8622x *aw8622x, unsigned char mode);

 /******************************************************
 *
 * aw8622x i2c write/read
 *
 ******************************************************/
static int aw8622x_i2c_write(struct aw8622x *aw8622x,
			     unsigned char reg_addr, unsigned char reg_data)
{
	int ret = -1;
	unsigned char cnt = 0;

	while (cnt < AW8622X_I2C_RETRIES) {
		ret =
		    i2c_smbus_write_byte_data(aw8622x->i2c, reg_addr, reg_data);
		if (ret < 0) {
			aw_dev_err(aw8622x->dev, "%s: i2c_write addr=0x%02X, data=0x%02X, cnt=%d, error=%d\n",
				__func__, reg_addr, reg_data, cnt, ret);
		} else {
			break;
		}
		cnt++;
		usleep_range(AW8622X_I2C_RETRY_DELAY * 1000,
			     AW8622X_I2C_RETRY_DELAY * 1000 + 500);
	}
	return ret;
}

int aw8622x_i2c_read(struct aw8622x *aw8622x,
			    unsigned char reg_addr, unsigned char *reg_data)
{
	int ret = -1;
	unsigned char cnt = 0;

	while (cnt < AW8622X_I2C_RETRIES) {
		ret = i2c_smbus_read_byte_data(aw8622x->i2c, reg_addr);
		if (ret < 0) {
			aw_dev_err(aw8622x->dev,
				"%s: i2c_read addr=0x%02X, cnt=%d error=%d\n",
				   __func__, reg_addr, cnt, ret);
		} else {
			*reg_data = ret;
			break;
		}
		cnt++;
		usleep_range(AW8622X_I2C_RETRY_DELAY * 1000,
			     AW8622X_I2C_RETRY_DELAY * 1000 + 500);
	}
	return ret;
}

int aw8622x_i2c_writes(struct aw8622x *aw8622x,
			unsigned char reg_addr, unsigned char *buf,
			unsigned int len)
{
	int ret = -1;
	unsigned char *data = NULL;

	data = kmalloc(len + 1, GFP_KERNEL);
	if (data == NULL) {
		aw_dev_err(aw8622x->dev,
			"%s: can not allocate memory\n", __func__);
		return -ENOMEM;
	}
	data[0] = reg_addr;
	memcpy(&data[1], buf, len);
	ret = i2c_master_send(aw8622x->i2c, data, len + 1);
	if (ret < 0)
		aw_dev_err(aw8622x->dev,
			"%s: i2c master send error\n", __func__);
	kfree(data);
	return ret;
}

static int aw8622x_i2c_write_bits(struct aw8622x *aw8622x,
				  unsigned char reg_addr, unsigned int mask,
				  unsigned char reg_data)
{
	int ret = -1;
	unsigned char reg_val = 0;

	ret = aw8622x_i2c_read(aw8622x, reg_addr, &reg_val);
	if (ret < 0) {
		aw_dev_err(aw8622x->dev,
			"%s: i2c read error, ret=%d\n", __func__, ret);
		return ret;
	}
	reg_val &= mask;
	reg_val |= reg_data;
	ret = aw8622x_i2c_write(aw8622x, reg_addr, reg_val);
	if (ret < 0) {
		aw_dev_err(aw8622x->dev,
			"%s: i2c write error, ret=%d\n", __func__, ret);
		return ret;
	}
	return 0;
}

unsigned char aw8622x_haptic_rtp_get_fifo_afs(struct aw8622x *aw8622x)
{
	unsigned char ret = 0;
	unsigned char reg_val = 0;

	aw8622x_i2c_read(aw8622x, AW8622X_REG_SYSST, &reg_val);
	reg_val &= AW8622X_BIT_SYSST_FF_AFS;
	ret = reg_val >> 3;
	return ret;
}

/*****************************************************
 *
 * rtp
 *
 *****************************************************/
void aw8622x_haptic_set_rtp_aei(struct aw8622x *aw8622x, bool flag)
{
	if (flag) {
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSINTM,
				       AW8622X_BIT_SYSINTM_FF_AEM_MASK,
				       AW8622X_BIT_SYSINTM_FF_AEM_ON);
	} else {
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSINTM,
				       AW8622X_BIT_SYSINTM_FF_AEM_MASK,
				       AW8622X_BIT_SYSINTM_FF_AEM_OFF);
	}
}

static int aw8622x_analyse_duration_range(struct aw8622x *aw8622x)
{
	int i = 0;
	int ret = 0;
	int len = 0;
	int *duration_time = NULL;

	len = ARRAY_SIZE(aw8622x->dts_info.duration_time);
	duration_time = aw8622x->dts_info.duration_time;
	if (len < 2) {
		aw_dev_err(aw8622x->dev, "%s: duration time range error\n",
			__func__);
		return -ERANGE;
	}
	for (i = (len - 1); i > 0; i--) {
		if (duration_time[i] > duration_time[i-1])
			continue;
		else
			break;

	}
	if (i > 0) {
		aw_dev_err(aw8622x->dev, "%s: duration time range error\n",
			__func__);
		ret = -ERANGE;
	}
	return ret;
}

static int
aw8622x_analyse_duration_array_size(struct aw8622x *aw8622x, struct device_node *np)
{
	int ret = 0;

	ret = of_property_count_elems_of_size(np, "aw8622x_vib_duration_time", 4);
	if (ret < 0) {
		aw8622x->duration_time_flag = -1;
		aw_dev_info(aw8622x->dev,
			"%s vib_duration_time not found\n", __func__);
		return ret;
	}
	aw8622x->duration_time_size = ret;
	if (aw8622x->duration_time_size > 3) {
		aw8622x->duration_time_flag = -1;
		aw_dev_info(aw8622x->dev,
			"%s vib_duration_time error, array size = %d\n",
			__func__, aw8622x->duration_time_size);
		return -ERANGE;
	}
	return 0;
}

/*****************************************************
 *
 * device tree
 *
 *****************************************************/
int aw8622x_parse_dt(struct aw8622x *aw8622x, struct device *dev,
			    struct device_node *np)
{
	unsigned int val = 0;
	unsigned int prctmode_temp[3];
	unsigned int sine_array_temp[4];
	unsigned int trig_config_temp[21];
	unsigned int duration_time[3];
	int ret = 0;

#if 0
        val = of_property_read_u32(np, "aw8622x_vib_lk_f0_cali",
                        &aw8622x->dts_info.lk_f0_cali);
        if (val != 0)
                aw_dev_info(aw8622x->dev, "aw8622x_vib_lk_f0_cali not found\n");
        aw_dev_info(aw8622x->dev, "%s: aw8622x_vib_lk_f0_cali = 0x%02x\n",
                    __func__, aw8622x->dts_info.lk_f0_cali);
#endif

	val = of_property_read_u32(np,
			"aw8622x_vib_mode",
			&aw8622x->dts_info.mode);
	if (val != 0)
		aw_dev_info(aw8622x->dev,
			"%s aw8622x_vib_mode not found\n",
			__func__);
	val = of_property_read_u32(np,
			"aw8622x_vib_f0_pre",
			&aw8622x->dts_info.f0_ref);
	if (val != 0)
		aw_dev_info(aw8622x->dev, "%s vib_f0_ref not found\n",
			    __func__);
	val =
	    of_property_read_u32(np, "aw8622x_vib_f0_cali_percen",
				 &aw8622x->dts_info.f0_cali_percent);
	if (val != 0)
		aw_dev_info(aw8622x->dev, "%s vib_f0_cali_percent not found\n",
			    __func__);

	val = of_property_read_u32(np, "aw8622x_vib_cont_drv1_lvl",
				   &aw8622x->dts_info.cont_drv1_lvl_dt);
	if (val != 0)
		aw_dev_info(aw8622x->dev, "%s vib_cont_drv1_lvl not found\n",
			    __func__);
	val =
	    of_property_read_u32(np, "aw8622x_vib_cont_drv2_lvl",
				 &aw8622x->dts_info.cont_drv2_lvl_dt);
	if (val != 0)
		aw_dev_info(aw8622x->dev, "%s vib_cont_drv2_lvl not found\n",
			    __func__);
	val =
	    of_property_read_u32(np, "aw8622x_vib_cont_drv1_time",
				 &aw8622x->dts_info.cont_drv1_time_dt);
	if (val != 0)
		aw_dev_info(aw8622x->dev, "%s vib_cont_drv1_time not found\n",
			    __func__);
	val =
	    of_property_read_u32(np, "aw8622x_vib_cont_drv2_time",
				 &aw8622x->dts_info.cont_drv2_time_dt);
	if (val != 0)
		aw_dev_info(aw8622x->dev, "%s vib_cont_drv2_time not found\n",
			    __func__);
	val =
	    of_property_read_u32(np, "aw8622x_vib_cont_drv_width",
				 &aw8622x->dts_info.cont_drv_width);
	if (val != 0)
		aw_dev_info(aw8622x->dev, "%s vib_cont_drv_width not found\n",
			    __func__);
	val =
	    of_property_read_u32(np, "aw8622x_vib_cont_wait_num",
				 &aw8622x->dts_info.cont_wait_num_dt);
	if (val != 0)
		aw_dev_info(aw8622x->dev, "%s vib_cont_wait_num not found\n",
			    __func__);
	val =
	    of_property_read_u32(np, "aw8622x_vib_cont_brk_gain",
				 &aw8622x->dts_info.cont_brk_gain);
	if (val != 0)
		aw_dev_info(aw8622x->dev, "%s vib_cont_brk_gain not found\n",
			    __func__);
	val =
	    of_property_read_u32(np, "aw8622x_vib_cont_tset",
				 &aw8622x->dts_info.cont_tset);
	if (val != 0)
		aw_dev_info(aw8622x->dev, "%s vib_cont_tset not found\n",
			    __func__);
	val =
	    of_property_read_u32(np, "aw8622x_vib_cont_bemf_set",
				 &aw8622x->dts_info.cont_bemf_set);
	if (val != 0)
		aw_dev_info(aw8622x->dev, "%s vib_cont_bemf_set not found\n",
			    __func__);
	val =
	    of_property_read_u32(np, "aw8622x_vib_d2s_gain",
				 &aw8622x->dts_info.d2s_gain);
	if (val != 0)
		aw_dev_info(aw8622x->dev, "%s vib_d2s_gain not found\n",
			    __func__);
	val =
	    of_property_read_u32(np, "aw8622x_vib_cont_brk_time",
				 &aw8622x->dts_info.cont_brk_time_dt);
	if (val != 0)
		aw_dev_info(aw8622x->dev, "%s vib_cont_brk_time not found\n",
			    __func__);
	val =
	    of_property_read_u32(np, "aw8622x_vib_cont_track_margin",
				 &aw8622x->dts_info.cont_track_margin);
	if (val != 0)
		aw_dev_info(aw8622x->dev,
			"%s vib_cont_track_margin not found\n", __func__);

	val = of_property_read_u32_array(np, "aw8622x_vib_prctmode",
					 prctmode_temp,
					 ARRAY_SIZE(prctmode_temp));
	if (val != 0)
		aw_dev_info(aw8622x->dev, "%s vib_prctmode not found\n",
			    __func__);
	memcpy(aw8622x->dts_info.prctmode, prctmode_temp,
					sizeof(prctmode_temp));
	val = of_property_read_u32_array(np,
				"aw8622x_vib_sine_array",
				sine_array_temp,
				ARRAY_SIZE(sine_array_temp));
	if (val != 0)
		aw_dev_info(aw8622x->dev, "%s vib_sine_array not found\n",
			    __func__);
	memcpy(aw8622x->dts_info.sine_array, sine_array_temp,
		sizeof(sine_array_temp));
	val =
	    of_property_read_u32_array(np,
				"aw8622x_vib_trig_config",
				trig_config_temp,
				ARRAY_SIZE(trig_config_temp));
	if (val != 0)
		aw_dev_info(aw8622x->dev, "%s vib_trig_config not found\n",
			    __func__);
	memcpy(aw8622x->dts_info.trig_config, trig_config_temp,
	       sizeof(trig_config_temp));
	val = of_property_read_u32_array(np, "aw8622x_vib_duration_time",
		duration_time, ARRAY_SIZE(duration_time));
	if (val != 0)
		aw_dev_info(aw8622x->dev,
			"%s vib_duration_time not found\n", __func__);
	ret = aw8622x_analyse_duration_array_size(aw8622x, np);
	if (!ret)
		memcpy(aw8622x->dts_info.duration_time,
			duration_time, sizeof(duration_time));
	aw8622x->dts_info.is_enabled_auto_bst =
			of_property_read_bool(np,
					"aw8622x_vib_is_enabled_auto_bst");
	aw_dev_info(aw8622x->dev,
		    "%s aw8622x->info.is_enabled_auto_bst = %d\n", __func__,
		    aw8622x->dts_info.is_enabled_auto_bst);

	return 0;
}

static void aw8622x_haptic_upload_lra(struct aw8622x *aw8622x,
				      unsigned int flag)
{
	switch (flag) {
	case WRITE_ZERO:
		//aw_dev_info(aw8622x->dev, "%s write zero to trim_lra!\n",
		//	    __func__);
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_TRIMCFG3,
				       AW8622X_BIT_TRIMCFG3_TRIM_LRA_MASK,
				       0x00);
		break;
	case F0_CALI:
		//aw_dev_info(aw8622x->dev, "%s write f0_cali_data to trim_lra = 0x%02X\n",
		//	    __func__, aw8622x->f0_cali_data);
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_TRIMCFG3,
				       AW8622X_BIT_TRIMCFG3_TRIM_LRA_MASK,
				       (char)aw8622x->f0_cali_data);
		break;
	case OSC_CALI:
		//aw_dev_info(aw8622x->dev, "%s write osc_cali_data to trim_lra = 0x%02X\n",
		//	    __func__, aw8622x->osc_cali_data);
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_TRIMCFG3,
				       AW8622X_BIT_TRIMCFG3_TRIM_LRA_MASK,
				       (char)aw8622x->osc_cali_data);
		break;
	default:
		break;
	}
}



/*****************************************************
 *
 * sram size, normally 3k(2k fifo, 1k ram)
 *
 *****************************************************/
static int aw8622x_sram_size(struct aw8622x *aw8622x, int size_flag)
{
	if (size_flag == AW8622X_HAPTIC_SRAM_2K) {
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_RTPCFG1,
				       AW8622X_BIT_RTPCFG1_SRAM_SIZE_2K_MASK,
				       AW8622X_BIT_RTPCFG1_SRAM_SIZE_2K_EN);
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_RTPCFG1,
				       AW8622X_BIT_RTPCFG1_SRAM_SIZE_1K_MASK,
				       AW8622X_BIT_RTPCFG1_SRAM_SIZE_1K_DIS);
	} else if (size_flag == AW8622X_HAPTIC_SRAM_1K) {
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_RTPCFG1,
				       AW8622X_BIT_RTPCFG1_SRAM_SIZE_2K_MASK,
				       AW8622X_BIT_RTPCFG1_SRAM_SIZE_2K_DIS);
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_RTPCFG1,
				       AW8622X_BIT_RTPCFG1_SRAM_SIZE_1K_MASK,
				       AW8622X_BIT_RTPCFG1_SRAM_SIZE_1K_EN);
	} else if (size_flag == AW8622X_HAPTIC_SRAM_3K) {
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_RTPCFG1,
				       AW8622X_BIT_RTPCFG1_SRAM_SIZE_1K_MASK,
				       AW8622X_BIT_RTPCFG1_SRAM_SIZE_1K_EN);
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_RTPCFG1,
				       AW8622X_BIT_RTPCFG1_SRAM_SIZE_2K_MASK,
				       AW8622X_BIT_RTPCFG1_SRAM_SIZE_2K_EN);
	}
	return 0;
}

static int aw8622x_haptic_stop(struct aw8622x *aw8622x)
{
	unsigned char cnt = 40;
	unsigned char reg_val = 0;
	bool force_flag = true;

	if (aw8622x->vib_stop_flag == true)
		return 0;

	//aw_dev_info(aw8622x->dev, "%s enter\n", __func__);
	aw8622x->play_mode = AW8622X_HAPTIC_STANDBY_MODE;
	aw8622x_i2c_write(aw8622x, AW8622X_REG_PLAYCFG4, 0x02);
	while (cnt) {
		aw8622x_i2c_read(aw8622x, AW8622X_REG_GLBRD5, &reg_val);
		if ((reg_val & 0x0f) == 0x00
		    || (reg_val & 0x0f) == 0x0A) {
			cnt = 0;
			force_flag = false;
			//aw_dev_info(aw8622x->dev, "%s entered standby! glb_state=0x%02X\n",
			//	    __func__, reg_val);
		} else {
			cnt--;
			aw_dev_dbg(aw8622x->dev, "%s wait for standby, glb_state=0x%02X\n",
			     __func__, reg_val);
		}
		usleep_range(2000, 2500);
	}

	if (force_flag) {
		aw_dev_err(aw8622x->dev, "%s force to enter standby mode!\n",
			   __func__);
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL2,
				       AW8622X_BIT_SYSCTRL2_STANDBY_MASK,
				       AW8622X_BIT_SYSCTRL2_STANDBY_ON);
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL2,
				       AW8622X_BIT_SYSCTRL2_STANDBY_MASK,
				       AW8622X_BIT_SYSCTRL2_STANDBY_OFF);
	}
	aw8622x->vib_stop_flag = true;
	return 0;
}

static void aw8622x_haptic_raminit(struct aw8622x *aw8622x, bool flag)
{
	if (flag) {
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL1,
				       AW8622X_BIT_SYSCTRL1_RAMINIT_MASK,
				       AW8622X_BIT_SYSCTRL1_RAMINIT_ON);
	} else {
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL1,
				       AW8622X_BIT_SYSCTRL1_RAMINIT_MASK,
				       AW8622X_BIT_SYSCTRL1_RAMINIT_OFF);
	}
}

static int aw8622x_haptic_get_vbat(struct aw8622x *aw8622x)
{
	unsigned char reg_val = 0;
	unsigned int vbat_code = 0;
	/*unsigned int cont = 2000;*/

	aw8622x_haptic_stop(aw8622x);
	aw8622x_haptic_raminit(aw8622x, true);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_DETCFG2,
			       AW8622X_BIT_DETCFG2_VBAT_GO_MASK,
			       AW8622X_BIT_DETCFG2_VABT_GO_ON);
	usleep_range(20000, 25000);
	aw8622x_i2c_read(aw8622x, AW8622X_REG_DET_VBAT, &reg_val);
	vbat_code = (vbat_code | reg_val) << 2;
	aw8622x_i2c_read(aw8622x, AW8622X_REG_DET_LO, &reg_val);
	vbat_code = vbat_code | ((reg_val & 0x30) >> 4);
	aw8622x->vbat = 6100 * vbat_code / 1024;
	if (aw8622x->vbat > AW8622X_VBAT_MAX) {
		aw8622x->vbat = AW8622X_VBAT_MAX;
		aw_dev_info(aw8622x->dev, "%s vbat max limit = %dmV\n",
			__func__, aw8622x->vbat);
	}
	if (aw8622x->vbat < AW8622X_VBAT_MIN) {
		aw8622x->vbat = AW8622X_VBAT_MIN;
		aw_dev_info(aw8622x->dev, "%s vbat min limit = %dmV\n",
			    __func__, aw8622x->vbat);
	}
	//aw_dev_info(aw8622x->dev, "%s aw8622x->vbat=%dmV, vbat_code=0x%02X\n",
	//	    __func__, aw8622x->vbat, vbat_code);
	aw8622x_haptic_raminit(aw8622x, false);
	return 0;
}

/*****************************************************
 *
 * rtp brk
 *
 *****************************************************/
/*
*static int aw8622x_rtp_brake_set(struct aw8622x *aw8622x) {
*     aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_CONTCFG1,
*			AW8622X_BIT_CONTCFG1_MBRK_MASK,
*			AW8622X_BIT_CONTCFG1_MBRK_ENABLE);
*
*     aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL7,
*			AW8622X_BIT_SYSCTRL7_D2S_GAIN_MASK,
*			0x05);
*     return 0;
*}
*/

static void aw8622x_interrupt_clear(struct aw8622x *aw8622x)
{
	unsigned char reg_val = 0;

	aw_dev_info(aw8622x->dev, "%s enter\n", __func__);
	aw8622x_i2c_read(aw8622x, AW8622X_REG_SYSINT, &reg_val);
	aw_dev_dbg(aw8622x->dev, "%s: reg SYSINT=0x%02X\n", __func__, reg_val);
}

static int aw8622x_haptic_set_gain(struct aw8622x *aw8622x, unsigned char gain)
{
	//aw_dev_info(aw8622x->dev, "%s: gain=%d\n", __func__, gain);
	aw8622x_i2c_write(aw8622x, AW8622X_REG_PLAYCFG2, gain);
	return 0;
}

static int aw8622x_haptic_ram_vbat_compensate(struct aw8622x *aw8622x,
					      bool flag)
{
	int temp_gain = 0;

	if (flag) {
		if (aw8622x->ram_vbat_compensate ==
		    AW8622X_HAPTIC_RAM_VBAT_COMP_ENABLE) {
			aw8622x_haptic_get_vbat(aw8622x);
			temp_gain =
			    aw8622x->gain * AW8622X_VBAT_REFER / aw8622x->vbat;
			if (temp_gain >
			    (128 * AW8622X_VBAT_REFER / AW8622X_VBAT_MIN)) {
				temp_gain =
				    128 * AW8622X_VBAT_REFER / AW8622X_VBAT_MIN;
				aw_dev_dbg(aw8622x->dev, "%s gain limit=%d\n",
					   __func__, temp_gain);
			}
			aw8622x_haptic_set_gain(aw8622x, temp_gain);
		} else {
			aw8622x_haptic_set_gain(aw8622x, aw8622x->gain);
		}
	} else {
		aw8622x_haptic_set_gain(aw8622x, aw8622x->gain);
	}
	return 0;
}

static int aw8622x_haptic_play_mode(struct aw8622x *aw8622x,
				    unsigned char play_mode)
{
	//aw_dev_info(aw8622x->dev, "%s enter\n", __func__);

	switch (play_mode) {
	case AW8622X_HAPTIC_STANDBY_MODE:
		aw_dev_info(aw8622x->dev, "%s: enter standby mode\n", __func__);
		aw8622x->play_mode = AW8622X_HAPTIC_STANDBY_MODE;
		aw8622x_haptic_stop(aw8622x);
		break;
	case AW8622X_HAPTIC_RAM_MODE:
		aw_dev_info(aw8622x->dev, "%s: enter ram mode\n", __func__);
		aw8622x->play_mode = AW8622X_HAPTIC_RAM_MODE;
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_PLAYCFG3,
				       AW8622X_BIT_PLAYCFG3_PLAY_MODE_MASK,
				       AW8622X_BIT_PLAYCFG3_PLAY_MODE_RAM);
		break;
	case AW8622X_HAPTIC_RAM_LOOP_MODE:
		aw_dev_info(aw8622x->dev, "%s: enter ram loop mode\n",
			    __func__);
		aw8622x->play_mode = AW8622X_HAPTIC_RAM_LOOP_MODE;
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_PLAYCFG3,
				       AW8622X_BIT_PLAYCFG3_PLAY_MODE_MASK,
				       AW8622X_BIT_PLAYCFG3_PLAY_MODE_RAM);
		break;
	case AW8622X_HAPTIC_RTP_MODE:
		//aw_dev_info(aw8622x->dev, "%s: enter rtp mode\n", __func__);
		aw8622x->play_mode = AW8622X_HAPTIC_RTP_MODE;
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_PLAYCFG3,
				       AW8622X_BIT_PLAYCFG3_PLAY_MODE_MASK,
				       AW8622X_BIT_PLAYCFG3_PLAY_MODE_RTP);
		break;
	case AW8622X_HAPTIC_TRIG_MODE:
		aw_dev_info(aw8622x->dev, "%s: enter trig mode\n", __func__);
		aw8622x->play_mode = AW8622X_HAPTIC_TRIG_MODE;
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_PLAYCFG3,
				       AW8622X_BIT_PLAYCFG3_PLAY_MODE_MASK,
				       AW8622X_BIT_PLAYCFG3_PLAY_MODE_RAM);
		break;
	case AW8622X_HAPTIC_CONT_MODE:
		aw_dev_info(aw8622x->dev, "%s: enter cont mode\n", __func__);
		aw8622x->play_mode = AW8622X_HAPTIC_CONT_MODE;
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_PLAYCFG3,
				       AW8622X_BIT_PLAYCFG3_PLAY_MODE_MASK,
				       AW8622X_BIT_PLAYCFG3_PLAY_MODE_CONT);
		break;
	default:
		aw_dev_err(aw8622x->dev, "%s: play mode %d error",
			   __func__, play_mode);
		break;
	}
	return 0;
}

static int aw8622x_haptic_play_go(struct aw8622x *aw8622x, bool flag)
{

	//aw_dev_dbg(aw8622x->dev, "%s enter\n", __func__);
	if (flag == true) {
		aw8622x_i2c_write(aw8622x, AW8622X_REG_PLAYCFG4, 0x01);
		mdelay(2);
		aw8622x->vib_stop_flag = false;
	} else {
		aw8622x_i2c_write(aw8622x, AW8622X_REG_PLAYCFG4, 0x02);
	}

	return 0;
}

static int aw8622x_haptic_set_wav_seq(struct aw8622x *aw8622x,
				      unsigned char wav, unsigned char seq)
{
	aw8622x_i2c_write(aw8622x, AW8622X_REG_WAVCFG1 + wav, seq);
	return 0;
}

static int aw8622x_haptic_set_wav_loop(struct aw8622x *aw8622x,
				       unsigned char wav, unsigned char loop)
{
	unsigned char tmp = 0;

	if (wav % 2) {
		tmp = loop << 0;
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_WAVCFG9 + (wav / 2),
				       AW8622X_BIT_WAVLOOP_SEQ_EVEN_MASK, tmp);
	} else {
		tmp = loop << 4;
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_WAVCFG9 + (wav / 2),
				       AW8622X_BIT_WAVLOOP_SEQ_ODD_MASK, tmp);
	}
	return 0;
}

/*****************************************************
 *
 * haptic f0 cali
 *
 *****************************************************/
static int aw8622x_haptic_read_lra_f0(struct aw8622x *aw8622x)
{
	int ret = 0;
	unsigned char reg_val = 0;
	unsigned int f0_reg = 0;
	unsigned long f0_tmp = 0;

	aw_dev_info(aw8622x->dev, "%s enter\n", __func__);
	/* F_LRA_F0_H */
	ret = aw8622x_i2c_read(aw8622x, AW8622X_REG_CONTRD14, &reg_val);
	f0_reg = (f0_reg | reg_val) << 8;
	/* F_LRA_F0_L */
	ret = aw8622x_i2c_read(aw8622x, AW8622X_REG_CONTRD15, &reg_val);
	f0_reg |= (reg_val << 0);
	if (!f0_reg) {
		aw_dev_err(aw8622x->dev, "%s didn't get lra f0 because f0_reg value is 0!\n",
			   __func__);
		aw8622x->f0 = aw8622x->dts_info.f0_ref;
		aw8622x->raw_f0 = 0;
		return -ERANGE;
	} else {
		f0_tmp = 384000 * 10 / f0_reg;
		aw8622x->f0 = (unsigned int)f0_tmp;
		aw8622x->raw_f0 = (unsigned int)f0_tmp;
		aw_dev_info(aw8622x->dev, "%s lra_f0=%d\n", __func__,
			    aw8622x->f0);
	}

	return 0;
}

static int aw8622x_haptic_read_cont_f0(struct aw8622x *aw8622x)
{
	int ret = 0;
	unsigned char reg_val = 0;
	unsigned int f0_reg = 0;
	unsigned long f0_tmp = 0;

	aw_dev_info(aw8622x->dev, "%s enter\n", __func__);
	ret = aw8622x_i2c_read(aw8622x, AW8622X_REG_CONTRD16, &reg_val);
	f0_reg = (f0_reg | reg_val) << 8;
	ret = aw8622x_i2c_read(aw8622x, AW8622X_REG_CONTRD17, &reg_val);
	f0_reg |= (reg_val << 0);
	if (!f0_reg) {
		aw_dev_err(aw8622x->dev, "%s didn't get cont f0 because f0_reg value is 0!\n",
			   __func__);
		aw8622x->cont_f0 = aw8622x->dts_info.f0_ref;
		return -ERANGE;
	} else {
		f0_tmp = 384000 * 10 / f0_reg;
		aw8622x->cont_f0 = (unsigned int)f0_tmp;
		aw_dev_info(aw8622x->dev, "%s cont_f0=%d\n", __func__,
			    aw8622x->cont_f0);
	}
	return 0;
}

static int aw8622x_haptic_cont_get_f0(struct aw8622x *aw8622x)
{
	int ret = 0;
	unsigned char reg_val = 0;
	unsigned int cnt = 200;
	bool get_f0_flag = false;
	unsigned char brk_en_temp = 0;

	aw_dev_info(aw8622x->dev, "%s enter\n", __func__);
	aw8622x->f0 = aw8622x->dts_info.f0_ref;
	/* enter standby mode */
	aw8622x_haptic_stop(aw8622x);
	/* f0 calibrate work mode */
	aw8622x_haptic_play_mode(aw8622x, AW8622X_HAPTIC_CONT_MODE);
	/* enable f0 detect */
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_CONTCFG1,
			       AW8622X_BIT_CONTCFG1_EN_F0_DET_MASK,
			       AW8622X_BIT_CONTCFG1_F0_DET_ENABLE);
	/* cont config */
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_CONTCFG6,
			       AW8622X_BIT_CONTCFG6_TRACK_EN_MASK,
			       AW8622X_BIT_CONTCFG6_TRACK_ENABLE);
	/* enable auto brake */
	aw8622x_i2c_read(aw8622x, AW8622X_REG_PLAYCFG3, &reg_val);
	brk_en_temp = 0x04 & reg_val;
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_PLAYCFG3,
			       AW8622X_BIT_PLAYCFG3_BRK_EN_MASK,
			       AW8622X_BIT_PLAYCFG3_BRK_ENABLE);
	/* f0 driver level */
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_CONTCFG6,
			       AW8622X_BIT_CONTCFG6_DRV1_LVL_MASK,
			       aw8622x->dts_info.cont_drv1_lvl_dt);
	aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG7,
			  aw8622x->dts_info.cont_drv2_lvl_dt);
	/* DRV1_TIME */
	aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG8,
			  aw8622x->dts_info.cont_drv1_time_dt);
	/* DRV2_TIME */
	aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG9,
			  aw8622x->dts_info.cont_drv2_time_dt);
	/* TRACK_MARGIN */
	if (!aw8622x->dts_info.cont_track_margin) {
		aw_dev_err(aw8622x->dev, "%s aw8622x->dts_info.cont_track_margin = 0!\n",
			   __func__);
	} else {
		aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG11,
				  (unsigned char)aw8622x->dts_info.
				  cont_track_margin);
	}
	/* DRV_WIDTH */
	/*
	 * aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG3,
	 *                aw8622x->dts_info.cont_drv_width);
	 */
	/* cont play go */
	aw8622x_haptic_play_go(aw8622x, true);
	/* 300ms */
	while (cnt) {
		aw8622x_i2c_read(aw8622x, AW8622X_REG_GLBRD5, &reg_val);
		if ((reg_val & 0x0f) == 0x00) {
			cnt = 0;
			get_f0_flag = true;
			aw_dev_info(aw8622x->dev, "%s entered standby mode! glb_state=0x%02X\n",
				    __func__, reg_val);
		} else {
			cnt--;
			aw_dev_info(aw8622x->dev, "%s waitting for standby, glb_state=0x%02X\n",
				    __func__, reg_val);
		}
		usleep_range(10000, 10500);
	}
	if (get_f0_flag) {
		aw8622x_haptic_read_lra_f0(aw8622x);
		aw8622x_haptic_read_cont_f0(aw8622x);
	} else {
		aw_dev_err(aw8622x->dev, "%s enter standby mode failed, stop reading f0!\n",
			   __func__);
	}
	/* restore default config */
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_CONTCFG1,
			       AW8622X_BIT_CONTCFG1_EN_F0_DET_MASK,
			       AW8622X_BIT_CONTCFG1_F0_DET_DISABLE);
	/* recover auto break config */
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_PLAYCFG3,
			       AW8622X_BIT_PLAYCFG3_BRK_EN_MASK,
			       brk_en_temp);
	return ret;
}

static int aw8622x_haptic_rtp_init(struct aw8622x *aw8622x)
{
	unsigned int buf_len = 0;
	unsigned char glb_state_val = 0;

	aw_dev_info(aw8622x->dev, "%s enter\n", __func__);
	pm_qos_add_request(&aw8622x_pm_qos_req_vb, PM_QOS_CPU_DMA_LATENCY,
			   AW8622X_PM_QOS_VALUE_VB);
	aw8622x->rtp_cnt = 0;
	mutex_lock(&aw8622x->rtp_lock);
	while ((!aw8622x_haptic_rtp_get_fifo_afs(aw8622x))
	       && (aw8622x->play_mode == AW8622X_HAPTIC_RTP_MODE)) {
		aw_dev_info(aw8622x->dev, "%s rtp cnt = %d\n", __func__,
			    aw8622x->rtp_cnt);
		if (!aw8622x->rtp_container) {
			aw_dev_info(aw8622x->dev, "%s:aw8622x->rtp_container is null, break!\n",
				    __func__);
			break;
		}
		if (aw8622x->rtp_cnt < (aw8622x->ram.base_addr)) {
			if ((aw8622x->rtp_container->len - aw8622x->rtp_cnt) <
			    (aw8622x->ram.base_addr)) {
				buf_len = aw8622x->rtp_container->len - aw8622x->rtp_cnt;
			} else {
				buf_len = aw8622x->ram.base_addr;
			}
		} else if ((aw8622x->rtp_container->len - aw8622x->rtp_cnt) <
			   (aw8622x->ram.base_addr >> 2)) {
			buf_len = aw8622x->rtp_container->len - aw8622x->rtp_cnt;
		} else {
			buf_len = aw8622x->ram.base_addr >> 2;
		}
		aw_dev_info(aw8622x->dev, "%s buf_len = %d\n", __func__,
			    buf_len);
		aw8622x_i2c_writes(aw8622x, AW8622X_REG_RTPDATA,
				   &aw8622x->rtp_container->data[aw8622x->rtp_cnt],
				   buf_len);
		aw8622x->rtp_cnt += buf_len;
		aw8622x_i2c_read(aw8622x, AW8622X_REG_GLBRD5, &glb_state_val);
		if ((aw8622x->rtp_cnt == aw8622x->rtp_container->len)
		    || ((glb_state_val & 0x0f) == 0x00)) {
			if (aw8622x->rtp_cnt == aw8622x->rtp_container->len)
				aw_dev_info(aw8622x->dev,
					"%s: rtp load completely! glb_state_val=%02x aw8622x->rtp_cnt=%d\n",
					__func__, glb_state_val,
					aw8622x->rtp_cnt);
			else
				aw_dev_err(aw8622x->dev,
					"%s rtp load failed!! glb_state_val=%02x aw8622x->rtp_cnt=%d\n",
					__func__, glb_state_val,
					aw8622x->rtp_cnt);
			aw8622x->rtp_cnt = 0;
			pm_qos_remove_request(&aw8622x_pm_qos_req_vb);
			mutex_unlock(&aw8622x->rtp_lock);
			return 0;
		}
	}

	if (aw8622x->play_mode == AW8622X_HAPTIC_RTP_MODE)
		aw8622x_haptic_set_rtp_aei(aw8622x, true);

	aw_dev_info(aw8622x->dev, "%s exit\n", __func__);
	mutex_unlock(&aw8622x->rtp_lock);
	pm_qos_remove_request(&aw8622x_pm_qos_req_vb);
	return 0;
}

static int aw8622x_haptic_ram_config(struct aw8622x *aw8622x, int duration)
{
	unsigned char wavseq = 0;
	unsigned char wavloop = 0;
	int ret = 0;

	if (aw8622x->duration_time_flag < 0) {
		aw_dev_err(aw8622x->dev,
			"%s: duration time error, array size = %d\n",
			__func__, aw8622x->duration_time_size);
		return -ERANGE;
	}
	ret = aw8622x_analyse_duration_range(aw8622x);
	if (ret < 0)
		return ret;

#if 0
	if ((duration > 0) && (duration <
				aw8622x->dts_info.duration_time[0])) {
		wavseq = 3;	/*3*/
		wavloop = 0;
	} else if ((duration >= aw8622x->dts_info.duration_time[0]) &&
		(duration < aw8622x->dts_info.duration_time[1])) {
		wavseq = 2;	/*2*/
		wavloop = 0;
	} else if ((duration >= aw8622x->dts_info.duration_time[1]) &&
		(duration < aw8622x->dts_info.duration_time[2])) {
		wavseq = 1;	/*1*/
		wavloop = 0;
	} else if(duration >= aw8622x->dts_info.duration_time[2]) {
		wavseq = 4;	/*4*/
		wavloop = 15;	/*long vibration*/
	} else {
		wavseq = 0;
		wavloop = 0;
	}
#endif
	if (duration ==200) {
		duration ==100; /*wrong face unlock*/
	}

	if (duration ==12) {
		duration ==0; /*ring tone*/
	}

	if (duration ==65 || duration ==95) {
		wavseq = 1;	/*1-short-strong*/
		wavloop = 0;
	} else if ((duration > 0) && (duration < aw8622x->dts_info.duration_time[0]) && duration !=12) {
		wavseq = 3;	/*0-30, 3-short-weak*/
		wavloop = 0;
	} else if ((duration >= aw8622x->dts_info.duration_time[0]) &&
		(duration < aw8622x->dts_info.duration_time[1])) {
		wavseq = 1;	/*30-60, 1-short-strong*/
		wavloop = 0;
	} else if ((duration >= aw8622x->dts_info.duration_time[1]) &&
		(duration < aw8622x->dts_info.duration_time[2]) && duration !=65) {
		wavseq = 5;	/*60-90, 5-long-weak*/
		wavloop = 15;   /*long vibration*/
	} else if(duration >= aw8622x->dts_info.duration_time[2] && duration !=95) {
		wavseq = 4;	/*90-, 4-long-strong*/
		wavloop = 15;	/*long vibration*/
	} else {
		wavseq = 0;
		wavloop = 0;
	}

	aw8622x_haptic_set_wav_seq(aw8622x, 0, wavseq);
	aw8622x_haptic_set_wav_loop(aw8622x, 0, wavloop);
	aw8622x_haptic_set_wav_seq(aw8622x, 1, 0);
	aw8622x_haptic_set_wav_loop(aw8622x, 1, 0);

	return 0;
}

static unsigned char aw8622x_haptic_osc_read_status(struct aw8622x *aw8622x)
{
	unsigned char reg_val = 0;

	aw8622x_i2c_read(aw8622x, AW8622X_REG_SYSST2, &reg_val);
	return reg_val;
}

static int aw8622x_haptic_set_repeat_wav_seq(struct aw8622x *aw8622x,
					     unsigned char seq)
{
	aw8622x_haptic_set_wav_seq(aw8622x, 0x00, seq);
	aw8622x_haptic_set_wav_loop(aw8622x, 0x00,
				    AW8622X_BIT_WAVLOOP_INIFINITELY);
	return 0;
}

static void aw8622x_rtp_work_routine(struct work_struct *work)
{
	const struct firmware *rtp_file;
	int ret = -1;
	unsigned int cnt = 200;
	unsigned char reg_val = 0;
	bool rtp_work_flag = false;
	struct aw8622x *aw8622x = container_of(work, struct aw8622x, rtp_work);

	aw_dev_info(aw8622x->dev, "%s enter\n", __func__);
	mutex_lock(&aw8622x->rtp_lock);
	/* fw loaded */
	ret = request_firmware(&rtp_file,
			       aw8622x_rtp_name[aw8622x->rtp_file_num],
			       aw8622x->dev);
	if (ret < 0) {
		aw_dev_err(aw8622x->dev, "%s: failed to read %s\n", __func__,
			   aw8622x_rtp_name[aw8622x->rtp_file_num]);
		mutex_unlock(&aw8622x->rtp_lock);
		return;
	}
	aw8622x->rtp_init = 0;
	vfree(aw8622x->rtp_container);
	aw8622x->rtp_container = vmalloc(rtp_file->size + sizeof(int));
	if (!aw8622x->rtp_container) {
		release_firmware(rtp_file);
		aw_dev_err(aw8622x->dev, "%s: error allocating memory\n",
			   __func__);
		mutex_unlock(&aw8622x->rtp_lock);
		return;
	}
	aw8622x->rtp_container->len = rtp_file->size;
	aw_dev_info(aw8622x->dev, "%s: rtp file:[%s] size = %dbytes\n",
		    __func__, aw8622x_rtp_name[aw8622x->rtp_file_num],
		    aw8622x->rtp_container->len);
	memcpy(aw8622x->rtp_container->data, rtp_file->data, rtp_file->size);
	mutex_unlock(&aw8622x->rtp_lock);
	release_firmware(rtp_file);
	mutex_lock(&aw8622x->lock);
	aw8622x->rtp_init = 1;
	aw8622x_haptic_upload_lra(aw8622x, OSC_CALI);
	/* gain */
	aw8622x_haptic_ram_vbat_compensate(aw8622x, false);
	/* rtp mode config */
	aw8622x_haptic_play_mode(aw8622x, AW8622X_HAPTIC_RTP_MODE);
	/* haptic go */
	aw8622x_haptic_play_go(aw8622x, true);
	mutex_unlock(&aw8622x->lock);
	usleep_range(2000, 2500);
	while (cnt) {
		aw8622x_i2c_read(aw8622x, AW8622X_REG_GLBRD5, &reg_val);
		if ((reg_val & 0x0f) == 0x08) {
			cnt = 0;
			rtp_work_flag = true;
			aw_dev_info(aw8622x->dev, "%s RTP_GO! glb_state=0x08\n",
				    __func__);
		} else {
			cnt--;
			aw_dev_dbg(aw8622x->dev, "%s wait for RTP_GO, glb_state=0x%02X\n",
				   __func__, reg_val);
		}
		usleep_range(2000, 2500);
	}
	if (rtp_work_flag) {
		aw8622x_haptic_rtp_init(aw8622x);
	} else {
		/* enter standby mode */
		aw8622x_haptic_stop(aw8622x);
		aw_dev_err(aw8622x->dev, "%s failed to enter RTP_GO status!\n",
			   __func__);
	}
}

static int aw8622x_rtp_osc_calibration(struct aw8622x *aw8622x)
{
	const struct firmware *rtp_file;
	int ret = -1;
	unsigned int buf_len = 0;
	unsigned char osc_int_state = 0;

	aw8622x->rtp_cnt = 0;
	aw8622x->timeval_flags = 1;

	aw_dev_info(aw8622x->dev, "%s enter\n", __func__);
	/* fw loaded */
	ret = request_firmware(&rtp_file, aw8622x_rtp_name[0], aw8622x->dev);
	if (ret < 0) {
		aw_dev_err(aw8622x->dev, "%s: failed to read %s\n", __func__,
			   aw8622x_rtp_name[0]);
		return ret;
	}
	/*awinic add stop,for irq interrupt during calibrate */
	aw8622x_haptic_stop(aw8622x);
	aw8622x->rtp_init = 0;
	mutex_lock(&aw8622x->rtp_lock);
	vfree(aw8622x->rtp_container);
	aw8622x->rtp_container = vmalloc(rtp_file->size + sizeof(int));
	if (!aw8622x->rtp_container) {
		release_firmware(rtp_file);
		mutex_unlock(&aw8622x->rtp_lock);
		aw_dev_err(aw8622x->dev, "%s: error allocating memory\n",
			   __func__);
		return -ENOMEM;
	}
	aw8622x->rtp_container->len = rtp_file->size;
	aw8622x->rtp_len = rtp_file->size;
	aw_dev_info(aw8622x->dev, "%s: rtp file:[%s] size = %dbytes\n",
		    __func__, aw8622x_rtp_name[0], aw8622x->rtp_container->len);

	memcpy(aw8622x->rtp_container->data, rtp_file->data, rtp_file->size);
	release_firmware(rtp_file);
	mutex_unlock(&aw8622x->rtp_lock);
	/* gain */
	aw8622x_haptic_ram_vbat_compensate(aw8622x, false);
	/* rtp mode config */
	aw8622x_haptic_play_mode(aw8622x, AW8622X_HAPTIC_RTP_MODE);

	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL7,
			       AW8622X_BIT_SYSCTRL7_INT_MODE_MASK,
			       AW8622X_BIT_SYSCTRL7_INT_MODE_EDGE);
	disable_irq(gpio_to_irq(aw8622x->irq_gpio));
	/* haptic go */
	aw8622x_haptic_play_go(aw8622x, true);
	/* require latency of CPU & DMA not more then PM_QOS_VALUE_VB us */
	pm_qos_add_request(&aw8622x_pm_qos_req_vb, PM_QOS_CPU_DMA_LATENCY,
			   AW8622X_PM_QOS_VALUE_VB);
	while (1) {
		if (!aw8622x_haptic_rtp_get_fifo_afs(aw8622x)) {
			mutex_lock(&aw8622x->rtp_lock);
			if ((aw8622x->rtp_container->len - aw8622x->rtp_cnt) <
			    (aw8622x->ram.base_addr >> 2))
				buf_len = aw8622x->rtp_container->len - aw8622x->rtp_cnt;
			else
				buf_len = (aw8622x->ram.base_addr >> 2);

			if (aw8622x->rtp_cnt != aw8622x->rtp_container->len) {
				if (aw8622x->timeval_flags == 1) {
#ifdef KERNEL_VERSION_49
					do_gettimeofday(&aw8622x->start);
#else
					aw8622x->kstart = ktime_get();
#endif
					aw8622x->timeval_flags = 0;
				}
				aw8622x->rtp_update_flag =
				    aw8622x_i2c_writes(aw8622x,
						AW8622X_REG_RTPDATA,
						&aw8622x->rtp_container->data
						[aw8622x->rtp_cnt],
						buf_len);
				aw8622x->rtp_cnt += buf_len;
			}
			mutex_unlock(&aw8622x->rtp_lock);
		}
		osc_int_state = aw8622x_haptic_osc_read_status(aw8622x);
		if (osc_int_state & AW8622X_BIT_SYSST2_FF_EMPTY) {
#ifdef KERNEL_VERSION_49
			do_gettimeofday(&aw8622x->end);
#else
			aw8622x->kend = ktime_get();
#endif
			pr_info
			    ("%s osc trim playback done aw8622x->rtp_cnt= %d\n",
			     __func__, aw8622x->rtp_cnt);
			break;
		}
#ifdef KERNEL_VERSION_49
		do_gettimeofday(&aw8622x->end);
		aw8622x->microsecond =
		    (aw8622x->end.tv_sec - aw8622x->start.tv_sec) * 1000000 +
		    (aw8622x->end.tv_usec - aw8622x->start.tv_usec);
#else
		aw8622x->kend = ktime_get();
		aw8622x->microsecond = ktime_to_us(ktime_sub(aw8622x->kend,
								aw8622x->kstart));
#endif


		if (aw8622x->microsecond > AW8622X_OSC_CALI_MAX_LENGTH) {
			aw_dev_info(aw8622x->dev,
				"%s osc trim time out! aw8622x->rtp_cnt %d osc_int_state %02x\n",
				__func__, aw8622x->rtp_cnt, osc_int_state);
			break;
		}
	}
	pm_qos_remove_request(&aw8622x_pm_qos_req_vb);
	enable_irq(gpio_to_irq(aw8622x->irq_gpio));
#ifdef KERNEL_VERSION_49
		aw8622x->microsecond =
			(aw8622x->end.tv_sec - aw8622x->start.tv_sec)*1000000 +
			(aw8622x->end.tv_usec - aw8622x->start.tv_usec);

#else
		aw8622x->microsecond = ktime_to_us(ktime_sub(aw8622x->kend,
							aw8622x->kstart));
#endif
	/*calibration osc */
	aw_dev_info(aw8622x->dev, "%s awinic_microsecond: %ld\n", __func__,
		    aw8622x->microsecond);
	aw_dev_info(aw8622x->dev, "%s exit\n", __func__);
	return 0;
}

static int aw8622x_osc_trim_calculation(unsigned long int theory_time,
					unsigned long int real_time)
{
	unsigned int real_code = 0;
	unsigned int lra_code = 0;
	unsigned int DFT_LRA_TRIM_CODE = 0;
	/*0.1 percent below no need to calibrate */
	unsigned int osc_cali_threshold = 10;

	aw_dev_info(g_aw8622x->dev, "%s enter\n", __func__);
	if (theory_time == real_time) {
		aw_dev_info(g_aw8622x->dev,
			"%s theory_time == real_time: %ld, no need to calibrate!\n",
			__func__, real_time);
		return 0;
	} else if (theory_time < real_time) {
		if ((real_time - theory_time) > (theory_time / 50)) {
			aw_dev_info(g_aw8622x->dev,
				"%s (real_time - theory_time) > (theory_time/50), can't calibrate!\n",
				__func__);
			return DFT_LRA_TRIM_CODE;
		}

		if ((real_time - theory_time) <
		    (osc_cali_threshold * theory_time / 10000)) {
			aw_dev_info(g_aw8622x->dev,
				"%s real_time: %ld, theory_time: %ld, no need to calibrate!\n",
				__func__,
				real_time, theory_time);
			return DFT_LRA_TRIM_CODE;
		}

		real_code = ((real_time - theory_time) * 4000) / theory_time;
		real_code = ((real_code % 10 < 5) ? 0 : 1) + real_code / 10;
		real_code = 32 + real_code;
	} else if (theory_time > real_time) {
		if ((theory_time - real_time) > (theory_time / 50)) {
			aw_dev_info(g_aw8622x->dev,
				"%s (theory_time - real_time) > (theory_time / 50), can't calibrate!\n",
				__func__);
			return DFT_LRA_TRIM_CODE;
		}
		if ((theory_time - real_time) <
		    (osc_cali_threshold * theory_time / 10000)) {
			aw_dev_info(g_aw8622x->dev,
				"%s real_time: %ld, theory_time: %ld, no need to calibrate!\n",
				__func__,
				real_time, theory_time);
			return DFT_LRA_TRIM_CODE;
		}
		real_code = ((theory_time - real_time) * 4000) / theory_time;
		real_code = ((real_code % 10 < 5) ? 0 : 1) + real_code / 10;
		real_code = 32 - real_code;
	}
	if (real_code > 31)
		lra_code = real_code - 32;
	else
		lra_code = real_code + 32;
	aw_dev_info(g_aw8622x->dev,
		"%s real_time: %ld, theory_time: %ld\n", __func__, real_time,
		theory_time);
	aw_dev_info(g_aw8622x->dev,
		"%s real_code: %02X, trim_lra: 0x%02X\n", __func__, real_code,
		lra_code);
	return lra_code;
}

static int aw8622x_haptic_get_lra_resistance(struct aw8622x *aw8622x)
{
	unsigned char reg_val = 0;
	unsigned char d2s_gain_temp = 0;
	unsigned int lra_code = 0;
	unsigned int lra = 0;

	mutex_lock(&aw8622x->lock);
	aw8622x_haptic_stop(aw8622x);
	aw8622x_i2c_read(aw8622x, AW8622X_REG_SYSCTRL7, &reg_val);
	d2s_gain_temp = 0x07 & reg_val;
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL7,
			       AW8622X_BIT_SYSCTRL7_D2S_GAIN_MASK,
			       aw8622x->dts_info.d2s_gain);
	aw8622x_haptic_raminit(aw8622x, true);
	/* enter standby mode */
	aw8622x_haptic_stop(aw8622x);
	usleep_range(2000, 2500);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL2,
			       AW8622X_BIT_SYSCTRL2_STANDBY_MASK,
			       AW8622X_BIT_SYSCTRL2_STANDBY_OFF);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_DETCFG1,
			       AW8622X_BIT_DETCFG1_RL_OS_MASK,
			       AW8622X_BIT_DETCFG1_RL);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_DETCFG2,
			       AW8622X_BIT_DETCFG2_DIAG_GO_MASK,
			       AW8622X_BIT_DETCFG2_DIAG_GO_ON);
	usleep_range(30000, 35000);
	aw8622x_i2c_read(aw8622x, AW8622X_REG_DET_RL, &reg_val);
	lra_code = (lra_code | reg_val) << 2;
	aw8622x_i2c_read(aw8622x, AW8622X_REG_DET_LO, &reg_val);
	lra_code = lra_code | (reg_val & 0x03);
	/* 2num */
	lra = (lra_code * 678 * 100) / (1024 * 10);
	/* Keep up with aw8624 driver */
	aw8622x->lra = lra * 10;
	aw8622x_haptic_raminit(aw8622x, false);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL7,
			       AW8622X_BIT_SYSCTRL7_D2S_GAIN_MASK,
			       d2s_gain_temp);
	mutex_unlock(&aw8622x->lock);
	return 0;
}

static int aw8622x_haptic_juge_RTP_is_going_on(struct aw8622x *aw8622x)
{
	unsigned char rtp_state = 0;
	unsigned char mode = 0;
	unsigned char glb_st = 0;

	aw8622x_i2c_read(aw8622x, AW8622X_REG_PLAYCFG3, &mode);
	aw8622x_i2c_read(aw8622x, AW8622X_REG_GLBRD5, &glb_st);
	if ((mode & AW8622X_BIT_PLAYCFG3_PLAY_MODE_RTP) &&
		(glb_st == AW8622X_BIT_GLBRD5_STATE_RTP_GO)) {
		rtp_state = 1;
	}
	return rtp_state;
}

static int aw8622x_container_update(struct aw8622x *aw8622x,
				     struct aw8622x_container *aw8622x_cont)
{
	unsigned char reg_val = 0;
	unsigned int shift = 0;
	unsigned int temp = 0;
	int i = 0;
	int ret = 0;
#ifdef AW_CHECK_RAM_DATA
	unsigned short check_sum = 0;
#endif

	aw_dev_info(aw8622x->dev, "%s enter\n", __func__);
	mutex_lock(&aw8622x->lock);
	aw8622x->ram.baseaddr_shift = 2;
	aw8622x->ram.ram_shift = 4;
	/* RAMINIT Enable */
	aw8622x_haptic_raminit(aw8622x, true);
	/* Enter standby mode */
	aw8622x_haptic_stop(aw8622x);
	/* base addr */
	shift = aw8622x->ram.baseaddr_shift;
	aw8622x->ram.base_addr =
	    (unsigned int)((aw8622x_cont->data[0 + shift] << 8) |
			   (aw8622x_cont->data[1 + shift]));

	/* default 3k SRAM */
	aw8622x_sram_size(aw8622x, AW8622X_HAPTIC_SRAM_3K);

	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_RTPCFG1, /*ADDRH*/
			AW8622X_BIT_RTPCFG1_ADDRH_MASK,
			aw8622x_cont->data[0 + shift]);

	aw8622x_i2c_write(aw8622x, AW8622X_REG_RTPCFG2, /*ADDRL*/
			aw8622x_cont->data[1 + shift]);

	/* FIFO_AEH */
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_RTPCFG3,
			AW8622X_BIT_RTPCFG3_FIFO_AEH_MASK,
			(unsigned char)
				(((aw8622x->ram.base_addr >> 1) >> 4) & 0xF0));
	/* FIFO AEL */
	aw8622x_i2c_write(aw8622x, AW8622X_REG_RTPCFG4,
			(unsigned char)
				(((aw8622x->ram.base_addr >> 1) & 0x00FF)));
	/* FIFO_AFH */
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_RTPCFG3,
				AW8622X_BIT_RTPCFG3_FIFO_AFH_MASK,
				(unsigned char)(((aw8622x->ram.base_addr -
				(aw8622x->ram.base_addr >> 2)) >> 8) & 0x0F));
	/* FIFO_AFL */
	aw8622x_i2c_write(aw8622x, AW8622X_REG_RTPCFG5,
			(unsigned char)(((aw8622x->ram.base_addr -
				(aw8622x->ram.base_addr >> 2)) & 0x00FF)));
/*
*	unsigned int temp
*	HIGH<byte4 byte3 byte2 byte1>LOW
*	|_ _ _ _AF-12BIT_ _ _ _AE-12BIT|
*/
	aw8622x_i2c_read(aw8622x, AW8622X_REG_RTPCFG3, &reg_val);
	temp = ((reg_val & 0x0f) << 24) | ((reg_val & 0xf0) << 4);
	aw8622x_i2c_read(aw8622x, AW8622X_REG_RTPCFG4, &reg_val);
	temp = temp | reg_val;
	aw_dev_info(aw8622x->dev, "%s: almost_empty_threshold = %d\n", __func__,
		    (unsigned short)temp);
	aw8622x_i2c_read(aw8622x, AW8622X_REG_RTPCFG5, &reg_val);
	temp = temp | (reg_val << 16);
	aw_dev_info(aw8622x->dev, "%s: almost_full_threshold = %d\n", __func__,
		    temp >> 16);
	/* ram */
	shift = aw8622x->ram.baseaddr_shift;

	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_RAMADDRH,
			       AW8622X_BIT_RAMADDRH_MASK,
			       aw8622x_cont->data[0 + shift]);
	aw8622x_i2c_write(aw8622x, AW8622X_REG_RAMADDRL,
			  aw8622x_cont->data[1 + shift]);
	shift = aw8622x->ram.ram_shift;
	aw_dev_info(aw8622x->dev, "%s: ram_len = %d\n", __func__,
		    aw8622x_cont->len - shift);
	for (i = shift; i < aw8622x_cont->len; i++) {
		aw8622x->ram_update_flag = aw8622x_i2c_write(aw8622x,
							AW8622X_REG_RAMDATA,
							aw8622x_cont->data
							[i]);
	}
#ifdef	AW_CHECK_RAM_DATA
	shift = aw8622x->ram.baseaddr_shift;
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_RAMADDRH,
			       AW8622X_BIT_RAMADDRH_MASK,
			       aw8622x_cont->data[0 + shift]);
	aw8622x_i2c_write(aw8622x, AW8622X_REG_RAMADDRL,
			  aw8622x_cont->data[1 + shift]);
	shift = aw8622x->ram.ram_shift;
	for (i = shift; i < aw8622x_cont->len; i++) {
		aw8622x_i2c_read(aw8622x, AW8622X_REG_RAMDATA, &reg_val);
		/*
		*   aw_dev_info(aw8622x->dev,
		*	"%s aw8622x_cont->data=0x%02X, ramdata=0x%02X\n",
		*	__func__,aw8622x_cont->data[i],reg_val);
		*/
		if (reg_val != aw8622x_cont->data[i]) {
			aw_dev_err(aw8622x->dev,
				"%s: ram check error addr=0x%04x, file_data=0x%02X, ram_data=0x%02X\n",
				__func__, i, aw8622x_cont->data[i], reg_val);
			ret = -ERANGE;
			break;
		}
		check_sum += reg_val;
	}
	if (!ret) {
		aw8622x_i2c_read(aw8622x, AW8622X_REG_RTPCFG1, &reg_val);
		check_sum += reg_val & 0x0f;
		aw8622x_i2c_read(aw8622x, AW8622X_REG_RTPCFG2, &reg_val);
		check_sum += reg_val;

		if (check_sum != aw8622x->ram.check_sum) {
			aw_dev_err(aw8622x->dev, "%s: ram data check sum error, check_sum=0x%04x\n",
				__func__, check_sum);
			ret = -ERANGE;
		} else {
			aw_dev_info(aw8622x->dev, "%s: ram data check sum pass, check_sum=0x%04x\n",
				 __func__, check_sum);
		}
	}

#endif
	/* RAMINIT Disable */
	aw8622x_haptic_raminit(aw8622x, false);
	mutex_unlock(&aw8622x->lock);
	aw_dev_info(aw8622x->dev, "%s exit\n", __func__);
	return ret;
}

static int aw8622x_haptic_get_ram_number(struct aw8622x *aw8622x)
{
	unsigned char i = 0;
	unsigned char reg_val = 0;
	unsigned char ram_data[3];
	unsigned int first_wave_addr = 0;

	aw_dev_info(aw8622x->dev, "%s enter!\n", __func__);
	if (!aw8622x->ram_init) {
		aw_dev_err(aw8622x->dev,
			   "%s: ram init faild, ram_num = 0!\n",
			   __func__);
		return -EPERM;
	}

	mutex_lock(&aw8622x->lock);
	/* RAMINIT Enable */
	aw8622x_haptic_raminit(aw8622x, true);
	aw8622x_haptic_stop(aw8622x);
	aw8622x_i2c_write(aw8622x, AW8622X_REG_RAMADDRH,
			  (unsigned char)(aw8622x->ram.base_addr >> 8));
	aw8622x_i2c_write(aw8622x, AW8622X_REG_RAMADDRL,
			  (unsigned char)(aw8622x->ram.base_addr & 0x00ff));
	for (i = 0; i < 3; i++) {
		aw8622x_i2c_read(aw8622x, AW8622X_REG_RAMDATA, &reg_val);
		ram_data[i] = reg_val;
	}
	first_wave_addr = (ram_data[1] << 8 | ram_data[2]);
	aw8622x->ram.ram_num =
			(first_wave_addr - aw8622x->ram.base_addr - 1) / 4;
	aw_dev_info(aw8622x->dev,
		    "%s: ram_version = 0x%02x\n", __func__, ram_data[0]);
	aw_dev_info(aw8622x->dev,
		    "%s: first waveform addr = 0x%04x\n",
		    __func__, first_wave_addr);
	aw_dev_info(aw8622x->dev,
		    "%s: ram_num = %d\n", __func__, aw8622x->ram.ram_num);
	/* RAMINIT Disable */
	aw8622x_haptic_raminit(aw8622x, false);
	mutex_unlock(&aw8622x->lock);

	return 0;
}

static void aw8622x_ram_loaded(const struct firmware *cont, void *context)
{
	struct aw8622x *aw8622x = context;
	struct aw8622x_container *aw8622x_fw;
	unsigned short check_sum = 0;
	int i = 0;
	int ret = 0;
#ifdef AW_READ_BIN_FLEXBALLY
	static unsigned char load_cont;
	int ram_timer_val = 1000;

	load_cont++;
#endif
	aw_dev_info(aw8622x->dev, "%s enter\n", __func__);
	if (!cont) {
		aw_dev_err(aw8622x->dev, "%s: failed to read %s\n", __func__,
			   aw8622x_ram_name);
		release_firmware(cont);
#ifdef AW_READ_BIN_FLEXBALLY
		if (load_cont <= 20) {
			schedule_delayed_work(&aw8622x->ram_work,
					msecs_to_jiffies(ram_timer_val));
			aw_dev_info(aw8622x->dev, "%s:start hrtimer: load_cont=%d\n",
					__func__, load_cont);
		}
#endif
		return;
	}
	aw_dev_info(aw8622x->dev, "%s: loaded %s - size: %zu bytes\n", __func__,
		    aw8622x_ram_name, cont ? cont->size : 0);
/*
*	for(i=0; i < cont->size; i++) {
*		aw_dev_info(aw8622x->dev, "%s: addr: 0x%04x, data: 0x%02X\n",
*			__func__, i, *(cont->data+i));
*	}
*/
	/* check sum */
	for (i = 2; i < cont->size; i++)
		check_sum += cont->data[i];
	if (check_sum !=
	    (unsigned short)((cont->data[0] << 8) | (cont->data[1]))) {
		aw_dev_err(aw8622x->dev,
			"%s: check sum err: check_sum=0x%04x\n", __func__,
			check_sum);
		return;
	} else {
		aw_dev_info(aw8622x->dev, "%s: check sum pass: 0x%04x\n",
			    __func__, check_sum);
		aw8622x->ram.check_sum = check_sum;
	}

	/* aw8622x ram update less then 128kB */
	aw8622x_fw = kzalloc(cont->size + sizeof(int), GFP_KERNEL);
	if (!aw8622x_fw) {
		release_firmware(cont);
		aw_dev_err(aw8622x->dev, "%s: Error allocating memory\n",
			   __func__);
		return;
	}
	aw8622x_fw->len = cont->size;
	memcpy(aw8622x_fw->data, cont->data, cont->size);
	release_firmware(cont);
	ret = aw8622x_container_update(aw8622x, aw8622x_fw);
	if (ret) {
		kfree(aw8622x_fw);
		aw8622x->ram.len = 0;
		aw_dev_err(aw8622x->dev, "%s: ram firmware update failed!\n",
			__func__);
	} else {
		aw8622x->ram_init = 1;
		aw8622x->ram.len = aw8622x_fw->len;
		kfree(aw8622x_fw);
		aw_dev_info(aw8622x->dev, "%s: ram firmware update complete!\n",
		    __func__);
	}
	aw8622x_haptic_get_ram_number(aw8622x);

}

static int aw8622x_ram_update(struct aw8622x *aw8622x)
{
	aw8622x->ram_init = 0;
	aw8622x->rtp_init = 0;

	return request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
				       aw8622x_ram_name, aw8622x->dev,
				       GFP_KERNEL, aw8622x, aw8622x_ram_loaded);
}

static int aw8622x_rtp_trim_lra_calibration(struct aw8622x *aw8622x)
{
	unsigned char reg_val = 0;
	unsigned int fre_val = 0;
	unsigned int theory_time = 0;
	unsigned int lra_trim_code = 0;

	aw8622x_i2c_read(aw8622x, AW8622X_REG_SYSCTRL2, &reg_val);
	fre_val = (reg_val & 0x03) >> 0;

	if (fre_val == 2 || fre_val == 3)
		theory_time = (aw8622x->rtp_len / 12000) * 1000000;	/*12K */
	if (fre_val == 0)
		theory_time = (aw8622x->rtp_len / 24000) * 1000000;	/*24K */
	if (fre_val == 1)
		theory_time = (aw8622x->rtp_len / 48000) * 1000000;	/*48K */

	aw_dev_info(aw8622x->dev, "%s microsecond:%ld  theory_time = %d\n",
		    __func__, aw8622x->microsecond, theory_time);

	lra_trim_code = aw8622x_osc_trim_calculation(theory_time,
						     aw8622x->microsecond);
	if (lra_trim_code >= 0) {
		aw8622x->osc_cali_data = lra_trim_code;
		aw8622x_haptic_upload_lra(aw8622x, OSC_CALI);
	}
	return 0;
}

static enum hrtimer_restart aw8622x_vibrator_timer_func(struct hrtimer *timer)
{
	struct aw8622x *aw8622x = container_of(timer, struct aw8622x, timer);

	aw_dev_info(aw8622x->dev, "%s enter\n", __func__);
	aw8622x->state = 0;
	schedule_work(&aw8622x->long_vibrate_work);

	return HRTIMER_NORESTART;
}

static int aw8622x_haptic_play_repeat_seq(struct aw8622x *aw8622x,
					  unsigned char flag)
{
	//aw_dev_info(aw8622x->dev, "%s enter\n", __func__);

	if (flag) {
		aw8622x_haptic_play_mode(aw8622x, AW8622X_HAPTIC_RAM_LOOP_MODE);
		aw8622x_haptic_play_go(aw8622x, true);
	}
	return 0;
}

static int aw8622x_haptic_trig_param_init(struct aw8622x *aw8622x)
{
	aw_dev_info(aw8622x->dev, "%s enter\n", __func__);

	if ((aw8622x->name == AW86224_5) && (aw8622x->isUsedIntn))
		return 0;
	/* trig1 date */
	aw8622x->trig[0].trig_level = aw8622x->dts_info.trig_config[0];
	aw8622x->trig[0].trig_polar = aw8622x->dts_info.trig_config[1];
	aw8622x->trig[0].pos_enable = aw8622x->dts_info.trig_config[2];
	aw8622x->trig[0].pos_sequence = aw8622x->dts_info.trig_config[3];
	aw8622x->trig[0].neg_enable = aw8622x->dts_info.trig_config[4];
	aw8622x->trig[0].neg_sequence = aw8622x->dts_info.trig_config[5];
	aw8622x->trig[0].trig_brk = aw8622x->dts_info.trig_config[6];
	aw_dev_info(aw8622x->dev, "%s: trig1 date init ok!\n", __func__);
	if ((aw8622x->name == AW86224_5) && (!aw8622x->isUsedIntn)) {
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL2,
				AW8622X_BIT_SYSCTRL2_INTN_PIN_MASK,
				AW8622X_BIT_SYSCTRL2_TRIG1);
		return 0;
	}

	/* trig2 date */
	aw8622x->trig[1].trig_level = aw8622x->dts_info.trig_config[7 + 0];
	aw8622x->trig[1].trig_polar = aw8622x->dts_info.trig_config[7 + 1];
	aw8622x->trig[1].pos_enable = aw8622x->dts_info.trig_config[7 + 2];
	aw8622x->trig[1].pos_sequence = aw8622x->dts_info.trig_config[7 + 3];
	aw8622x->trig[1].neg_enable = aw8622x->dts_info.trig_config[7 + 4];
	aw8622x->trig[1].neg_sequence = aw8622x->dts_info.trig_config[7 + 5];
	aw8622x->trig[1].trig_brk = aw8622x->dts_info.trig_config[7 + 6];
	aw_dev_info(aw8622x->dev, "%s: trig2 date init ok!\n", __func__);

	/* trig3 date */
	aw8622x->trig[2].trig_level = aw8622x->dts_info.trig_config[14 + 0];
	aw8622x->trig[2].trig_polar = aw8622x->dts_info.trig_config[14 + 1];
	aw8622x->trig[2].pos_enable = aw8622x->dts_info.trig_config[14 + 2];
	aw8622x->trig[2].pos_sequence = aw8622x->dts_info.trig_config[14 + 3];
	aw8622x->trig[2].neg_enable = aw8622x->dts_info.trig_config[14 + 4];
	aw8622x->trig[2].neg_sequence = aw8622x->dts_info.trig_config[14 + 5];
	aw8622x->trig[2].trig_brk = aw8622x->dts_info.trig_config[14 + 6];
	aw_dev_info(aw8622x->dev, "%s: trig3 date init ok!\n", __func__);

	return 0;
}

static int aw8622x_haptic_trig_param_config(struct aw8622x *aw8622x)
{
	unsigned char trig1_polar_lev_brk = 0x00;
	unsigned char trig2_polar_lev_brk = 0x00;
	unsigned char trig3_polar_lev_brk = 0x00;
	unsigned char trig1_pos_seq = 0x00;
	unsigned char trig2_pos_seq = 0x00;
	unsigned char trig3_pos_seq = 0x00;
	unsigned char trig1_neg_seq = 0x00;
	unsigned char trig2_neg_seq = 0x00;
	unsigned char trig3_neg_seq = 0x00;

	aw_dev_info(aw8622x->dev, "%s enter\n", __func__);

	if ((aw8622x->name == AW86224_5) && (aw8622x->isUsedIntn))
		return 0;
	/* trig1 config */
	trig1_polar_lev_brk = aw8622x->trig[0].trig_polar << 2 |
				aw8622x->trig[0].trig_level << 1 |
				aw8622x->trig[0].trig_brk;
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_TRGCFG7,
				AW8622X_BIT_TRGCFG7_TRG1_POR_LEV_BRK_MASK,
				trig1_polar_lev_brk << 5);

	trig1_pos_seq = aw8622x->trig[0].pos_enable << 7 |
			aw8622x->trig[0].pos_sequence;
	aw8622x_i2c_write(aw8622x, AW8622X_REG_TRGCFG1, trig1_pos_seq);

	trig1_neg_seq = aw8622x->trig[0].neg_enable << 7 |
			aw8622x->trig[0].neg_sequence;
	aw8622x_i2c_write(aw8622x, AW8622X_REG_TRGCFG4, trig1_neg_seq);

	aw_dev_info(aw8622x->dev, "%s: trig1 date config ok!\n", __func__);

	if ((aw8622x->name == AW86224_5) && (!aw8622x->isUsedIntn)) {
		aw_dev_info(aw8622x->dev, "%s: intn pin is trig.\n", __func__);
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL2,
				AW8622X_BIT_SYSCTRL2_INTN_PIN_MASK,
				AW8622X_BIT_SYSCTRL2_TRIG1);
		return 0;
	}
	/* trig2 config */
	trig2_polar_lev_brk = aw8622x->trig[1].trig_polar << 2 |
				aw8622x->trig[1].trig_level << 1 |
				aw8622x->trig[1].trig_brk;
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_TRGCFG7,
				AW8622X_BIT_TRGCFG7_TRG2_POR_LEV_BRK_MASK,
				trig2_polar_lev_brk << 1);
	trig2_pos_seq = aw8622x->trig[1].pos_enable << 7 |
			aw8622x->trig[1].pos_sequence;
	aw8622x_i2c_write(aw8622x, AW8622X_REG_TRGCFG2, trig2_pos_seq);
	trig2_neg_seq = aw8622x->trig[1].neg_enable << 7 |
			aw8622x->trig[1].neg_sequence;
	aw8622x_i2c_write(aw8622x, AW8622X_REG_TRGCFG5, trig2_neg_seq);
	aw_dev_info(aw8622x->dev, "%s: trig2 date config ok!\n", __func__);

	/* trig3 config */
	trig3_polar_lev_brk = aw8622x->trig[2].trig_polar << 2 |
				aw8622x->trig[2].trig_level << 1 |
				aw8622x->trig[2].trig_brk;
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_TRGCFG8,
				AW8622X_BIT_TRGCFG8_TRG3_POR_LEV_BRK_MASK,
				trig3_polar_lev_brk << 5);
	trig3_pos_seq = aw8622x->trig[2].pos_enable << 7 |
			aw8622x->trig[2].pos_sequence;
	aw8622x_i2c_write(aw8622x, AW8622X_REG_TRGCFG3, trig3_pos_seq);
	trig3_neg_seq = aw8622x->trig[2].neg_enable << 7 |
			aw8622x->trig[2].neg_sequence;
	aw8622x_i2c_write(aw8622x, AW8622X_REG_TRGCFG6, trig3_neg_seq);
	aw_dev_info(aw8622x->dev, "%s: trig3 date config ok!\n", __func__);

	return 0;
}

/*****************************************************
 *
 * motor protect
 *
 *****************************************************/
static int aw8622x_haptic_swicth_motor_protect_config(struct aw8622x *aw8622x,
						      unsigned char addr,
						      unsigned char val)
{
	aw_dev_info(aw8622x->dev, "%s enter\n", __func__);
	if (addr == 1) {
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_DETCFG1,
				       AW8622X_BIT_DETCFG1_PRCT_MODE_MASK,
				       AW8622X_BIT_DETCFG1_PRCT_MODE_VALID);
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_PWMCFG1,
				       AW8622X_BIT_PWMCFG1_PRC_EN_MASK,
				       AW8622X_BIT_PWMCFG1_PRC_ENABLE);
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_PWMCFG3,
				       AW8622X_BIT_PWMCFG3_PR_EN_MASK,
				       AW8622X_BIT_PWMCFG3_PR_ENABLE);
	} else if (addr == 0) {
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_DETCFG1,
				       AW8622X_BIT_DETCFG1_PRCT_MODE_MASK,
				       AW8622X_BIT_DETCFG1_PRCT_MODE_INVALID);
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_PWMCFG1,
				       AW8622X_BIT_PWMCFG1_PRC_EN_MASK,
				       AW8622X_BIT_PWMCFG1_PRC_DISABLE);
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_PWMCFG3,
				       AW8622X_BIT_PWMCFG3_PR_EN_MASK,
				       AW8622X_BIT_PWMCFG3_PR_DISABLE);
	} else if (addr == 0x2d) {
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_PWMCFG1,
				       AW8622X_BIT_PWMCFG1_PRCTIME_MASK, val);
	} else if (addr == 0x3e) {
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_PWMCFG3,
				       AW8622X_BIT_PWMCFG3_PRLVL_MASK, val);
	} else if (addr == 0x3f) {
		aw8622x_i2c_write(aw8622x, AW8622X_REG_PWMCFG4, val);
	}
	return 0;
}

static int aw8622x_haptic_f0_calibration(struct aw8622x *aw8622x)
{
	int ret = 0;
	unsigned char reg_val = 0;
	unsigned int f0_limit = 0;
	char f0_cali_lra = 0;
	int f0_cali_step = 0;
	unsigned int f0_cali_min = aw8622x->dts_info.f0_ref *
				(100 - aw8622x->dts_info.f0_cali_percent) / 100;
	unsigned int f0_cali_max =  aw8622x->dts_info.f0_ref *
				(100 + aw8622x->dts_info.f0_cali_percent) / 100;

	aw_dev_info(aw8622x->dev, "%s enter\n", __func__);
	/*
	 * aw8622x_haptic_upload_lra(aw8622x, WRITE_ZERO);
	 */
	if (aw8622x_haptic_cont_get_f0(aw8622x)) {
		aw_dev_err(aw8622x->dev, "%s get f0 error, user defafult f0\n",
			   __func__);
	} else {
		/* max and min limit */
		f0_limit = aw8622x->f0;
		aw_dev_info(aw8622x->dev, "%s f0_ref = %d, f0_cali_min = %d, f0_cali_max = %d, f0 = %d\n",
			    __func__, aw8622x->dts_info.f0_ref,
			    f0_cali_min, f0_cali_max, aw8622x->f0);

		if ((aw8622x->f0 < f0_cali_min) || aw8622x->f0 > f0_cali_max) {
			aw_dev_err(aw8622x->dev, "%s f0 calibration out of range = %d!\n",
				   __func__, aw8622x->f0);
			f0_limit = aw8622x->dts_info.f0_ref;
			return -ERANGE;
		}
		aw_dev_info(aw8622x->dev, "%s f0_limit = %d\n", __func__,
			    (int)f0_limit);
		/* calculate cali step */
		f0_cali_step = 100000 * ((int)f0_limit -
					 (int)aw8622x->dts_info.f0_ref) /
		    ((int)f0_limit * 24);
		aw_dev_info(aw8622x->dev, "%s f0_cali_step = %d\n", __func__,
			    f0_cali_step);
		if (f0_cali_step >= 0) {	/*f0_cali_step >= 0 */
			if (f0_cali_step % 10 >= 5)
				f0_cali_step = 32 + (f0_cali_step / 10 + 1);
			else
				f0_cali_step = 32 + f0_cali_step / 10;
		} else {	/* f0_cali_step < 0 */
			if (f0_cali_step % 10 <= -5)
				f0_cali_step = 32 + (f0_cali_step / 10 - 1);
			else
				f0_cali_step = 32 + f0_cali_step / 10;
		}
		if (f0_cali_step > 31)
			f0_cali_lra = (char)f0_cali_step - 32;
		else
			f0_cali_lra = (char)f0_cali_step + 32;
		/* update cali step */
		aw8622x->f0_cali_data = (int)f0_cali_lra;
		aw8622x_haptic_upload_lra(aw8622x, F0_CALI);

		aw8622x_i2c_read(aw8622x, AW8622X_REG_TRIMCFG3, &reg_val);

		aw_dev_info(aw8622x->dev, "%s final trim_lra=0x%02x\n",
			__func__, reg_val);
	}
	/* restore standby work mode */
	aw8622x_haptic_stop(aw8622x);
	return ret;
}

/*****************************************************
 *
 * haptic cont
 *
 *****************************************************/
static int aw8622x_haptic_cont_config(struct aw8622x *aw8622x)
{
	aw_dev_info(aw8622x->dev, "%s enter\n", __func__);

	/* work mode */
	aw8622x_haptic_play_mode(aw8622x, AW8622X_HAPTIC_CONT_MODE);
	/* cont config */
	/* aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_CONTCFG1,
	 **                     AW8622X_BIT_CONTCFG1_EN_F0_DET_MASK,
	 **                     AW8622X_BIT_CONTCFG1_F0_DET_ENABLE);
	 */
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_CONTCFG6,
			       AW8622X_BIT_CONTCFG6_TRACK_EN_MASK,
			       AW8622X_BIT_CONTCFG6_TRACK_ENABLE);
	/* f0 driver level */
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_CONTCFG6,
			       AW8622X_BIT_CONTCFG6_DRV1_LVL_MASK,
			       aw8622x->cont_drv1_lvl);
	aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG7,
			  aw8622x->cont_drv2_lvl);
	/* DRV1_TIME */
	/* aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG8, 0xFF); */
	/* DRV2_TIME */
	aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG9, 0xFF);
	/* cont play go */
	aw8622x_haptic_play_go(aw8622x, true);
	return 0;
}

static int aw8622x_haptic_play_wav_seq(struct aw8622x *aw8622x,
				       unsigned char flag)
{
	aw_dev_info(aw8622x->dev, "%s enter\n", __func__);
	if (flag) {
		aw8622x_haptic_play_mode(aw8622x, AW8622X_HAPTIC_RAM_MODE);
		aw8622x_haptic_play_go(aw8622x, true);
	}
	return 0;
}

#ifdef TIMED_OUTPUT
static int aw8622x_vibrator_get_time(struct timed_output_dev *dev)
{
	struct aw8622x *aw8622x = container_of(dev, struct aw8622x, vib_dev);

	if (hrtimer_active(&aw8622x->timer)) {
		ktime_t r = hrtimer_get_remaining(&aw8622x->timer);

		return ktime_to_ms(r);
	}
	return 0;
}

static void aw8622x_vibrator_enable(struct timed_output_dev *dev, int value)
{
	struct aw8622x *aw8622x = container_of(dev, struct aw8622x, vib_dev);

	aw_dev_info(aw8622x->dev, "%s enter\n", __func__);
	if (!aw8622x->ram_init) {
		aw_dev_err(aw8622x->dev,
			   "%s: ram init failed, not allow to play!\n",
			   __func__);
		return;
	}
	mutex_lock(&aw8622x->lock);
	aw8622x_haptic_stop(aw8622x);
	if (value > 0) {
		aw8622x_haptic_ram_vbat_compensate(aw8622x, false);
		aw8622x_haptic_play_wav_seq(aw8622x, value);
	}
	mutex_unlock(&aw8622x->lock);
	aw_dev_info(aw8622x->dev, "%s exit\n", __func__);
}
#else
static enum led_brightness aw8622x_haptic_brightness_get(struct led_classdev
							 *cdev)
{
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);

	return aw8622x->amplitude;
}

static void aw8622x_haptic_brightness_set(struct led_classdev *cdev,
					  enum led_brightness level)
{
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);

	aw_dev_info(aw8622x->dev, "%s enter\n", __func__);
	if (!aw8622x->ram_init) {
		aw_dev_err(aw8622x->dev, "%s: ram init failed, not allow to play!\n",
		       __func__);
		return;
	}
	if (aw8622x->ram_update_flag < 0)
		return;
	aw8622x->amplitude = level;
	mutex_lock(&aw8622x->lock);
	aw8622x_haptic_stop(aw8622x);
	if (aw8622x->amplitude > 0) {
		aw8622x_haptic_upload_lra(aw8622x, F0_CALI);
		aw8622x_haptic_ram_vbat_compensate(aw8622x, false);
		aw8622x_haptic_play_wav_seq(aw8622x, aw8622x->amplitude);
	}
	mutex_unlock(&aw8622x->lock);
}
#endif

static int
aw8622x_haptic_audio_ctr_list_insert(struct haptic_audio *haptic_audio,
						struct haptic_ctr *haptic_ctr,
						struct device *dev)
{
	struct haptic_ctr *p_new = NULL;

	p_new = (struct haptic_ctr *)kzalloc(
		sizeof(struct haptic_ctr), GFP_KERNEL);
	if (p_new == NULL) {
		aw_dev_err(dev, "%s: kzalloc memory fail\n", __func__);
		return -ENOMEM;
	}
	/* update new list info */
	p_new->cnt = haptic_ctr->cnt;
	p_new->cmd = haptic_ctr->cmd;
	p_new->play = haptic_ctr->play;
	p_new->wavseq = haptic_ctr->wavseq;
	p_new->loop = haptic_ctr->loop;
	p_new->gain = haptic_ctr->gain;

	INIT_LIST_HEAD(&(p_new->list));
	list_add(&(p_new->list), &(haptic_audio->ctr_list));
	return 0;
}

static int
aw8622x_haptic_audio_ctr_list_clear(struct haptic_audio *haptic_audio)
{
	struct haptic_ctr *p_ctr = NULL;
	struct haptic_ctr *p_ctr_bak = NULL;

	list_for_each_entry_safe_reverse(p_ctr, p_ctr_bak,
					&(haptic_audio->ctr_list),
					list) {
		list_del(&p_ctr->list);
		kfree(p_ctr);
	}

	return 0;
}

static int aw8622x_haptic_audio_off(struct aw8622x *aw8622x)
{
	aw_dev_dbg(aw8622x->dev, "%s: enter\n", __func__);
	mutex_lock(&aw8622x->lock);
	aw8622x_haptic_set_gain(aw8622x, 0x80);
	aw8622x_haptic_stop(aw8622x);
	aw8622x->gun_type = 0xff;
	aw8622x->bullet_nr = 0;
	aw8622x_haptic_audio_ctr_list_clear(&aw8622x->haptic_audio);
	mutex_unlock(&aw8622x->lock);
	return 0;
}

static int aw8622x_haptic_audio_init(struct aw8622x *aw8622x)
{

	aw_dev_dbg(aw8622x->dev, "%s enter\n", __func__);
	aw8622x_haptic_set_wav_seq(aw8622x, 0x01, 0x00);

	return 0;
}

static int aw8622x_haptic_activate(struct aw8622x *aw8622x)
{
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL2,
			       AW8622X_BIT_SYSCTRL2_STANDBY_MASK,
			       AW8622X_BIT_SYSCTRL2_STANDBY_OFF);
	aw8622x_interrupt_clear(aw8622x);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSINTM,
			       AW8622X_BIT_SYSINTM_UVLM_MASK,
			       AW8622X_BIT_SYSINTM_UVLM_ON);
	return 0;
}

static int aw8622x_haptic_start(struct aw8622x *aw8622x)
{
	aw8622x_haptic_activate(aw8622x);
	aw8622x_haptic_play_go(aw8622x, true);
	return 0;
}


static void aw8622x_haptic_audio_work_routine(struct work_struct *work)
{
	struct aw8622x *aw8622x = container_of(work,
					struct aw8622x,
					haptic_audio.work);
	struct haptic_audio *haptic_audio = NULL;
	struct haptic_ctr *p_ctr = NULL;
	struct haptic_ctr *p_ctr_bak = NULL;
	unsigned int ctr_list_flag = 0;
	unsigned int ctr_list_input_cnt = 0;
	unsigned int ctr_list_output_cnt = 0;
	unsigned int ctr_list_diff_cnt = 0;
	unsigned int ctr_list_del_cnt = 0;
	int rtp_is_going_on = 0;

	aw_dev_dbg(aw8622x->dev, "%s enter\n", __func__);

	haptic_audio = &(aw8622x->haptic_audio);
	mutex_lock(&aw8622x->haptic_audio.lock);
	memset(&aw8622x->haptic_audio.ctr, 0,
	       sizeof(struct haptic_ctr));
	ctr_list_flag = 0;
	list_for_each_entry_safe_reverse(p_ctr, p_ctr_bak,
			&(haptic_audio->ctr_list), list) {
		ctr_list_flag = 1;
		break;
	}
	if (ctr_list_flag == 0)
		aw_dev_info(aw8622x->dev, "%s: ctr list empty\n", __func__);

	if (ctr_list_flag == 1) {
		list_for_each_entry_safe(p_ctr, p_ctr_bak,
					&(haptic_audio->ctr_list), list) {
			ctr_list_input_cnt =  p_ctr->cnt;
			break;
		}
		list_for_each_entry_safe_reverse(p_ctr, p_ctr_bak,
					&(haptic_audio->ctr_list), list) {
			ctr_list_output_cnt =  p_ctr->cnt;
			break;
		}
		if (ctr_list_input_cnt > ctr_list_output_cnt)
			ctr_list_diff_cnt = ctr_list_input_cnt - ctr_list_output_cnt;

		if (ctr_list_input_cnt < ctr_list_output_cnt)
			ctr_list_diff_cnt = 32 + ctr_list_input_cnt - ctr_list_output_cnt;

		if (ctr_list_diff_cnt > 2) {
			list_for_each_entry_safe_reverse(p_ctr, p_ctr_bak,
					&(haptic_audio->ctr_list), list) {
				if ((p_ctr->play == 0) &&
				(AW8622X_HAPTIC_CMD_ENABLE ==
					(AW8622X_HAPTIC_CMD_HAPTIC & p_ctr->cmd))) {
					list_del(&p_ctr->list);
					kfree(p_ctr);
					ctr_list_del_cnt++;
				}
				if (ctr_list_del_cnt == ctr_list_diff_cnt)
					break;
			}
		}
	}

	/* get the last data from list */
	list_for_each_entry_safe_reverse(p_ctr, p_ctr_bak,
				&(haptic_audio->ctr_list), list) {
		aw8622x->haptic_audio.ctr.cnt = p_ctr->cnt;
		aw8622x->haptic_audio.ctr.cmd = p_ctr->cmd;
		aw8622x->haptic_audio.ctr.play = p_ctr->play;
		aw8622x->haptic_audio.ctr.wavseq = p_ctr->wavseq;
		aw8622x->haptic_audio.ctr.loop = p_ctr->loop;
		aw8622x->haptic_audio.ctr.gain = p_ctr->gain;
		list_del(&p_ctr->list);
		kfree(p_ctr);
		break;
	}

	if (aw8622x->haptic_audio.ctr.play) {
		aw_dev_info(aw8622x->dev, "%s: cnt=%d, cmd=%d, play=%d, wavseq=%d, loop=%d, gain=%d\n",
			__func__,
			aw8622x->haptic_audio.ctr.cnt,
			aw8622x->haptic_audio.ctr.cmd,
			aw8622x->haptic_audio.ctr.play,
			aw8622x->haptic_audio.ctr.wavseq,
			aw8622x->haptic_audio.ctr.loop,
			aw8622x->haptic_audio.ctr.gain);
	}

	/* rtp mode jump */
	rtp_is_going_on = aw8622x_haptic_juge_RTP_is_going_on(aw8622x);
	if (rtp_is_going_on) {
		mutex_unlock(&aw8622x->haptic_audio.lock);
		return;
	}
	mutex_unlock(&aw8622x->haptic_audio.lock);

	/*haptic play control*/
	if (AW8622X_HAPTIC_CMD_ENABLE ==
	   (AW8622X_HAPTIC_CMD_HAPTIC & aw8622x->haptic_audio.ctr.cmd)) {
		if (aw8622x->haptic_audio.ctr.play ==
			AW8622X_HAPTIC_PLAY_ENABLE) {
			aw_dev_info(aw8622x->dev,
				"%s: haptic_audio_play_start\n", __func__);
			aw_dev_info(aw8622x->dev,
				"%s: normal haptic start\n", __func__);
			mutex_lock(&aw8622x->lock);
			aw8622x_haptic_stop(aw8622x);
			aw8622x_haptic_play_mode(aw8622x,
				AW8622X_HAPTIC_RAM_MODE);
			aw8622x_haptic_set_wav_seq(aw8622x, 0x00,
				aw8622x->haptic_audio.ctr.wavseq);
			aw8622x_haptic_set_wav_loop(aw8622x, 0x00,
				aw8622x->haptic_audio.ctr.loop);
			aw8622x_haptic_set_gain(aw8622x,
				aw8622x->haptic_audio.ctr.gain);
			aw8622x_haptic_start(aw8622x);
			mutex_unlock(&aw8622x->lock);
		} else if (AW8622X_HAPTIC_PLAY_STOP ==
			   aw8622x->haptic_audio.ctr.play) {
			mutex_lock(&aw8622x->lock);
			aw8622x_haptic_stop(aw8622x);
			mutex_unlock(&aw8622x->lock);
		} else if (AW8622X_HAPTIC_PLAY_GAIN ==
			   aw8622x->haptic_audio.ctr.play) {
			mutex_lock(&aw8622x->lock);
			aw8622x_haptic_set_gain(aw8622x,
					       aw8622x->haptic_audio.ctr.gain);
			mutex_unlock(&aw8622x->lock);
		}
	}


}

#if 0
static ssize_t aw8622x_cont_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	ssize_t len = 0;

	aw8622x_haptic_read_cont_f0(aw8622x);
	len += snprintf(buf + len, PAGE_SIZE - len,
			"%d\n", aw8622x->cont_f0);
	return len;
}

static ssize_t aw8622x_cont_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	aw8622x_haptic_stop(aw8622x);
	if (val)
		aw8622x_haptic_cont_config(aw8622x);
	return count;
}

static ssize_t aw8622x_f0_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	ssize_t len = 0;
	unsigned char reg = 0;

	mutex_lock(&aw8622x->lock);

	/* set d2s_gain to max to get better performance when cat f0 .*/
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL7,
				AW8622X_BIT_SYSCTRL7_D2S_GAIN_MASK,
				AW8622X_BIT_SYSCTRL7_D2S_GAIN_40);
	aw8622x_i2c_read(aw8622x, AW8622X_REG_TRIMCFG3, &reg);
	aw8622x_haptic_upload_lra(aw8622x, WRITE_ZERO);
	aw8622x_haptic_cont_get_f0(aw8622x);
	aw8622x_i2c_write(aw8622x, AW8622X_REG_TRIMCFG3, reg);
	/* set d2s_gain to default when cat f0 is finished.*/
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL7,
				AW8622X_BIT_SYSCTRL7_D2S_GAIN_MASK,
				aw8622x->dts_info.d2s_gain);
	mutex_unlock(&aw8622x->lock);
	len += snprintf(buf + len, PAGE_SIZE - len,
			"%d\n", aw8622x->f0);
	return len;
}

static ssize_t aw8622x_f0_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t aw8622x_reg_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	ssize_t len = 0;
	unsigned char i = 0;
	unsigned char reg_val = 0;

	for (i = 0; i < AW8622X_REG_MAX; i++) {
		if (!(aw8622x_reg_access[i] & REG_RD_ACCESS))
			continue;
		aw8622x_i2c_read(aw8622x, i, &reg_val);
		len += snprintf(buf + len, PAGE_SIZE - len,
				"reg:0x%02X=0x%02X\n", i, reg_val);
	}
	return len;
}

static ssize_t aw8622x_reg_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t count)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	unsigned int databuf[2] = { 0, 0 };

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
		aw8622x_i2c_write(aw8622x, (unsigned char)databuf[0],
				  (unsigned char)databuf[1]);
	}

	return count;
}

static ssize_t aw8622x_duration_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	ktime_t time_rem;
	s64 time_ms = 0;

	if (hrtimer_active(&aw8622x->timer)) {
		time_rem = hrtimer_get_remaining(&aw8622x->timer);
		time_ms = ktime_to_ms(time_rem);
	}
	return snprintf(buf, PAGE_SIZE, "%lld\n", time_ms);
}

static ssize_t aw8622x_duration_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;
	/* setting 0 on duration is NOP for now */
	if (val <= 0)
		return count;
	rc = aw8622x_haptic_ram_config(aw8622x, val);
	if (rc < 0)
		return rc;
	aw8622x->duration = val;
	return count;
}

static ssize_t aw8622x_activate_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);

	/* For now nothing to show */
	return snprintf(buf, PAGE_SIZE, "activate = %d\n", aw8622x->state);
}

static ssize_t aw8622x_activate_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	unsigned int val = 0;
	int rc = 0;

	if (!aw8622x->ram_init) {
		aw_dev_err(aw8622x->dev, "%s: ram init failed, not allow to play!\n",
		       __func__);
		return count;
	}
	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;
	if (val != 0 && val != 1)
		return count;
	aw_dev_info(aw8622x->dev, "%s: value=%d\n", __func__, val);
	mutex_lock(&aw8622x->lock);
	hrtimer_cancel(&aw8622x->timer);
	aw8622x->state = val;
	mutex_unlock(&aw8622x->lock);
	schedule_work(&aw8622x->long_vibrate_work);
	return count;
}

static ssize_t aw8622x_seq_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	size_t count = 0;
	unsigned char i = 0;
	unsigned char reg_val = 0;

	for (i = 0; i < AW8622X_SEQUENCER_SIZE; i++) {
		aw8622x_i2c_read(aw8622x, AW8622X_REG_WAVCFG1 + i, &reg_val);
		count += snprintf(buf + count, PAGE_SIZE - count,
				  "seq%d: 0x%02x\n", i + 1, reg_val);
		aw8622x->seq[i] |= reg_val;
	}
	return count;
}

static ssize_t aw8622x_seq_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t count)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	unsigned int databuf[2] = { 0, 0 };

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
		if (databuf[0] > AW8622X_SEQUENCER_SIZE ||
		    databuf[1] > aw8622x->ram.ram_num) {
			aw_dev_err(aw8622x->dev, "%s input value out of range\n",
				__func__);
			return count;
		}
		aw_dev_info(aw8622x->dev, "%s: seq%d=0x%02X\n", __func__,
			    databuf[0], databuf[1]);
		mutex_lock(&aw8622x->lock);
		aw8622x->seq[databuf[0]] = (unsigned char)databuf[1];
		aw8622x_haptic_set_wav_seq(aw8622x, (unsigned char)databuf[0],
					   aw8622x->seq[databuf[0]]);
		mutex_unlock(&aw8622x->lock);
	}
	return count;
}

static ssize_t aw8622x_loop_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	size_t count = 0;
	unsigned char i = 0;
	unsigned char reg_val = 0;

	for (i = 0; i < AW8622X_SEQUENCER_LOOP_SIZE; i++) {
		aw8622x_i2c_read(aw8622x, AW8622X_REG_WAVCFG9 + i, &reg_val);
		aw8622x->loop[i * 2 + 0] = (reg_val >> 4) & 0x0F;
		aw8622x->loop[i * 2 + 1] = (reg_val >> 0) & 0x0F;

		count += snprintf(buf + count, PAGE_SIZE - count,
				  "seq%d_loop = 0x%02x\n", i * 2 + 1,
				  aw8622x->loop[i * 2 + 0]);
		count += snprintf(buf + count, PAGE_SIZE - count,
				  "seq%d_loop = 0x%02x\n", i * 2 + 2,
				  aw8622x->loop[i * 2 + 1]);
	}
	return count;
}

static ssize_t aw8622x_loop_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	unsigned int databuf[2] = { 0, 0 };

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
		aw_dev_info(aw8622x->dev, "%s: seq%d loop=0x%02X\n", __func__,
			    databuf[0], databuf[1]);
		mutex_lock(&aw8622x->lock);
		aw8622x->loop[databuf[0]] = (unsigned char)databuf[1];
		aw8622x_haptic_set_wav_loop(aw8622x, (unsigned char)databuf[0],
					    aw8622x->loop[databuf[0]]);
		mutex_unlock(&aw8622x->lock);
	}
	return count;
}

static ssize_t aw8622x_rtp_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len,
			"rtp_cnt = %d\n",
			aw8622x->rtp_cnt);
	return len;
}

static ssize_t aw8622x_rtp_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t count)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev,
				struct aw8622x, vib_dev);
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0) {
		aw_dev_info(aw8622x->dev,
			"%s: kstrtouint fail\n", __func__);
		return rc;
	}
	mutex_lock(&aw8622x->lock);
	aw8622x_haptic_stop(aw8622x);
	aw8622x_haptic_set_rtp_aei(aw8622x, false);
	aw8622x_interrupt_clear(aw8622x);
	/* aw8622x_rtp_brake_set(aw8622x); */
	if (val < (sizeof(aw8622x_rtp_name) / AW8622X_RTP_NAME_MAX)) {
		aw8622x->rtp_file_num = val;
		if (val) {
			aw_dev_info(aw8622x->dev,
				"%s: aw8622x_rtp_name[%d]: %s\n", __func__,
				val, aw8622x_rtp_name[val]);

			schedule_work(&aw8622x->rtp_work);
		} else {
			aw_dev_err(aw8622x->dev,
				"%s: rtp_file_num 0x%02X over max value\n",
				__func__, aw8622x->rtp_file_num);
		}
	}
	mutex_unlock(&aw8622x->lock);
	return count;
}

static ssize_t aw8622x_state_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", aw8622x->state);
}

static ssize_t aw8622x_state_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	return count;
}

static ssize_t aw8622x_activate_mode_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);

	return snprintf(buf, PAGE_SIZE, "%d\n",
			aw8622x->activate_mode);
}

static ssize_t aw8622x_activate_mode_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;
	mutex_lock(&aw8622x->lock);
	aw8622x->activate_mode = val;
	mutex_unlock(&aw8622x->lock);
	return count;
}

static ssize_t aw8622x_index_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	unsigned char reg_val = 0;

	aw8622x_i2c_read(aw8622x, AW8622X_REG_WAVCFG1, &reg_val);
	aw8622x->index = reg_val;
	return snprintf(buf, PAGE_SIZE, "index = %d\n", aw8622x->index);
}

static ssize_t aw8622x_index_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;
	if (val > aw8622x->ram.ram_num) {
		aw_dev_err(aw8622x->dev,
			   "%s: input value out of range!\n", __func__);
		return count;
	}
	aw_dev_info(aw8622x->dev, "%s: value=%d\n", __func__, val);
	mutex_lock(&aw8622x->lock);
	aw8622x->index = val;
	aw8622x_haptic_set_repeat_wav_seq(aw8622x, aw8622x->index);
	mutex_unlock(&aw8622x->lock);
	return count;
}

static ssize_t aw8622x_sram_size_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	unsigned char reg_val = 0;

	aw8622x_i2c_read(aw8622x, AW8622X_REG_RTPCFG1, &reg_val);
	if ((reg_val & 0x30) == 0x20)
		return snprintf(buf, PAGE_SIZE, "sram_size = 2K\n");
	else if ((reg_val & 0x30) == 0x10)
		return snprintf(buf, PAGE_SIZE, "sram_size = 1K\n");
	else if ((reg_val & 0x30) == 0x30)
		return snprintf(buf, PAGE_SIZE, "sram_size = 3K\n");
	return snprintf(buf, PAGE_SIZE,
			"sram_size = 0x%02x error, plz check reg.\n",
			reg_val & 0x30);
}

static ssize_t aw8622x_sram_size_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;
	aw_dev_info(aw8622x->dev, "%s: value=%d\n", __func__, val);
	mutex_lock(&aw8622x->lock);
	if (val == AW8622X_HAPTIC_SRAM_2K)
		aw8622x_sram_size(aw8622x, AW8622X_HAPTIC_SRAM_2K);
	else if (val == AW8622X_HAPTIC_SRAM_1K)
		aw8622x_sram_size(aw8622x, AW8622X_HAPTIC_SRAM_1K);
	else if (val == AW8622X_HAPTIC_SRAM_3K)
		aw8622x_sram_size(aw8622x, AW8622X_HAPTIC_SRAM_3K);
	mutex_unlock(&aw8622x->lock);
	return count;
}

static ssize_t aw8622x_osc_cali_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "osc_cali_data = 0x%02X\n",
			aw8622x->osc_cali_data);

	return len;
}

static ssize_t aw8622x_osc_cali_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;
	mutex_lock(&aw8622x->lock);
	if (val == 1) {
		aw8622x_haptic_upload_lra(aw8622x, WRITE_ZERO);
		aw8622x_rtp_osc_calibration(aw8622x);
		aw8622x_rtp_trim_lra_calibration(aw8622x);
	} else if (val == 2) {
		aw8622x_haptic_upload_lra(aw8622x, OSC_CALI);
		aw8622x_rtp_osc_calibration(aw8622x);
	} else {
		aw_dev_err(aw8622x->dev, "%s input value out of range\n", __func__);
	}
	/* osc calibration flag end, other behaviors are permitted */
	mutex_unlock(&aw8622x->lock);

	return count;
}

static ssize_t aw8622x_gain_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	unsigned char reg = 0;
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);

	aw8622x_i2c_read(aw8622x, AW8622X_REG_PLAYCFG2, &reg);

	return snprintf(buf, PAGE_SIZE, "0x%02X\n", reg);
}

static ssize_t aw8622x_gain_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	aw_dev_info(aw8622x->dev, "%s: value=%d\n", __func__, val);
	if (val >= 0x80)
		val = 0x80;
	mutex_lock(&aw8622x->lock);
	aw8622x->gain = val;
	aw8622x_haptic_set_gain(aw8622x, aw8622x->gain);
	mutex_unlock(&aw8622x->lock);
	return count;
}

static ssize_t aw8622x_ram_update_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	ssize_t len = 0;
	unsigned int i = 0;
	unsigned char reg_val = 0;

	/* RAMINIT Enable */
	aw8622x_haptic_raminit(aw8622x, true);
	aw8622x_haptic_stop(aw8622x);
	aw8622x_i2c_write(aw8622x, AW8622X_REG_RAMADDRH,
			  (unsigned char)(aw8622x->ram.base_addr >> 8));
	aw8622x_i2c_write(aw8622x, AW8622X_REG_RAMADDRL,
			  (unsigned char)(aw8622x->ram.base_addr & 0x00ff));
	len += snprintf(buf + len, PAGE_SIZE - len,
			"haptic_ram len = %d\n", aw8622x->ram.len);
	for (i = 0; i < aw8622x->ram.len; i++) {
		aw8622x_i2c_read(aw8622x, AW8622X_REG_RAMDATA, &reg_val);
		if (i % 5 == 0)
			len += snprintf(buf + len,
				PAGE_SIZE - len, "0x%02X\n", reg_val);
		else
			len += snprintf(buf + len,
				PAGE_SIZE - len, "0x%02X,", reg_val);
	}
	len += snprintf(buf + len, PAGE_SIZE - len, "\n");
	/* RAMINIT Disable */
	aw8622x_haptic_raminit(aw8622x, false);
	return len;
}

static ssize_t aw8622x_ram_update_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;
	if (val)
		aw8622x_ram_update(aw8622x);
	return count;
}

static ssize_t aw8622x_f0_save_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "f0_cali_data = 0x%02X\n",
			aw8622x->f0_cali_data);

	return len;
}

static ssize_t aw8622x_f0_save_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;
	aw8622x->f0_cali_data = val;
	return count;
}

static ssize_t aw8622x_osc_save_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "osc_cali_data = 0x%02X\n",
			aw8622x->osc_cali_data);

	return len;
}

static ssize_t aw8622x_osc_save_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;
	aw8622x->osc_cali_data = val;
	return count;
}

static ssize_t aw8622x_trig_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	ssize_t len = 0;
	unsigned char i = 0;
	unsigned char trig_num = 3;

	if (aw8622x->name == AW86224_5)
		trig_num = 1;

	for (i = 0; i < trig_num; i++) {
		len += snprintf(buf + len, PAGE_SIZE - len,
				"trig%d: trig_level=%d, trig_polar=%d, pos_enable=%d, pos_sequence=%d, neg_enable=%d, neg_sequence=%d trig_brk=%d\n",
				i + 1,
				aw8622x->trig[i].trig_level,
				aw8622x->trig[i].trig_polar,
				aw8622x->trig[i].pos_enable,
				aw8622x->trig[i].pos_sequence,
				aw8622x->trig[i].neg_enable,
				aw8622x->trig[i].neg_sequence,
				aw8622x->trig[i].trig_brk);
	}

	return len;
}



static ssize_t aw8622x_trig_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	unsigned int databuf[9] = { 0 };

	if (sscanf(buf, "%d %d %d %d %d %d %d %d",
		&databuf[0], &databuf[1], &databuf[2],
		&databuf[3], &databuf[4], &databuf[5],
		&databuf[6], &databuf[7]) == 8) {
		aw_dev_info(aw8622x->dev,
			    "%s: %d, %d, %d, %d, %d, %d, %d, %d\n",
			    __func__, databuf[0], databuf[1], databuf[2],
			    databuf[3], databuf[4], databuf[5], databuf[6],
			    databuf[7]);
		if ((aw8622x->name == AW86224_5) && (databuf[0])) {
			aw_dev_err(aw8622x->dev,
				   "%s: input seq value out of range!\n",
				   __func__);
			return count;
		}
		if (databuf[0] < 0 || databuf[0] > 2) {
			aw_dev_err(aw8622x->dev,
				   "%s: input seq value out of range!\n",
				   __func__);
			return count;
		}
		if (!aw8622x->ram_init) {
			aw_dev_err(aw8622x->dev,
				   "%s: ram init failed, not allow to play!\n",
				   __func__);
			return count;
		}
		if (databuf[4] > aw8622x->ram.ram_num ||
		    databuf[6] > aw8622x->ram.ram_num) {
			aw_dev_err(aw8622x->dev,
				   "%s: input seq value out of range!\n",
				   __func__);
			return count;
		}
		aw8622x->trig[databuf[0]].trig_level = databuf[1];
		aw8622x->trig[databuf[0]].trig_polar = databuf[2];
		aw8622x->trig[databuf[0]].pos_enable = databuf[3];
		aw8622x->trig[databuf[0]].pos_sequence = databuf[4];
		aw8622x->trig[databuf[0]].neg_enable = databuf[5];
		aw8622x->trig[databuf[0]].neg_sequence = databuf[6];
		aw8622x->trig[databuf[0]].trig_brk = databuf[7];
		mutex_lock(&aw8622x->lock);
		aw8622x_haptic_trig_param_config(aw8622x);
		mutex_unlock(&aw8622x->lock);
	} else
		aw_dev_err(aw8622x->dev,
				   "%s: please input eight parameters\n",
				   __func__);
	return count;
}

static ssize_t aw8622x_ram_vbat_compensate_show(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	ssize_t len = 0;

	len +=
	    snprintf(buf + len, PAGE_SIZE - len, "ram_vbat_comp = %d\n",
		     aw8622x->ram_vbat_compensate);

	return len;
}

static ssize_t aw8622x_ram_vbat_compensate_store(struct device *dev,
						 struct device_attribute *attr,
						 const char *buf, size_t count)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	mutex_lock(&aw8622x->lock);
	if (val)
		aw8622x->ram_vbat_compensate =
		    AW8622X_HAPTIC_RAM_VBAT_COMP_ENABLE;
	else
		aw8622x->ram_vbat_compensate =
		    AW8622X_HAPTIC_RAM_VBAT_COMP_DISABLE;
	mutex_unlock(&aw8622x->lock);

	return count;
}

static ssize_t aw8622x_cali_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	ssize_t len = 0;
	unsigned char reg = 0;

	mutex_lock(&aw8622x->lock);
	aw8622x_i2c_read(aw8622x, AW8622X_REG_TRIMCFG3, &reg);
	aw8622x_haptic_upload_lra(aw8622x, F0_CALI);
	aw8622x_haptic_cont_get_f0(aw8622x);
	aw8622x_i2c_write(aw8622x, AW8622X_REG_TRIMCFG3, reg);
	mutex_unlock(&aw8622x->lock);
	len += snprintf(buf + len, PAGE_SIZE - len, "%d\n", aw8622x->f0);
	return len;
}

static ssize_t aw8622x_cali_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;
	if (val) {
		mutex_lock(&aw8622x->lock);
		aw8622x_haptic_upload_lra(aw8622x, WRITE_ZERO);
		aw8622x_haptic_f0_calibration(aw8622x);
		mutex_unlock(&aw8622x->lock);
	}
	return count;
}

static ssize_t aw8622x_cont_wait_num_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len,
			"cont_wait_num = 0x%02X\n", aw8622x->cont_wait_num);
	return len;
}

static ssize_t aw8622x_cont_wait_num_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	unsigned int databuf[1] = { 0 };

	if (sscanf(buf, "%x", &databuf[0]) == 1) {
		aw8622x->cont_wait_num = databuf[0];
		aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG4, databuf[0]);
	}
	return count;
}

static ssize_t aw8622x_cont_drv_lvl_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len,
			"cont_drv1_lvl = 0x%02X\n",
			aw8622x->cont_drv1_lvl);
	len += snprintf(buf + len, PAGE_SIZE - len,
			"cont_drv2_lvl = 0x%02X\n",
			aw8622x->cont_drv2_lvl);
	return len;
}

static ssize_t aw8622x_cont_drv_lvl_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	unsigned int databuf[2] = { 0, 0 };

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
		aw8622x->cont_drv1_lvl = databuf[0];
		aw8622x->cont_drv2_lvl = databuf[1];
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_CONTCFG6,
				       AW8622X_BIT_CONTCFG6_DRV1_LVL_MASK,
				       aw8622x->cont_drv1_lvl);
		aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG7,
				  aw8622x->cont_drv2_lvl);
	}
	return count;
}

static ssize_t aw8622x_cont_drv_time_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len,
			"cont_drv1_time = 0x%02X\n",
			aw8622x->cont_drv1_time);
	len += snprintf(buf + len, PAGE_SIZE - len,
			"cont_drv2_time = 0x%02X\n",
			aw8622x->cont_drv2_time);
	return len;
}

static ssize_t aw8622x_cont_drv_time_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	unsigned int databuf[2] = { 0, 0 };

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
		aw8622x->cont_drv1_time = databuf[0];
		aw8622x->cont_drv2_time = databuf[1];
		aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG8,
				  aw8622x->cont_drv1_time);
		aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG9,
				  aw8622x->cont_drv2_time);
	}
	return count;
}

static ssize_t aw8622x_cont_brk_time_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "cont_brk_time = 0x%02X\n",
			aw8622x->cont_brk_time);
	return len;
}

static ssize_t aw8622x_cont_brk_time_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	unsigned int databuf[1] = { 0 };

	if (sscanf(buf, "%x", &databuf[0]) == 1) {
		aw8622x->cont_brk_time = databuf[0];
		aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG10,
				  aw8622x->cont_brk_time);
	}
	return count;
}

static ssize_t aw8622x_vbat_monitor_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	ssize_t len = 0;

	mutex_lock(&aw8622x->lock);
	aw8622x_haptic_get_vbat(aw8622x);
	len += snprintf(buf + len, PAGE_SIZE - len, "vbat_monitor = %d\n",
			aw8622x->vbat);
	mutex_unlock(&aw8622x->lock);

	return len;
}

static ssize_t aw8622x_vbat_monitor_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	return count;
}

static ssize_t aw8622x_lra_resistance_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	ssize_t len = 0;

	aw8622x_haptic_get_lra_resistance(aw8622x);
	len += snprintf(buf + len, PAGE_SIZE - len, "lra_resistance = %d\n",
			aw8622x->lra);
	return len;
}

static ssize_t aw8622x_lra_resistance_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count)
{
	return count;
}

static ssize_t aw8622x_prctmode_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	ssize_t len = 0;
	unsigned char reg_val = 0;

	aw8622x_i2c_read(aw8622x, AW8622X_REG_DETCFG1, &reg_val);

	len += snprintf(buf + len, PAGE_SIZE - len, "prctmode = %d\n",
			reg_val & 0x08);
	return len;
}

static ssize_t aw8622x_prctmode_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	unsigned int databuf[2] = { 0, 0 };
	unsigned int addr = 0;
	unsigned int val = 0;

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
		addr = databuf[0];
		val = databuf[1];
		mutex_lock(&aw8622x->lock);
		aw8622x_haptic_swicth_motor_protect_config(aw8622x, addr, val);
		mutex_unlock(&aw8622x->lock);
	}
	return count;
}

static ssize_t aw8622x_gun_type_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);

	return snprintf(buf, PAGE_SIZE, "0x%02x\n", aw8622x->gun_type);

}

static ssize_t aw8622x_gun_type_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;
	aw_dev_info(aw8622x->dev, "%s: value=%d\n", __func__, val);
	mutex_lock(&aw8622x->lock);
	aw8622x->gun_type = val;
	mutex_unlock(&aw8622x->lock);
	return count;
}

static ssize_t aw8622x_bullet_nr_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);

	return snprintf(buf, PAGE_SIZE, "0x%02x\n", aw8622x->bullet_nr);
}

static ssize_t aw8622x_bullet_nr_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;
	aw_dev_info(aw8622x->dev, "%s: value=%d\n", __func__, val);
	mutex_lock(&aw8622x->lock);
	aw8622x->bullet_nr = val;
	mutex_unlock(&aw8622x->lock);
	return count;
}

static ssize_t aw8622x_haptic_audio_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	ssize_t len = 0;

	len += snprintf(buf+len, PAGE_SIZE-len,
			"%d\n", aw8622x->haptic_audio.ctr.cnt);
	return len;
}

static ssize_t aw8622x_haptic_audio_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	unsigned int databuf[6] = {0};
	int rtp_is_going_on = 0;
	struct haptic_ctr *hap_ctr = NULL;

	rtp_is_going_on = aw8622x_haptic_juge_RTP_is_going_on(aw8622x);
	if (rtp_is_going_on) {
		aw_dev_info(aw8622x->dev,
			"%s: RTP is runing, stop audio haptic\n", __func__);
		return count;
	}
	if (!aw8622x->ram_init)
		return count;

	if (sscanf(buf, "%d %d %d %d %d %d",
		&databuf[0], &databuf[1], &databuf[2],
		&databuf[3], &databuf[4], &databuf[5]) == 6) {
		if (databuf[2]) {
			aw_dev_info(aw8622x->dev, "%s: cnt=%d, cmd=%d, play=%d, wavseq=%d, loop=%d, gain=%d\n",
				__func__,
				databuf[0], databuf[1], databuf[2],
				databuf[3], databuf[4], databuf[5]);
			hap_ctr = (struct haptic_ctr *)kzalloc(sizeof(struct haptic_ctr),
								GFP_KERNEL);
			if (hap_ctr == NULL) {
				aw_dev_err(aw8622x->dev, "%s: kzalloc memory fail\n",
					   __func__);
				return count;
			}
			mutex_lock(&aw8622x->haptic_audio.lock);
			hap_ctr->cnt = (unsigned char)databuf[0];
			hap_ctr->cmd = (unsigned char)databuf[1];
			hap_ctr->play = (unsigned char)databuf[2];
			hap_ctr->wavseq = (unsigned char)databuf[3];
			hap_ctr->loop = (unsigned char)databuf[4];
			hap_ctr->gain = (unsigned char)databuf[5];
			aw8622x_haptic_audio_ctr_list_insert(&aw8622x->haptic_audio,
							hap_ctr, aw8622x->dev);
			if (hap_ctr->cmd == 0xff) {
				aw_dev_info(aw8622x->dev,
					"%s: haptic_audio stop\n", __func__);
				if (hrtimer_active(&aw8622x->haptic_audio.timer)) {
					aw_dev_info(aw8622x->dev, "%s: cancel haptic_audio_timer\n",
						__func__);
					hrtimer_cancel(&aw8622x->haptic_audio.timer);
					aw8622x->haptic_audio.ctr.cnt = 0;
					aw8622x_haptic_audio_off(aw8622x);
				}
			} else {
				if (hrtimer_active(&aw8622x->haptic_audio.timer)) {
				} else {
					aw_dev_info(aw8622x->dev, "%s: start haptic_audio_timer\n",
						__func__);
					aw8622x_haptic_audio_init(aw8622x);
					hrtimer_start(&aw8622x->haptic_audio.timer,
					ktime_set(aw8622x->haptic_audio.delay_val/1000000,
						(aw8622x->haptic_audio.delay_val%1000000)*1000),
					HRTIMER_MODE_REL);
				}
			}

		}
		mutex_unlock(&aw8622x->haptic_audio.lock);
		kfree(hap_ctr);
	}
	return count;
}

static ssize_t aw8622x_ram_num_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, vib_dev);
	ssize_t len = 0;

	aw8622x_haptic_get_ram_number(aw8622x);
	len += snprintf(buf+len, PAGE_SIZE-len,
			"ram_num = %d\n", aw8622x->ram.ram_num);
	return len;
}
#endif

static ssize_t aw8622x_cont_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct aw8622x *aw8622x = g_aw8622x;
	ssize_t len = 0;

	aw8622x_haptic_read_cont_f0(aw8622x);
	len += snprintf(buf + len, PAGE_SIZE - len,
			"%d\n", aw8622x->cont_f0);
	return len;
}

static ssize_t aw8622x_cont_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct aw8622x *aw8622x = g_aw8622x;
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	aw8622x_haptic_stop(aw8622x);
	if (val)
		aw8622x_haptic_cont_config(aw8622x);
	return count;
}

static ssize_t aw8622x_f0_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct aw8622x *aw8622x = g_aw8622x;
	ssize_t len = 0;
	unsigned char reg = 0;

	mutex_lock(&aw8622x->lock);

	/* set d2s_gain to max to get better performance when cat f0 .*/
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL7,
				AW8622X_BIT_SYSCTRL7_D2S_GAIN_MASK,
				AW8622X_BIT_SYSCTRL7_D2S_GAIN_40);
	aw8622x_i2c_read(aw8622x, AW8622X_REG_TRIMCFG3, &reg);
	aw8622x_haptic_upload_lra(aw8622x, WRITE_ZERO);
	aw8622x_haptic_cont_get_f0(aw8622x);
	aw8622x_i2c_write(aw8622x, AW8622X_REG_TRIMCFG3, reg);
	/* set d2s_gain to default when cat f0 is finished.*/
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL7,
				AW8622X_BIT_SYSCTRL7_D2S_GAIN_MASK,
				aw8622x->dts_info.d2s_gain);
	mutex_unlock(&aw8622x->lock);
	//len += snprintf(buf + len, PAGE_SIZE - len,
	//		"%d\n", aw8622x->f0);
	len += snprintf(buf + len, PAGE_SIZE - len,
			"%d\n", aw8622x->raw_f0);

	return len;
}

static ssize_t aw8622x_f0_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t aw8622x_reg_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct aw8622x *aw8622x = g_aw8622x;
	ssize_t len = 0;
	unsigned char i = 0;
	unsigned char reg_val = 0;

	for (i = 0; i < AW8622X_REG_MAX; i++) {
		if (!(aw8622x_reg_access[i] & REG_RD_ACCESS))
			continue;
		aw8622x_i2c_read(aw8622x, i, &reg_val);
		len += snprintf(buf + len, PAGE_SIZE - len,
				"reg:0x%02X=0x%02X\n", i, reg_val);
	}
	return len;
}

static ssize_t aw8622x_reg_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t count)
{
	struct aw8622x *aw8622x = g_aw8622x;
	unsigned int databuf[2] = { 0, 0 };

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
		aw8622x_i2c_write(aw8622x, (unsigned char)databuf[0],
				  (unsigned char)databuf[1]);
	}

	return count;
}

static ssize_t aw8622x_duration_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct aw8622x *aw8622x = g_aw8622x;
	ktime_t time_rem;
	s64 time_ms = 0;

	if (hrtimer_active(&aw8622x->timer)) {
		time_rem = hrtimer_get_remaining(&aw8622x->timer);
		time_ms = ktime_to_ms(time_rem);
	}
	return snprintf(buf, PAGE_SIZE, "%lld\n", time_ms);
}

static ssize_t aw8622x_duration_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct aw8622x *aw8622x = g_aw8622x;
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	aw_dev_info(aw8622x->dev, "%s: duration=%d\n", __func__, val);
	if (rc < 0)
		return rc;
	/* setting 0 on duration is NOP for now */
	if (val <= 0)
		return count;
	rc = aw8622x_haptic_ram_config(aw8622x, val);
	if (rc < 0)
		return rc;
	aw8622x->duration = val;
	return count;
}

static ssize_t aw8622x_activate_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct aw8622x *aw8622x = g_aw8622x;

	/* For now nothing to show */
	return snprintf(buf, PAGE_SIZE, "activate = %d\n", aw8622x->state);
}

static ssize_t aw8622x_activate_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct aw8622x *aw8622x = g_aw8622x;
	unsigned int val = 0;
	int rc = 0;

	if (!aw8622x->ram_init) {
		aw_dev_err(aw8622x->dev, "%s: ram init failed, not allow to play!\n",
		       __func__);
		return count;
	}
	rc = kstrtouint(buf, 0, &val);
	aw_dev_info(aw8622x->dev, "%s: value=%d\n", __func__, val);
	if (rc < 0)
		return rc;
	if (val != 0 && val != 1)
		return count;
	mutex_lock(&aw8622x->lock);
	hrtimer_cancel(&aw8622x->timer);
	aw8622x->state = val;
	mutex_unlock(&aw8622x->lock);
	schedule_work(&aw8622x->long_vibrate_work);
	return count;
}

static ssize_t aw8622x_seq_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct aw8622x *aw8622x = g_aw8622x;
	size_t count = 0;
	unsigned char i = 0;
	unsigned char reg_val = 0;

	for (i = 0; i < AW8622X_SEQUENCER_SIZE; i++) {
		aw8622x_i2c_read(aw8622x, AW8622X_REG_WAVCFG1 + i, &reg_val);
		count += snprintf(buf + count, PAGE_SIZE - count,
				  "seq%d: 0x%02x\n", i + 1, reg_val);
		aw8622x->seq[i] |= reg_val;
	}
	return count;
}

static ssize_t aw8622x_seq_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t count)
{
	struct aw8622x *aw8622x = g_aw8622x;
	unsigned int databuf[2] = { 0, 0 };

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
		if (databuf[0] > AW8622X_SEQUENCER_SIZE ||
		    databuf[1] > aw8622x->ram.ram_num) {
			aw_dev_err(aw8622x->dev, "%s input value out of range\n",
				__func__);
			return count;
		}
		aw_dev_info(aw8622x->dev, "%s: seq%d=0x%02X\n", __func__,
			    databuf[0], databuf[1]);
		mutex_lock(&aw8622x->lock);
		aw8622x->seq[databuf[0]] = (unsigned char)databuf[1];
		aw8622x_haptic_set_wav_seq(aw8622x, (unsigned char)databuf[0],
					   aw8622x->seq[databuf[0]]);
		mutex_unlock(&aw8622x->lock);
	}
	return count;
}

static ssize_t aw8622x_loop_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct aw8622x *aw8622x = g_aw8622x;
	size_t count = 0;
	unsigned char i = 0;
	unsigned char reg_val = 0;

	for (i = 0; i < AW8622X_SEQUENCER_LOOP_SIZE; i++) {
		aw8622x_i2c_read(aw8622x, AW8622X_REG_WAVCFG9 + i, &reg_val);
		aw8622x->loop[i * 2 + 0] = (reg_val >> 4) & 0x0F;
		aw8622x->loop[i * 2 + 1] = (reg_val >> 0) & 0x0F;

		count += snprintf(buf + count, PAGE_SIZE - count,
				  "seq%d_loop = 0x%02x\n", i * 2 + 1,
				  aw8622x->loop[i * 2 + 0]);
		count += snprintf(buf + count, PAGE_SIZE - count,
				  "seq%d_loop = 0x%02x\n", i * 2 + 2,
				  aw8622x->loop[i * 2 + 1]);
	}
	return count;
}

static ssize_t aw8622x_loop_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct aw8622x *aw8622x = g_aw8622x;
	unsigned int databuf[2] = { 0, 0 };

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
		aw_dev_info(aw8622x->dev, "%s: seq%d loop=0x%02X\n", __func__,
			    databuf[0], databuf[1]);
		mutex_lock(&aw8622x->lock);
		aw8622x->loop[databuf[0]] = (unsigned char)databuf[1];
		aw8622x_haptic_set_wav_loop(aw8622x, (unsigned char)databuf[0],
					    aw8622x->loop[databuf[0]]);
		mutex_unlock(&aw8622x->lock);
	}
	return count;
}

static ssize_t aw8622x_rtp_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct aw8622x *aw8622x = g_aw8622x;
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len,
			"rtp_cnt = %d\n",
			aw8622x->rtp_cnt);
	return len;
}

static ssize_t aw8622x_rtp_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t count)
{
	struct aw8622x *aw8622x = g_aw8622x;
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0) {
		aw_dev_info(aw8622x->dev,
			"%s: kstrtouint fail\n", __func__);
		return rc;
	}
	mutex_lock(&aw8622x->lock);
	aw8622x_haptic_stop(aw8622x);
	aw8622x_haptic_set_rtp_aei(aw8622x, false);
	aw8622x_interrupt_clear(aw8622x);
	/* aw8622x_rtp_brake_set(aw8622x); */
	if (val < (sizeof(aw8622x_rtp_name) / AW8622X_RTP_NAME_MAX)) {
		aw8622x->rtp_file_num = val;
		if (val) {
			aw_dev_info(aw8622x->dev,
				"%s: aw8622x_rtp_name[%d]: %s\n", __func__,
				val, aw8622x_rtp_name[val]);

			schedule_work(&aw8622x->rtp_work);
		} else {
			aw_dev_err(aw8622x->dev,
				"%s: rtp_file_num 0x%02X over max value\n",
				__func__, aw8622x->rtp_file_num);
		}
	}
	mutex_unlock(&aw8622x->lock);
	return count;
}

static ssize_t aw8622x_state_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct aw8622x *aw8622x = g_aw8622x;

	return snprintf(buf, PAGE_SIZE, "%d\n", aw8622x->state);
}

static ssize_t aw8622x_state_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	return count;
}

static ssize_t aw8622x_activate_mode_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct aw8622x *aw8622x = g_aw8622x;

	return snprintf(buf, PAGE_SIZE, "%d\n",
			aw8622x->activate_mode);
}

static ssize_t aw8622x_activate_mode_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct aw8622x *aw8622x = g_aw8622x;
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;
	mutex_lock(&aw8622x->lock);
	aw8622x->activate_mode = val;
	mutex_unlock(&aw8622x->lock);
	return count;
}

static ssize_t aw8622x_index_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct aw8622x *aw8622x = g_aw8622x;
	unsigned char reg_val = 0;

	aw8622x_i2c_read(aw8622x, AW8622X_REG_WAVCFG1, &reg_val);
	aw8622x->index = reg_val;
	return snprintf(buf, PAGE_SIZE, "index = %d\n", aw8622x->index);
}

static ssize_t aw8622x_index_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct aw8622x *aw8622x = g_aw8622x;
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;
	if (val > aw8622x->ram.ram_num) {
		aw_dev_err(aw8622x->dev,
			   "%s: input value out of range!\n", __func__);
		return count;
	}
	aw_dev_info(aw8622x->dev, "%s: value=%d\n", __func__, val);
	mutex_lock(&aw8622x->lock);
	aw8622x->index = val;
	aw8622x_haptic_set_repeat_wav_seq(aw8622x, aw8622x->index);
	mutex_unlock(&aw8622x->lock);
	return count;
}

static ssize_t aw8622x_sram_size_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct aw8622x *aw8622x = g_aw8622x;
	unsigned char reg_val = 0;

	aw8622x_i2c_read(aw8622x, AW8622X_REG_RTPCFG1, &reg_val);
	if ((reg_val & 0x30) == 0x20)
		return snprintf(buf, PAGE_SIZE, "sram_size = 2K\n");
	else if ((reg_val & 0x30) == 0x10)
		return snprintf(buf, PAGE_SIZE, "sram_size = 1K\n");
	else if ((reg_val & 0x30) == 0x30)
		return snprintf(buf, PAGE_SIZE, "sram_size = 3K\n");
	return snprintf(buf, PAGE_SIZE,
			"sram_size = 0x%02x error, plz check reg.\n",
			reg_val & 0x30);
}

static ssize_t aw8622x_sram_size_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct aw8622x *aw8622x = g_aw8622x;
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;
	aw_dev_info(aw8622x->dev, "%s: value=%d\n", __func__, val);
	mutex_lock(&aw8622x->lock);
	if (val == AW8622X_HAPTIC_SRAM_2K)
		aw8622x_sram_size(aw8622x, AW8622X_HAPTIC_SRAM_2K);
	else if (val == AW8622X_HAPTIC_SRAM_1K)
		aw8622x_sram_size(aw8622x, AW8622X_HAPTIC_SRAM_1K);
	else if (val == AW8622X_HAPTIC_SRAM_3K)
		aw8622x_sram_size(aw8622x, AW8622X_HAPTIC_SRAM_3K);
	mutex_unlock(&aw8622x->lock);
	return count;
}

static ssize_t aw8622x_osc_cali_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct aw8622x *aw8622x = g_aw8622x;
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "osc_cali_data = 0x%02X\n",
			aw8622x->osc_cali_data);

	return len;
}

static ssize_t aw8622x_osc_cali_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct aw8622x *aw8622x = g_aw8622x;
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;
	mutex_lock(&aw8622x->lock);
	if (val == 1) {
		aw8622x_haptic_upload_lra(aw8622x, WRITE_ZERO);
		aw8622x_rtp_osc_calibration(aw8622x);
		aw8622x_rtp_trim_lra_calibration(aw8622x);
	} else if (val == 2) {
		aw8622x_haptic_upload_lra(aw8622x, OSC_CALI);
		aw8622x_rtp_osc_calibration(aw8622x);
	} else {
		aw_dev_err(aw8622x->dev, "%s input value out of range\n", __func__);
	}
	/* osc calibration flag end, other behaviors are permitted */
	mutex_unlock(&aw8622x->lock);

	return count;
}

static ssize_t aw8622x_gain_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
#if 0
	unsigned char reg = 0;
	struct aw8622x *aw8622x = g_aw8622x;

	aw8622x_i2c_read(aw8622x, AW8622X_REG_PLAYCFG2, &reg);

	return snprintf(buf, PAGE_SIZE, "0x%02X\n", reg);
#endif
	struct aw8622x *aw8622x = g_aw8622x;

	return snprintf(buf, PAGE_SIZE, "0x%02X\n", aw8622x->gain);
}

static ssize_t aw8622x_gain_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct aw8622x *aw8622x = g_aw8622x;
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	aw_dev_info(aw8622x->dev, "%s: value=%d\n", __func__, val);
	if (val >= 0x80)
		val = 0x80;
	mutex_lock(&aw8622x->lock);
	aw8622x->gain = val;
	aw8622x_haptic_set_gain(aw8622x, aw8622x->gain);
	mutex_unlock(&aw8622x->lock);
	return count;
}

static ssize_t aw8622x_ram_update_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct aw8622x *aw8622x = g_aw8622x;
	ssize_t len = 0;
	unsigned int i = 0;
	unsigned char reg_val = 0;

	/* RAMINIT Enable */
	aw8622x_haptic_raminit(aw8622x, true);
	aw8622x_haptic_stop(aw8622x);
	aw8622x_i2c_write(aw8622x, AW8622X_REG_RAMADDRH,
			  (unsigned char)(aw8622x->ram.base_addr >> 8));
	aw8622x_i2c_write(aw8622x, AW8622X_REG_RAMADDRL,
			  (unsigned char)(aw8622x->ram.base_addr & 0x00ff));
	len += snprintf(buf + len, PAGE_SIZE - len,
			"haptic_ram len = %d\n", aw8622x->ram.len);
	for (i = 0; i < aw8622x->ram.len; i++) {
		aw8622x_i2c_read(aw8622x, AW8622X_REG_RAMDATA, &reg_val);
		if (i % 5 == 0)
			len += snprintf(buf + len,
				PAGE_SIZE - len, "0x%02X\n", reg_val);
		else
			len += snprintf(buf + len,
				PAGE_SIZE - len, "0x%02X,", reg_val);
	}
	len += snprintf(buf + len, PAGE_SIZE - len, "\n");
	/* RAMINIT Disable */
	aw8622x_haptic_raminit(aw8622x, false);
	return len;
}

static ssize_t aw8622x_ram_update_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct aw8622x *aw8622x = g_aw8622x;
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;
	if (val)
		aw8622x_ram_update(aw8622x);
	return count;
}

static ssize_t aw8622x_f0_save_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct aw8622x *aw8622x = g_aw8622x;
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "f0_cali_data = 0x%02X\n",
			aw8622x->f0_cali_data);

	return len;
}

static ssize_t aw8622x_f0_save_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct aw8622x *aw8622x = g_aw8622x;
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;
	aw8622x->f0_cali_data = val;
	return count;
}

static ssize_t aw8622x_osc_save_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct aw8622x *aw8622x = g_aw8622x;
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "osc_cali_data = 0x%02X\n",
			aw8622x->osc_cali_data);

	return len;
}

static ssize_t aw8622x_osc_save_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct aw8622x *aw8622x = g_aw8622x;
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;
	aw8622x->osc_cali_data = val;
	return count;
}

static ssize_t aw8622x_trig_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct aw8622x *aw8622x = g_aw8622x;
	ssize_t len = 0;
	unsigned char i = 0;
	unsigned char trig_num = 3;

	if (aw8622x->name == AW86224_5)
		trig_num = 1;

	for (i = 0; i < trig_num; i++) {
		len += snprintf(buf + len, PAGE_SIZE - len,
				"trig%d: trig_level=%d, trig_polar=%d, pos_enable=%d, pos_sequence=%d, neg_enable=%d, neg_sequence=%d trig_brk=%d\n",
				i + 1,
				aw8622x->trig[i].trig_level,
				aw8622x->trig[i].trig_polar,
				aw8622x->trig[i].pos_enable,
				aw8622x->trig[i].pos_sequence,
				aw8622x->trig[i].neg_enable,
				aw8622x->trig[i].neg_sequence,
				aw8622x->trig[i].trig_brk);
	}

	return len;
}



static ssize_t aw8622x_trig_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct aw8622x *aw8622x = g_aw8622x;
	unsigned int databuf[9] = { 0 };

	if (sscanf(buf, "%d %d %d %d %d %d %d %d",
		&databuf[0], &databuf[1], &databuf[2],
		&databuf[3], &databuf[4], &databuf[5],
		&databuf[6], &databuf[7]) == 8) {
		aw_dev_info(aw8622x->dev,
			    "%s: %d, %d, %d, %d, %d, %d, %d, %d\n",
			    __func__, databuf[0], databuf[1], databuf[2],
			    databuf[3], databuf[4], databuf[5], databuf[6],
			    databuf[7]);
		if ((aw8622x->name == AW86224_5) && (databuf[0])) {
			aw_dev_err(aw8622x->dev,
				   "%s: input seq value out of range!\n",
				   __func__);
			return count;
		}
		if (databuf[0] < 0 || databuf[0] > 2) {
			aw_dev_err(aw8622x->dev,
				   "%s: input seq value out of range!\n",
				   __func__);
			return count;
		}
		if (!aw8622x->ram_init) {
			aw_dev_err(aw8622x->dev,
				   "%s: ram init failed, not allow to play!\n",
				   __func__);
			return count;
		}
		if (databuf[4] > aw8622x->ram.ram_num ||
		    databuf[6] > aw8622x->ram.ram_num) {
			aw_dev_err(aw8622x->dev,
				   "%s: input seq value out of range!\n",
				   __func__);
			return count;
		}
		aw8622x->trig[databuf[0]].trig_level = databuf[1];
		aw8622x->trig[databuf[0]].trig_polar = databuf[2];
		aw8622x->trig[databuf[0]].pos_enable = databuf[3];
		aw8622x->trig[databuf[0]].pos_sequence = databuf[4];
		aw8622x->trig[databuf[0]].neg_enable = databuf[5];
		aw8622x->trig[databuf[0]].neg_sequence = databuf[6];
		aw8622x->trig[databuf[0]].trig_brk = databuf[7];
		mutex_lock(&aw8622x->lock);
		aw8622x_haptic_trig_param_config(aw8622x);
		mutex_unlock(&aw8622x->lock);
	} else
		aw_dev_err(aw8622x->dev,
				   "%s: please input eight parameters\n",
				   __func__);
	return count;
}

static ssize_t aw8622x_ram_vbat_compensate_show(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct aw8622x *aw8622x = g_aw8622x;
	ssize_t len = 0;

	len +=
	    snprintf(buf + len, PAGE_SIZE - len, "ram_vbat_comp = %d\n",
		     aw8622x->ram_vbat_compensate);

	return len;
}

static ssize_t aw8622x_ram_vbat_compensate_store(struct device *dev,
						 struct device_attribute *attr,
						 const char *buf, size_t count)
{
	struct aw8622x *aw8622x = g_aw8622x;
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	mutex_lock(&aw8622x->lock);
	if (val)
		aw8622x->ram_vbat_compensate =
		    AW8622X_HAPTIC_RAM_VBAT_COMP_ENABLE;
	else
		aw8622x->ram_vbat_compensate =
		    AW8622X_HAPTIC_RAM_VBAT_COMP_DISABLE;
	mutex_unlock(&aw8622x->lock);

	return count;
}

static ssize_t aw8622x_cali_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct aw8622x *aw8622x = g_aw8622x;
	ssize_t len = 0;
	unsigned char reg = 0;

	mutex_lock(&aw8622x->lock);
	aw8622x_i2c_read(aw8622x, AW8622X_REG_TRIMCFG3, &reg);
	aw8622x_haptic_upload_lra(aw8622x, F0_CALI);
	aw8622x_haptic_cont_get_f0(aw8622x);
	aw8622x_i2c_write(aw8622x, AW8622X_REG_TRIMCFG3, reg);
	mutex_unlock(&aw8622x->lock);
	len += snprintf(buf + len, PAGE_SIZE - len, "%d\n", aw8622x->f0);
	return len;
}

static ssize_t aw8622x_cali_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct aw8622x *aw8622x = g_aw8622x;
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;
	if (val) {
		mutex_lock(&aw8622x->lock);
		aw8622x_haptic_upload_lra(aw8622x, WRITE_ZERO);
		aw8622x_haptic_f0_calibration(aw8622x);
		mutex_unlock(&aw8622x->lock);
	}
	return count;
}

static ssize_t aw8622x_cont_wait_num_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct aw8622x *aw8622x = g_aw8622x;
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len,
			"cont_wait_num = 0x%02X\n", aw8622x->cont_wait_num);
	return len;
}

static ssize_t aw8622x_cont_wait_num_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct aw8622x *aw8622x = g_aw8622x;
	unsigned int databuf[1] = { 0 };

	if (sscanf(buf, "%x", &databuf[0]) == 1) {
		aw8622x->cont_wait_num = databuf[0];
		aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG4, databuf[0]);
	}
	return count;
}

static ssize_t aw8622x_cont_drv_lvl_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct aw8622x *aw8622x = g_aw8622x;
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len,
			"cont_drv1_lvl = 0x%02X\n",
			aw8622x->cont_drv1_lvl);
	len += snprintf(buf + len, PAGE_SIZE - len,
			"cont_drv2_lvl = 0x%02X\n",
			aw8622x->cont_drv2_lvl);
	return len;
}

static ssize_t aw8622x_cont_drv_lvl_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct aw8622x *aw8622x = g_aw8622x;
	unsigned int databuf[2] = { 0, 0 };

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
		aw8622x->cont_drv1_lvl = databuf[0];
		aw8622x->cont_drv2_lvl = databuf[1];
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_CONTCFG6,
				       AW8622X_BIT_CONTCFG6_DRV1_LVL_MASK,
				       aw8622x->cont_drv1_lvl);
		aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG7,
				  aw8622x->cont_drv2_lvl);
	}
	return count;
}

static ssize_t aw8622x_cont_drv_time_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct aw8622x *aw8622x = g_aw8622x;
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len,
			"cont_drv1_time = 0x%02X\n",
			aw8622x->cont_drv1_time);
	len += snprintf(buf + len, PAGE_SIZE - len,
			"cont_drv2_time = 0x%02X\n",
			aw8622x->cont_drv2_time);
	return len;
}

static ssize_t aw8622x_cont_drv_time_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct aw8622x *aw8622x = g_aw8622x;
	unsigned int databuf[2] = { 0, 0 };

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
		aw8622x->cont_drv1_time = databuf[0];
		aw8622x->cont_drv2_time = databuf[1];
		aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG8,
				  aw8622x->cont_drv1_time);
		aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG9,
				  aw8622x->cont_drv2_time);
	}
	return count;
}

static ssize_t aw8622x_cont_brk_time_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct aw8622x *aw8622x = g_aw8622x;
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "cont_brk_time = 0x%02X\n",
			aw8622x->cont_brk_time);
	return len;
}

static ssize_t aw8622x_cont_brk_time_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct aw8622x *aw8622x = g_aw8622x;
	unsigned int databuf[1] = { 0 };

	if (sscanf(buf, "%x", &databuf[0]) == 1) {
		aw8622x->cont_brk_time = databuf[0];
		aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG10,
				  aw8622x->cont_brk_time);
	}
	return count;
}

static ssize_t aw8622x_vbat_monitor_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct aw8622x *aw8622x = g_aw8622x;
	ssize_t len = 0;

	mutex_lock(&aw8622x->lock);
	aw8622x_haptic_get_vbat(aw8622x);
	len += snprintf(buf + len, PAGE_SIZE - len, "vbat_monitor = %d\n",
			aw8622x->vbat);
	mutex_unlock(&aw8622x->lock);

	return len;
}

static ssize_t aw8622x_vbat_monitor_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	return count;
}

static ssize_t aw8622x_lra_resistance_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct aw8622x *aw8622x = g_aw8622x;
	ssize_t len = 0;

	aw8622x_haptic_get_lra_resistance(aw8622x);
	len += snprintf(buf + len, PAGE_SIZE - len, "lra_resistance = %d\n",
			aw8622x->lra);
	return len;
}

static ssize_t aw8622x_lra_resistance_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count)
{
	return count;
}

static ssize_t aw8622x_prctmode_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct aw8622x *aw8622x = g_aw8622x;
	ssize_t len = 0;
	unsigned char reg_val = 0;

	aw8622x_i2c_read(aw8622x, AW8622X_REG_DETCFG1, &reg_val);

	len += snprintf(buf + len, PAGE_SIZE - len, "prctmode = %d\n",
			reg_val & 0x08);
	return len;
}

static ssize_t aw8622x_prctmode_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct aw8622x *aw8622x = g_aw8622x;
	unsigned int databuf[2] = { 0, 0 };
	unsigned int addr = 0;
	unsigned int val = 0;

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
		addr = databuf[0];
		val = databuf[1];
		mutex_lock(&aw8622x->lock);
		aw8622x_haptic_swicth_motor_protect_config(aw8622x, addr, val);
		mutex_unlock(&aw8622x->lock);
	}
	return count;
}

static ssize_t aw8622x_gun_type_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct aw8622x *aw8622x = g_aw8622x;

	return snprintf(buf, PAGE_SIZE, "0x%02x\n", aw8622x->gun_type);

}

static ssize_t aw8622x_gun_type_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct aw8622x *aw8622x = g_aw8622x;
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;
	aw_dev_info(aw8622x->dev, "%s: value=%d\n", __func__, val);
	mutex_lock(&aw8622x->lock);
	aw8622x->gun_type = val;
	mutex_unlock(&aw8622x->lock);
	return count;
}

static ssize_t aw8622x_bullet_nr_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct aw8622x *aw8622x = g_aw8622x;

	return snprintf(buf, PAGE_SIZE, "0x%02x\n", aw8622x->bullet_nr);
}

static ssize_t aw8622x_bullet_nr_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct aw8622x *aw8622x = g_aw8622x;
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;
	aw_dev_info(aw8622x->dev, "%s: value=%d\n", __func__, val);
	mutex_lock(&aw8622x->lock);
	aw8622x->bullet_nr = val;
	mutex_unlock(&aw8622x->lock);
	return count;
}

static ssize_t aw8622x_haptic_audio_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct aw8622x *aw8622x = g_aw8622x;
	ssize_t len = 0;

	len += snprintf(buf+len, PAGE_SIZE-len,
			"%d\n", aw8622x->haptic_audio.ctr.cnt);
	return len;
}

static ssize_t aw8622x_haptic_audio_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct aw8622x *aw8622x = g_aw8622x;
	unsigned int databuf[6] = {0};
	int rtp_is_going_on = 0;
	struct haptic_ctr *hap_ctr = NULL;

	rtp_is_going_on = aw8622x_haptic_juge_RTP_is_going_on(aw8622x);
	if (rtp_is_going_on) {
		aw_dev_info(aw8622x->dev,
			"%s: RTP is runing, stop audio haptic\n", __func__);
		return count;
	}
	if (!aw8622x->ram_init)
		return count;

	if (sscanf(buf, "%d %d %d %d %d %d",
		&databuf[0], &databuf[1], &databuf[2],
		&databuf[3], &databuf[4], &databuf[5]) == 6) {
		if (databuf[2]) {
			aw_dev_info(aw8622x->dev, "%s: cnt=%d, cmd=%d, play=%d, wavseq=%d, loop=%d, gain=%d\n",
				__func__,
				databuf[0], databuf[1], databuf[2],
				databuf[3], databuf[4], databuf[5]);
			hap_ctr = (struct haptic_ctr *)kzalloc(sizeof(struct haptic_ctr),
								GFP_KERNEL);
			if (hap_ctr == NULL) {
				aw_dev_err(aw8622x->dev, "%s: kzalloc memory fail\n",
					   __func__);
				return count;
			}
			mutex_lock(&aw8622x->haptic_audio.lock);
			hap_ctr->cnt = (unsigned char)databuf[0];
			hap_ctr->cmd = (unsigned char)databuf[1];
			hap_ctr->play = (unsigned char)databuf[2];
			hap_ctr->wavseq = (unsigned char)databuf[3];
			hap_ctr->loop = (unsigned char)databuf[4];
			hap_ctr->gain = (unsigned char)databuf[5];
			aw8622x_haptic_audio_ctr_list_insert(&aw8622x->haptic_audio,
							hap_ctr, aw8622x->dev);
			if (hap_ctr->cmd == 0xff) {
				aw_dev_info(aw8622x->dev,
					"%s: haptic_audio stop\n", __func__);
				if (hrtimer_active(&aw8622x->haptic_audio.timer)) {
					aw_dev_info(aw8622x->dev, "%s: cancel haptic_audio_timer\n",
						__func__);
					hrtimer_cancel(&aw8622x->haptic_audio.timer);
					aw8622x->haptic_audio.ctr.cnt = 0;
					aw8622x_haptic_audio_off(aw8622x);
				}
			} else {
				if (hrtimer_active(&aw8622x->haptic_audio.timer)) {
				} else {
					aw_dev_info(aw8622x->dev, "%s: start haptic_audio_timer\n",
						__func__);
					aw8622x_haptic_audio_init(aw8622x);
					hrtimer_start(&aw8622x->haptic_audio.timer,
					ktime_set(aw8622x->haptic_audio.delay_val/1000000,
						(aw8622x->haptic_audio.delay_val%1000000)*1000),
					HRTIMER_MODE_REL);
				}
			}

		}
		mutex_unlock(&aw8622x->haptic_audio.lock);
		kfree(hap_ctr);
	}
	return count;
}

static ssize_t aw8622x_ram_num_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct aw8622x *aw8622x = g_aw8622x;
	ssize_t len = 0;

	aw8622x_haptic_get_ram_number(aw8622x);
	len += snprintf(buf+len, PAGE_SIZE-len,
			"ram_num = %d\n", aw8622x->ram.ram_num);
	return len;
}

static DEVICE_ATTR(f0, 0644, aw8622x_f0_show, aw8622x_f0_store);
static DEVICE_ATTR(cont, 0644, aw8622x_cont_show, aw8622x_cont_store);
static DEVICE_ATTR(register, 0644, aw8622x_reg_show, aw8622x_reg_store);
static DEVICE_ATTR(duration, 0644, aw8622x_duration_show,
		   aw8622x_duration_store);
static DEVICE_ATTR(index, 0644, aw8622x_index_show, aw8622x_index_store);
static DEVICE_ATTR(activate, 0644, aw8622x_activate_show,
		   aw8622x_activate_store);
static DEVICE_ATTR(activate_mode, 0644, aw8622x_activate_mode_show,
		   aw8622x_activate_mode_store);
static DEVICE_ATTR(seq, 0644, aw8622x_seq_show, aw8622x_seq_store);
static DEVICE_ATTR(loop, 0644, aw8622x_loop_show, aw8622x_loop_store);
static DEVICE_ATTR(rtp, 0644, aw8622x_rtp_show, aw8622x_rtp_store);
static DEVICE_ATTR(state, 0644, aw8622x_state_show, aw8622x_state_store);
static DEVICE_ATTR(sram_size, 0644, aw8622x_sram_size_show,
		   aw8622x_sram_size_store);
static DEVICE_ATTR(osc_cali, 0644, aw8622x_osc_cali_show,
		   aw8622x_osc_cali_store);
static DEVICE_ATTR(gain, 0644, aw8622x_gain_show, aw8622x_gain_store);
static DEVICE_ATTR(ram_update, 0644, aw8622x_ram_update_show,
		   aw8622x_ram_update_store);
static DEVICE_ATTR(f0_save, 0644, aw8622x_f0_save_show, aw8622x_f0_save_store);
static DEVICE_ATTR(osc_save, 0644, aw8622x_osc_save_show,
		   aw8622x_osc_save_store);
static DEVICE_ATTR(trig, 0644, aw8622x_trig_show, aw8622x_trig_store);
static DEVICE_ATTR(ram_vbat_comp, 0644, aw8622x_ram_vbat_compensate_show,
		   aw8622x_ram_vbat_compensate_store);
static DEVICE_ATTR(cali, 0644, aw8622x_cali_show, aw8622x_cali_store);
static DEVICE_ATTR(cont_wait_num, 0644, aw8622x_cont_wait_num_show,
		   aw8622x_cont_wait_num_store);
static DEVICE_ATTR(cont_drv_lvl, 0644, aw8622x_cont_drv_lvl_show,
		   aw8622x_cont_drv_lvl_store);
static DEVICE_ATTR(cont_drv_time, 0644, aw8622x_cont_drv_time_show,
		   aw8622x_cont_drv_time_store);
static DEVICE_ATTR(cont_brk_time, 0644, aw8622x_cont_brk_time_show,
		   aw8622x_cont_brk_time_store);
static DEVICE_ATTR(vbat_monitor, 0644, aw8622x_vbat_monitor_show,
		   aw8622x_vbat_monitor_store);
static DEVICE_ATTR(lra_resistance, 0644, aw8622x_lra_resistance_show,
		   aw8622x_lra_resistance_store);
static DEVICE_ATTR(prctmode, 0644, aw8622x_prctmode_show,
		   aw8622x_prctmode_store);
static DEVICE_ATTR(gun_type, 0644, aw8622x_gun_type_show,
		   aw8622x_gun_type_store);
static DEVICE_ATTR(bullet_nr, 0644, aw8622x_bullet_nr_show,
		   aw8622x_bullet_nr_store);
static DEVICE_ATTR(haptic_audio, 0644, aw8622x_haptic_audio_show,
		   aw8622x_haptic_audio_store);
static DEVICE_ATTR(ram_num, 0644, aw8622x_ram_num_show, NULL);
static struct attribute *aw8622x_vibrator_attributes[] = {
	&dev_attr_state.attr,
	&dev_attr_duration.attr,
	&dev_attr_activate.attr,
	&dev_attr_activate_mode.attr,
	&dev_attr_index.attr,
	&dev_attr_gain.attr,
	&dev_attr_seq.attr,
	&dev_attr_loop.attr,
	&dev_attr_register.attr,
	&dev_attr_rtp.attr,
	&dev_attr_ram_update.attr,
	&dev_attr_f0.attr,
	&dev_attr_cali.attr,
	&dev_attr_f0_save.attr,
	&dev_attr_osc_save.attr,
	&dev_attr_cont.attr,
	&dev_attr_cont_wait_num.attr,
	&dev_attr_cont_drv_lvl.attr,
	&dev_attr_cont_drv_time.attr,
	&dev_attr_cont_brk_time.attr,
	&dev_attr_vbat_monitor.attr,
	&dev_attr_lra_resistance.attr,
	&dev_attr_prctmode.attr,
	&dev_attr_sram_size.attr,
	&dev_attr_ram_vbat_comp.attr,
	&dev_attr_osc_cali.attr,
	&dev_attr_gun_type.attr,
	&dev_attr_bullet_nr.attr,
	&dev_attr_haptic_audio.attr,
	&dev_attr_trig.attr,
	&dev_attr_ram_num.attr,
	NULL
};

struct attribute_group aw8622x_vibrator_attribute_group = {
	.attrs = aw8622x_vibrator_attributes
};

static const struct attribute_group *vibr_group[] = {
        &aw8622x_vibrator_attribute_group,
        NULL
};
static struct led_classdev led_vibr = {
        .name           = "vibrator",
        .groups         = vibr_group,
};

static void aw8622x_long_vibrate_work_routine(struct work_struct *work)
{
	struct aw8622x *aw8622x = container_of(work, struct aw8622x,
					       long_vibrate_work);

	//aw_dev_info(aw8622x->dev, "%s enter\n", __func__);

	mutex_lock(&aw8622x->lock);
	/* Enter standby mode */
	aw8622x_haptic_stop(aw8622x);
	aw8622x_haptic_upload_lra(aw8622x, F0_CALI);
	if (aw8622x->state) {
		if (aw8622x->activate_mode ==
			AW8622X_HAPTIC_ACTIVATE_RAM_MODE) {
			aw8622x_haptic_ram_vbat_compensate(aw8622x, true);
			aw8622x_haptic_play_repeat_seq(aw8622x, true);
		} else if (aw8622x->activate_mode ==
			   AW8622X_HAPTIC_ACTIVATE_CONT_MODE) {
			aw_dev_info(aw8622x->dev, "%s mode:%s\n", __func__,
				    "AW8622X_HAPTIC_ACTIVATE_CONT_MODE");
			aw8622x_haptic_cont_config(aw8622x);
		} else {
			aw_dev_err(aw8622x->dev, "%s: activate_mode error\n",
				   __func__);
		}
		/* run ms timer */
		hrtimer_start(&aw8622x->timer,
			      ktime_set(aw8622x->duration / 1000,
					(aw8622x->duration % 1000) * 1000000),
			      HRTIMER_MODE_REL);
	}
	mutex_unlock(&aw8622x->lock);
}

#ifdef AW_TIKTAP
#if 0
static int aw8622x_tiktap_rtp_play(struct aw8622x *aw8622x)
{
	unsigned int buf_len = 0;
	unsigned char glb_state_val = 0;

	aw_dev_info(aw8622x->dev, "%s enter\n", __func__);
	pm_qos_add_request(&aw8622x_pm_qos_req_vb, PM_QOS_CPU_DMA_LATENCY,
			   AW8622X_PM_QOS_VALUE_VB);
	aw8622x->rtp_cnt = 0;
	while (true) {
		if ((aw8622x->play_mode != AW8622X_HAPTIC_RTP_MODE) ||
		    (aw8622x->tiktap_stop_flag == true)) {
			return 0;
		}
		if (aw8622x_haptic_rtp_get_fifo_afs(aw8622x)) {
			mdelay(1);
			continue;
		}
		aw_dev_info(aw8622x->dev, "%s tiktap rtp_cnt = %d\n", __func__,
			    aw8622x->rtp_cnt);
		aw_dev_info(aw8622x->dev, "%s tiktap_rtp->len = %d\n", __func__,
			    aw8622x->tiktap_rtp->len);
		if (!aw8622x->tiktap_rtp) {
			aw_dev_info(aw8622x->dev,
				    "%s:tiktap_rtp is null, break!\n",
				    __func__);
			break;
		}
		if (aw8622x->rtp_cnt < (aw8622x->ram.base_addr)) {
			if ((aw8622x->tiktap_rtp->len - aw8622x->rtp_cnt) <
			    (aw8622x->ram.base_addr)) {
				buf_len = aw8622x->tiktap_rtp->len - aw8622x->rtp_cnt;
			} else {
				buf_len = aw8622x->ram.base_addr;
			}
		}
		else if ((aw8622x->tiktap_rtp->len - aw8622x->rtp_cnt) <
			 (aw8622x->ram.base_addr >> 2)) {
			buf_len = aw8622x->tiktap_rtp->len - aw8622x->rtp_cnt;
		} else {
			buf_len = aw8622x->ram.base_addr >> 2;
		}
		aw_dev_info(aw8622x->dev, "%s buf_len = %d\n", __func__,
			    buf_len);
		aw8622x_i2c_writes(aw8622x, AW8622X_REG_RTPDATA,
				   &aw8622x->tiktap_rtp->data[aw8622x->rtp_cnt],
				   buf_len);
		aw8622x->rtp_cnt += buf_len;
		aw8622x_i2c_read(aw8622x, AW8622X_REG_GLBRD5, &glb_state_val);
		if ((aw8622x->rtp_cnt == aw8622x->tiktap_rtp->len) || ((glb_state_val & 0x0f) == 0x00)) {
			if (aw8622x->rtp_cnt == aw8622x->tiktap_rtp->len)
				aw_dev_info(aw8622x->dev, "%s: tiktap_rtp update complete!\n",
					    __func__);
			else
				aw_dev_err(aw8622x->dev, "%s: tiktap_rtp play suspend!\n",
					   __func__);
			aw8622x->rtp_cnt = 0;
			pm_qos_remove_request(&aw8622x_pm_qos_req_vb);
			return 0;
		}
	}

	if (aw8622x->play_mode == AW8622X_HAPTIC_RTP_MODE)
		aw8622x_haptic_set_rtp_aei(aw8622x, false);

	aw_dev_info(aw8622x->dev, "%s exit\n", __func__);
	pm_qos_remove_request(&aw8622x_pm_qos_req_vb);
	return 0;
}
#endif

static int aw8622x_tiktap_i2c_writes(struct aw8622x *aw8622x,
			      struct mmap_buf_format *tiktap_buf)
{
	int ret = -1;

	ret = i2c_master_send(aw8622x->i2c, &(tiktap_buf->reg_addr), tiktap_buf->length + 1);
	if (ret < 0)
		aw_dev_err(aw8622x->dev, "%s: i2c master send error\n",
			   __func__);
	return ret;
}

static inline unsigned int aw8622x_get_sys_msecs(void)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
	struct timespec64 ts64;
	ktime_get_coarse_real_ts64(&ts64);
#else
	struct timespec64 ts64 = current_kernel_time64();
#endif

	return jiffies_to_msecs(timespec64_to_jiffies(&ts64));
}

static void tiktap_clean_buf(struct aw8622x *aw8622x, int status)
{
	struct mmap_buf_format *tiktap_buf = aw8622x->start_buf;
	int i = 0;

	for( i = 0; i < TIKTAP_MMAP_BUF_SUM; i++)
	{
		tiktap_buf->status = status;
		tiktap_buf = tiktap_buf->kernel_next;
	}
}

static void aw8622x_rtp_work_tiktap(struct work_struct *work)
{
	//struct aw8622x *aw8622x = container_of(work, struct aw8622x, rtp_tiktap);
	struct aw8622x *aw8622x = g_aw8622x;
	struct mmap_buf_format *tiktap_buf = aw8622x->start_buf;
	int count = 100;
	unsigned char reg_val = 0x10;
	unsigned char glb_state_val = 0;
	unsigned int write_start;
	unsigned int buf_cnt = 0;

	mutex_lock(&aw8622x->lock);
	aw8622x->tiktap_stop_flag = false;
	while(true && count--)
	{
		if(tiktap_buf->status == MMAP_BUF_DATA_VALID) {
			aw8622x_haptic_play_mode(aw8622x, AW8622X_HAPTIC_RTP_MODE);
			aw8622x_haptic_play_go(aw8622x, true);
			break;
		} else if(aw8622x->tiktap_stop_flag == true) {
			mutex_unlock(&aw8622x->lock);
			return;
		} else {
			mdelay(1);
		}
	}
	if (count <= 0) {
		aw_dev_err(aw8622x->dev, "%s error, start_buf->status != VALID \n", __func__);
		aw8622x->tiktap_stop_flag = true;
		mutex_unlock(&aw8622x->lock);
		return;
	}
	mutex_unlock(&aw8622x->lock);

	mutex_lock(&aw8622x->rtp_lock);
	pm_qos_add_request(&aw8622x_pm_qos_req_vb, PM_QOS_CPU_DMA_LATENCY,
			   AW8622X_PM_QOS_VALUE_VB);
	write_start = aw8622x_get_sys_msecs();
	reg_val = 0x10;
	while(true)
	{
		if(aw8622x_get_sys_msecs() > (write_start + 800)) {
			aw_dev_err(aw8622x->dev, "Failed ! %s endless loop\n", __func__);
			break;
		}
		aw8622x_i2c_read(aw8622x, AW8622X_REG_GLBRD5, &glb_state_val);
		if ((glb_state_val & 0x0f) != 0x08) {
			aw_dev_err(aw8622x->dev, "%s: tiktap glb_state != RTP_GO!\n",
				   __func__);
			break;
		}
		if(reg_val & 0x01 || (aw8622x->tiktap_stop_flag == true) || (tiktap_buf->status == MMAP_BUF_DATA_FINISHED) \
				|| (tiktap_buf->status == MMAP_BUF_DATA_INVALID)) {
			break;
		} else if(tiktap_buf->status == MMAP_BUF_DATA_VALID && (reg_val & (0x01 << 4))) {
			aw_dev_info(aw8622x->dev, "%s: buf_cnt = %d, bit = %d, length = %d!\n",
				    __func__, buf_cnt, tiktap_buf->bit,tiktap_buf->length);
			aw8622x_tiktap_i2c_writes(aw8622x, tiktap_buf);
			memset(tiktap_buf->data, 0, tiktap_buf->length);
			tiktap_buf->length = 0;
			tiktap_buf->status = MMAP_BUF_DATA_FINISHED;
			tiktap_buf = tiktap_buf->kernel_next;
			write_start = aw8622x_get_sys_msecs();
			buf_cnt++;
		} else {
			mdelay(1);
		}
		aw8622x_i2c_read(aw8622x, AW8622X_REG_SYSST, &reg_val);
	}

	pm_qos_remove_request(&aw8622x_pm_qos_req_vb);
	aw8622x->tiktap_stop_flag = true;
	mutex_unlock(&aw8622x->rtp_lock);
}

static void aw8622x_rtp_irq_work_tiktap(struct work_struct *work)
{
	unsigned int cnt = 200;
	unsigned char reg_val = 0;
	bool rtp_work_flag = false;
	struct aw8622x *aw8622x = container_of(work, struct aw8622x,
					     rtp_irq_tiktap);
	aw_dev_info(aw8622x->dev, "%s enter\n", __func__);

	mutex_lock(&aw8622x->lock);
	aw8622x_haptic_stop(aw8622x);
	aw8622x_haptic_set_rtp_aei(aw8622x, false);
	aw8622x_interrupt_clear(aw8622x);
	aw8622x_haptic_play_mode(aw8622x, AW8622X_HAPTIC_RTP_MODE);
	aw8622x_haptic_play_go(aw8622x, true);
	//usleep_range(2000, 2500);
	while (cnt) {
		aw8622x_i2c_read(aw8622x, AW8622X_REG_GLBRD5, &reg_val);
		if ((reg_val & 0x0f) == 0x08) {
			cnt = 0;
			rtp_work_flag = true;
			aw_dev_info(aw8622x->dev, "%s RTP_GO! glb_state=0x08\n",
				    __func__);
		} else {
			cnt--;
			aw_dev_dbg(aw8622x->dev,
				   "%s wait for RTP_GO, glb_state=0x%02X\n",
				   __func__, reg_val);
		}
		usleep_range(1000, 1500);
	}
	if (!rtp_work_flag) {
		aw_dev_err(aw8622x->dev, "%s failed to enter RTP_GO status!\n",
			   __func__);
		aw8622x_haptic_stop(aw8622x);
	}
	mutex_unlock(&aw8622x->lock);

	if (aw8622x->tiktap_stop_flag == false) {
		aw8622x_haptic_rtp_init(aw8622x);
	}
}

static long aw8622x_tiktap_unlocked_ioctl(struct file *file, unsigned int cmd,
					unsigned long arg)
{
	struct aw8622x *aw8622x = g_aw8622x;
	unsigned char reg_addr = 0;
	unsigned char reg_data = 0;
	int ret = 0;
	unsigned int tmp = 0;

	switch (cmd) {
	case TIKTAP_GET_HWINFO:
		aw_dev_info(aw8622x->dev, "%s cmd = TIKTAP_GET_HWINFO!\n", __func__);
		tmp = aw8622x->chipid;
		if (copy_to_user((void __user *)arg, &tmp, sizeof(unsigned int)))
			ret = -EFAULT;
		break;
	case TIKTAP_SETTING_GAIN:
		aw_dev_info(aw8622x->dev, "%s cmd = TIKTAP_SETTING_GAIN!, arg = 0x%02lx\n", __func__, arg);
		mutex_lock(&aw8622x->lock);
		aw8622x->gain = arg;
		aw8622x_haptic_set_gain(aw8622x, aw8622x->gain);
		mutex_unlock(&aw8622x->lock);
		break;
	case TIKTAP_GET_F0:
		aw_dev_info(aw8622x->dev, "%s cmd = TIKTAP_GET_F0!\n", __func__);
		tmp = aw8622x->f0;
		if (copy_to_user((void __user *)arg, &tmp, sizeof(unsigned int)))
			ret = -EFAULT;
		break;
	case TIKTAP_WRITE_REG:
		aw_dev_info(aw8622x->dev, "%s cmd = TIKTAP_WRITE_REG!\n", __func__);
		reg_addr = (arg & 0xFF00) >> 8;
		reg_data = arg & 0x00FF;
		aw8622x_i2c_write(aw8622x, reg_addr, reg_data);
		break;
	case TIKTAP_READ_REG:
		aw_dev_info(aw8622x->dev, "%s cmd = TIKTAP_READ_REG!\n", __func__);
		if(copy_from_user(&reg_addr, (void __user *)arg, sizeof(unsigned char))) {
			ret = -EFAULT;
			break;
		}
		aw8622x_i2c_read(aw8622x, reg_addr, &reg_data);
		if (copy_to_user((void __user *)arg, &reg_data, sizeof(unsigned char)))
			ret = -EFAULT;
		break;
	case TIKTAP_STOP_MODE:
		aw_dev_info(aw8622x->dev, "%s cmd = TIKTAP_STOP_MODE!\n", __func__);
		aw8622x->tiktap_stop_flag = true;
		mutex_lock(&aw8622x->lock);
		aw8622x_haptic_stop(aw8622x);
		mutex_unlock(&aw8622x->lock);
		break;
	case TIKTAP_ON_MODE:
		aw_dev_info(aw8622x->dev, "%s cmd = TIKTAP_ON_MODE!, arg = %ld\n", __func__, arg);
		tmp = arg;
		vfree(aw8622x->tiktap_rtp);
		aw8622x->tiktap_rtp = NULL;
		aw8622x->tiktap_rtp = vmalloc(tmp);
		if (aw8622x->tiktap_rtp == NULL) {
			aw_dev_err(aw8622x->dev, "%s malloc tiktap_rtp memory failed\n", __func__);
			return -ENOMEM;
		}
		break;
	case TIKTAP_OFF_MODE:
		aw_dev_info(aw8622x->dev, "%s cmd = TIKTAP_OFF_MODE!\n", __func__);
		aw8622x->tiktap_stop_flag = true;
		vfree(aw8622x->tiktap_rtp);
		break;
	case TIKTAP_RTP_MODE:
		//aw_dev_info(aw8622x->dev, "%s cmd = TIKTAP_RTP_MODE!\n", __func__);
		tiktap_clean_buf(aw8622x, MMAP_BUF_DATA_INVALID);
		aw8622x->tiktap_stop_flag = true;
		if (aw8622x->vib_stop_flag == false) {
			mutex_lock(&aw8622x->lock);
			aw8622x_haptic_stop(aw8622x);
			mutex_unlock(&aw8622x->lock);
		}

		schedule_work(&aw8622x->rtp_tiktap);
		break;
	case TIKTAP_SETTING_SPEED:
		aw_dev_info(aw8622x->dev, "%s cmd = TIKTAP_SETTING_SPEED!, arg = 0x%02lx\n", __func__, arg);
		aw8622x_haptic_set_pwm(aw8622x, arg);
		break;
	case TIKTAP_GET_SPEED:
		aw_dev_info(aw8622x->dev, "%s cmd = TIKTAP_GET_SPEED!\n", __func__);
		tmp = aw8622x->pwm_mode;
		if (copy_to_user((void __user *)arg, &tmp, sizeof(unsigned int)))
			ret = -EFAULT;
		break;
	default:
		aw_dev_info(aw8622x->dev, "%s unknown cmd = %d\n", __func__, cmd);
		break;
	}
	return ret;
}

#define WRITE_RTP_MODE		1
#define WRITE_STOP_MODE		2

static ssize_t aw_buf_write_proc(struct file *filp,
				 const char __user *buffer,
				 size_t count, loff_t *off)
{
	struct aw8622x *aw8622x = g_aw8622x;
	switch (count) {
	case WRITE_RTP_MODE:
		tiktap_clean_buf(aw8622x, MMAP_BUF_DATA_INVALID);
		aw8622x->tiktap_stop_flag = true;
		if (aw8622x->vib_stop_flag == false) {
			mutex_lock(&aw8622x->lock);
			aw8622x_haptic_stop(aw8622x);
			mutex_unlock(&aw8622x->lock);
		}
		schedule_work(&aw8622x->rtp_tiktap);
		break;
	case WRITE_STOP_MODE:
		aw8622x->tiktap_stop_flag = true;
		mutex_lock(&aw8622x->lock);
		aw8622x_haptic_stop(aw8622x);
		mutex_unlock(&aw8622x->lock);
		break;
	default:
		break;
	}

	return count;
}

static int aw8622x_file_mmap(struct file *filp, struct vm_area_struct *vma)
{
	unsigned long phys;
	struct aw8622x *aw8622x = g_aw8622x;
	int ret = 0;

#if LINUX_VERSION_CODE > KERNEL_VERSION(4,7,0)
	vm_flags_t vm_flags = calc_vm_prot_bits(PROT_READ|PROT_WRITE, 0) | calc_vm_flag_bits(MAP_SHARED);

	vm_flags |= current->mm->def_flags | VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC| VM_SHARED | VM_MAYSHARE;

	if(vma && (pgprot_val(vma->vm_page_prot) != pgprot_val(vm_get_page_prot(vm_flags)))) {
		aw_dev_err(aw8622x->dev, "%s: vm_page_prot error!\n", __func__);
		return -EPERM;
	}

	if(vma && ((vma->vm_end - vma->vm_start) != (PAGE_SIZE << TIKTAP_MMAP_PAGE_ORDER))) {
		aw_dev_err(aw8622x->dev, "%s: mmap size check err!\n", __func__);
		return -EPERM;
	}
#endif
	phys = virt_to_phys(aw8622x->start_buf);

	ret = remap_pfn_range(vma, vma->vm_start, (phys >> PAGE_SHIFT), (vma->vm_end - vma->vm_start), vma->vm_page_prot);
	if(ret) {
		aw_dev_err(aw8622x->dev, "%s: mmap failed!\n", __func__);
		return ret;
	}

	aw_dev_info(aw8622x->dev, "%s success!\n", __func__);

	return ret;
}

static const struct file_operations config_proc_ops = {
    .owner = THIS_MODULE,
    .write = aw_buf_write_proc,
    .unlocked_ioctl = aw8622x_tiktap_unlocked_ioctl,
    .mmap = aw8622x_file_mmap,
};
#endif

int aw8622x_vibrator_init(struct aw8622x *aw8622x)
{
	//int ret = 0;

	char * ptr;

	aw_dev_info(aw8622x->dev, "%s enter\n", __func__);

	ptr = strstr(saved_command_line, "aw8622x_lk_f0_cali=");
	if (ptr != NULL) {
		ptr += strlen("aw8622x_lk_f0_cali=");
		aw8622x->f0_cali_data = simple_strtol(ptr, NULL, 0);
		aw_dev_info(aw8622x->dev, "%s aw8622x->f0_cali_data = 0x%x\n", __func__, aw8622x->f0_cali_data);
	}

#ifdef TIMED_OUTPUT
	aw_dev_info(aw8622x->dev, "%s: TIMED_OUT FRAMEWORK!\n", __func__);
	aw8622x->vib_dev.name = "awinic_vibrator";
	aw8622x->vib_dev.get_time = aw8622x_vibrator_get_time;
	aw8622x->vib_dev.enable = aw8622x_vibrator_enable;

	ret = timed_output_dev_register(&(aw8622x->vib_dev));
	if (ret < 0) {
		aw_dev_err(aw8622x->dev,
			"%s: fail to create timed output dev\n", __func__);
		return ret;
	}
	ret = sysfs_create_group(&aw8622x->vib_dev.dev->kobj,
				 &aw8622x_vibrator_attribute_group);
	if (ret < 0) {
		aw_dev_err(aw8622x->dev, "%s error creating sysfs attr files\n",
			   __func__);
		return ret;
	}
#else
#if 0
	aw_dev_info(aw8622x->dev, "%s: loaded in leds_cdev framework!\n",
		    __func__);
	aw8622x->vib_dev.name = "awinic_vibrator";
#endif
	aw8622x->vib_dev.brightness_get = aw8622x_haptic_brightness_get;
	aw8622x->vib_dev.brightness_set = aw8622x_haptic_brightness_set;
#if 0
	ret = devm_led_classdev_register(&aw8622x->i2c->dev, &aw8622x->vib_dev);
	if (ret < 0) {
		aw_dev_err(aw8622x->dev, "%s: fail to create led dev\n",
			   __func__);
		return ret;
	}
	ret = sysfs_create_group(&aw8622x->vib_dev.dev->kobj,
				 &aw8622x_vibrator_attribute_group);
	if (ret < 0) {
		aw_dev_err(aw8622x->dev, "%s error creating sysfs attr files\n",
			   __func__);
		return ret;
	}
#endif
#endif

#ifdef AW_TIKTAP
	aw8622x->aw_config_proc = NULL;
	aw8622x->aw_config_proc = proc_create(AW_TIKTAP_PROCNAME, 0666,
				     NULL, &config_proc_ops);
	if (aw8622x->aw_config_proc == NULL)
		dev_err(aw8622x->dev, "create_proc_entry %s failed\n",
			AW_TIKTAP_PROCNAME);
	else
		dev_info(aw8622x->dev, "create proc entry %s success\n",
			 AW_TIKTAP_PROCNAME);

	aw8622x->start_buf = (struct mmap_buf_format *)__get_free_pages(GFP_KERNEL, TIKTAP_MMAP_PAGE_ORDER);
	if(aw8622x->start_buf == NULL) {
		aw_dev_err(aw8622x->dev, "Error __get_free_pages failed\n");
		return -ENOMEM;
	}

	SetPageReserved(virt_to_page(aw8622x->start_buf));
	{
		struct mmap_buf_format *temp;
		uint32_t i = 0;
		temp = aw8622x->start_buf;
		for( i = 1; i < TIKTAP_MMAP_BUF_SUM; i++)
		{
			temp->kernel_next = (aw8622x->start_buf + i);
			temp = temp->kernel_next;
		}
		temp->kernel_next = aw8622x->start_buf;

		temp = aw8622x->start_buf;
		for(i = 0; i < TIKTAP_MMAP_BUF_SUM; i++)
		{
			temp->bit = i;
			temp->reg_addr = AW8622X_REG_RTPDATA;
			temp = temp->kernel_next;
		}
	}
	aw8622x->tiktap_stop_flag = true;
	aw8622x->vib_stop_flag = false;

	INIT_WORK(&aw8622x->rtp_irq_tiktap, aw8622x_rtp_irq_work_tiktap);
	INIT_WORK(&aw8622x->rtp_tiktap, aw8622x_rtp_work_tiktap);
#endif
	hrtimer_init(&aw8622x->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	aw8622x->timer.function = aw8622x_vibrator_timer_func;
	INIT_WORK(&aw8622x->long_vibrate_work,
		  aw8622x_long_vibrate_work_routine);
	INIT_WORK(&aw8622x->rtp_work, aw8622x_rtp_work_routine);
	mutex_init(&aw8622x->lock);
	mutex_init(&aw8622x->rtp_lock);

	return 0;
}

static int aw8622x_haptic_set_pwm(struct aw8622x *aw8622x, unsigned char mode)
{
	aw8622x->pwm_mode = mode;

	switch (mode) {
	case AW8622X_PWM_48K:
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL2,
				       AW8622X_BIT_SYSCTRL2_WAVDAT_MODE_MASK,
				       AW8622X_BIT_SYSCTRL2_RATE_48K);
		break;
	case AW8622X_PWM_24K:
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL2,
				       AW8622X_BIT_SYSCTRL2_WAVDAT_MODE_MASK,
				       AW8622X_BIT_SYSCTRL2_RATE_24K);
		break;
	case AW8622X_PWM_12K:
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL2,
				       AW8622X_BIT_SYSCTRL2_WAVDAT_MODE_MASK,
				       AW8622X_BIT_SYSCTRL2_RATE_12K);
		break;
	default:
		break;
	}
	return 0;
}

static void aw8622x_haptic_misc_para_init(struct aw8622x *aw8622x)
{

	aw_dev_info(aw8622x->dev, "%s enter\n", __func__);
	aw8622x->cont_drv1_lvl = aw8622x->dts_info.cont_drv1_lvl_dt;
	aw8622x->cont_drv2_lvl = aw8622x->dts_info.cont_drv2_lvl_dt;
	aw8622x->cont_drv1_time = aw8622x->dts_info.cont_drv1_time_dt;
	aw8622x->cont_drv2_time = aw8622x->dts_info.cont_drv2_time_dt;
	aw8622x->cont_brk_time = aw8622x->dts_info.cont_brk_time_dt;
	aw8622x->cont_wait_num = aw8622x->dts_info.cont_wait_num_dt;
	/* SIN_H */
	aw8622x_i2c_write(aw8622x, AW8622X_REG_SYSCTRL3,
			  aw8622x->dts_info.sine_array[0]);
	/* SIN_L */
	aw8622x_i2c_write(aw8622x, AW8622X_REG_SYSCTRL4,
			  aw8622x->dts_info.sine_array[1]);
	/* COS_H */
	aw8622x_i2c_write(aw8622x, AW8622X_REG_SYSCTRL5,
			  aw8622x->dts_info.sine_array[2]);
	/* COS_L */
	aw8622x_i2c_write(aw8622x, AW8622X_REG_SYSCTRL6,
			  aw8622x->dts_info.sine_array[3]);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_TRGCFG8,
				       AW8622X_BIT_TRGCFG8_TRG_TRIG1_MODE_MASK,
				       AW8622X_BIT_TRGCFG8_TRIG1);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_ANACFG8,
					AW8622X_BIT_ANACFG8_TRTF_CTRL_HDRV_MASK,
					AW8622X_BIT_ANACFG8_TRTF_CTRL_HDRV);

	/* d2s_gain */
	if (!aw8622x->dts_info.d2s_gain) {
		aw_dev_err(aw8622x->dev, "%s aw8622x->dts_info.d2s_gain = 0!\n",
			   __func__);
	} else {
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL7,
				       AW8622X_BIT_SYSCTRL7_D2S_GAIN_MASK,
				       aw8622x->dts_info.d2s_gain);
	}

	/* drv_width */
	aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG3,
	                  aw8622x->dts_info.cont_drv_width);

	/* cont_tset */
	if (!aw8622x->dts_info.cont_tset) {
		aw_dev_err(aw8622x->dev,
			"%s aw8622x->dts_info.cont_tset = 0!\n", __func__);
	} else {
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_CONTCFG13,
				       AW8622X_BIT_CONTCFG13_TSET_MASK,
				       aw8622x->dts_info.cont_tset << 4);
	}

	/* cont_bemf_set */
	if (!aw8622x->dts_info.cont_bemf_set) {
		aw_dev_err(aw8622x->dev, "%s aw8622x->dts_info.cont_bemf_set = 0!\n",
			   __func__);
	} else {
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_CONTCFG13,
				       AW8622X_BIT_CONTCFG13_BEME_SET_MASK,
				       aw8622x->dts_info.cont_bemf_set);
	}

	/* cont_brk_time */
	if (!aw8622x->cont_brk_time) {
		aw_dev_err(aw8622x->dev, "%s aw8622x->cont_brk_time = 0!\n",
			   __func__);
	} else {
		aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG10,
				  aw8622x->cont_brk_time);
	}

	/* cont_bst_brk_gain */
	/*
	** if (!aw8622x->dts_info.cont_bst_brk_gain) {
	**	aw_dev_err(aw8622x->dev,
	**		"%s aw8622x->dts_info.cont_bst_brk_gain = 0!\n",
	**		   __func__);
	** } else {
	**	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_CONTCFG5,
	**			       AW8622X_BIT_CONTCFG5_BST_BRK_GAIN_MASK,
	**			       aw8622x->dts_info.cont_bst_brk_gain);
	** }
	*/

	/* cont_brk_gain */
	if (!aw8622x->dts_info.cont_brk_gain) {
		aw_dev_err(aw8622x->dev, "%s aw8622x->dts_info.cont_brk_gain = 0!\n",
			   __func__);
	} else {
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_CONTCFG5,
				       AW8622X_BIT_CONTCFG5_BRK_GAIN_MASK,
				       aw8622x->dts_info.cont_brk_gain);
	}
}

/*****************************************************
 *
 * offset calibration
 *
 *****************************************************/
static int aw8622x_haptic_offset_calibration(struct aw8622x *aw8622x)
{
	unsigned int cont = 2000;
	unsigned char reg_val = 0;

	aw_dev_info(aw8622x->dev, "%s enter\n", __func__);

	aw8622x_haptic_raminit(aw8622x, true);

	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_DETCFG2,
			       AW8622X_BIT_DETCFG2_DIAG_GO_MASK,
			       AW8622X_BIT_DETCFG2_DIAG_GO_ON);
	while (1) {
		aw8622x_i2c_read(aw8622x, AW8622X_REG_DETCFG2, &reg_val);
		if ((reg_val & 0x01) == 0 || cont == 0)
			break;
		cont--;
	}
	if (cont == 0)
		aw_dev_err(aw8622x->dev, "%s calibration offset failed!\n",
			   __func__);
	aw8622x_haptic_raminit(aw8622x, false);
	return 0;
}

/*****************************************************
 *
 * vbat mode
 *
 *****************************************************/
static int aw8622x_haptic_vbat_mode_config(struct aw8622x *aw8622x,
					   unsigned char flag)
{
	if (flag == AW8622X_HAPTIC_CONT_VBAT_HW_ADJUST_MODE) {
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL1,
				       AW8622X_BIT_SYSCTRL1_VBAT_MODE_MASK,
				       AW8622X_BIT_SYSCTRL1_VBAT_MODE_HW);
	} else {
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL1,
				       AW8622X_BIT_SYSCTRL1_VBAT_MODE_MASK,
				       AW8622X_BIT_SYSCTRL1_VBAT_MODE_SW);
	}
	return 0;
}

static void aw8622x_ram_work_routine(struct work_struct *work)
{
	struct aw8622x *aw8622x = container_of(work, struct aw8622x,
						ram_work.work);

	aw_dev_info(aw8622x->dev, "%s enter\n", __func__);
	aw8622x_ram_update(aw8622x);
}

int aw8622x_ram_work_init(struct aw8622x *aw8622x)
{
	int ram_timer_val = 8000;

	aw_dev_info(aw8622x->dev, "%s enter\n", __func__);
	INIT_DELAYED_WORK(&aw8622x->ram_work, aw8622x_ram_work_routine);
	schedule_delayed_work(&aw8622x->ram_work,
				msecs_to_jiffies(ram_timer_val));
	return 0;
}
static enum hrtimer_restart
aw8622x_haptic_audio_timer_func(struct hrtimer *timer)
{
	struct aw8622x *aw8622x = container_of(timer,
					struct aw8622x, haptic_audio.timer);

	aw_dev_dbg(aw8622x->dev, "%s enter\n", __func__);
	schedule_work(&aw8622x->haptic_audio.work);

	hrtimer_start(&aw8622x->haptic_audio.timer,
		ktime_set(aw8622x->haptic_audio.timer_val/1000000,
			(aw8622x->haptic_audio.timer_val%1000000)*1000),
		HRTIMER_MODE_REL);
	return HRTIMER_NORESTART;
}

static void
aw8622x_haptic_auto_bst_enable(struct aw8622x *aw8622x, unsigned char flag)
{
	if (flag) {
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_PLAYCFG3,
				AW8622X_BIT_PLAYCFG3_BRK_EN_MASK,
				AW8622X_BIT_PLAYCFG3_BRK_ENABLE);
	} else {
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_PLAYCFG3,
				AW8622X_BIT_PLAYCFG3_BRK_EN_MASK,
				AW8622X_BIT_PLAYCFG3_BRK_DISABLE);
	}
}
int aw8622x_haptic_init(struct aw8622x *aw8622x)
{
	int ret = 0;
	unsigned char i = 0;
	unsigned char reg_val = 0;

	aw_dev_info(aw8622x->dev, "%s enter\n", __func__);
	/* haptic audio */
	aw8622x->haptic_audio.delay_val = 1;
	aw8622x->haptic_audio.timer_val = 21318;
	INIT_LIST_HEAD(&(aw8622x->haptic_audio.ctr_list));
	hrtimer_init(&aw8622x->haptic_audio.timer,
		CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	aw8622x->haptic_audio.timer.function = aw8622x_haptic_audio_timer_func;
	INIT_WORK(&aw8622x->haptic_audio.work,
		aw8622x_haptic_audio_work_routine);
	mutex_init(&aw8622x->haptic_audio.lock);
	aw8622x->gun_type = 0xff;
	aw8622x->bullet_nr = 0x00;

	mutex_lock(&aw8622x->lock);
	/* haptic init */
	aw8622x->ram_state = 0;
	aw8622x->activate_mode = aw8622x->dts_info.mode;
	ret = aw8622x_i2c_read(aw8622x, AW8622X_REG_WAVCFG1, &reg_val);
	aw8622x->index = reg_val & 0x7F;
	ret = aw8622x_i2c_read(aw8622x, AW8622X_REG_PLAYCFG2, &reg_val);
	aw8622x->gain = reg_val & 0xFF;

	aw8622x->gain = 0x64;

	aw_dev_info(aw8622x->dev, "%s aw8622x->gain =0x%02X\n", __func__,
		    aw8622x->gain);
	for (i = 0; i < AW8622X_SEQUENCER_SIZE; i++) {
		ret = aw8622x_i2c_read(aw8622x, AW8622X_REG_WAVCFG1 + i,
				       &reg_val);
		aw8622x->seq[i] = reg_val;
	}
	aw8622x_haptic_play_mode(aw8622x, AW8622X_HAPTIC_STANDBY_MODE);
	aw8622x_haptic_set_pwm(aw8622x, AW8622X_PWM_12K);
	/* misc value init */
	aw8622x_haptic_misc_para_init(aw8622x);
	/* set motor protect */
	aw8622x_haptic_swicth_motor_protect_config(aw8622x, 0x00, 0x00);
	aw8622x_haptic_trig_param_init(aw8622x);
	aw8622x_haptic_trig_param_config(aw8622x);
	aw8622x_haptic_offset_calibration(aw8622x);
	/*config auto_brake*/
	aw8622x_haptic_auto_bst_enable(aw8622x,
				       aw8622x->dts_info.is_enabled_auto_bst);
	/* vbat compensation */
	aw8622x_haptic_vbat_mode_config(aw8622x,
				AW8622X_HAPTIC_CONT_VBAT_HW_ADJUST_MODE);
	aw8622x->ram_vbat_compensate = AW8622X_HAPTIC_RAM_VBAT_COMP_ENABLE;

	/* f0 calibration */
	/*LRA trim source select register*/
	aw8622x_i2c_write_bits(aw8622x,
				AW8622X_REG_TRIMCFG1,
				AW8622X_BIT_TRIMCFG1_RL_TRIM_SRC_MASK,
				AW8622X_BIT_TRIMCFG1_RL_TRIM_SRC_REG);
	aw8622x_haptic_upload_lra(aw8622x, WRITE_ZERO);
	//aw8622x_haptic_f0_calibration(aw8622x);

#if 0
	aw8622x->f0_cali_data = aw8622x->dts_info.lk_f0_cali;
#endif
	aw_dev_info(aw8622x->dev, "%s f0_cali_data = 0x%x\n", __func__, aw8622x->f0_cali_data);
	aw8622x_haptic_upload_lra(aw8622x, F0_CALI);
	mutex_unlock(&aw8622x->lock);
	return ret;
}

void aw8622x_interrupt_setup(struct aw8622x *aw8622x)
{
	unsigned char reg_val = 0;

	aw_dev_info(aw8622x->dev, "%s enter\n", __func__);

	aw8622x_i2c_read(aw8622x, AW8622X_REG_SYSINT, &reg_val);

	aw_dev_info(aw8622x->dev, "%s: reg SYSINT=0x%02X\n", __func__, reg_val);

	/* edge int mode */
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL7,
			       AW8622X_BIT_SYSCTRL7_INT_MODE_MASK,
			       AW8622X_BIT_SYSCTRL7_INT_MODE_EDGE);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL7,
			       AW8622X_BIT_SYSCTRL7_INT_EDGE_MODE_MASK,
			       AW8622X_BIT_SYSCTRL7_INT_EDGE_MODE_POS);
	/* int enable */
	/*
	*aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSINTM,
	*			AW8622X_BIT_SYSINTM_BST_SCPM_MASK,
	*			AW8622X_BIT_SYSINTM_BST_SCPM_OFF);
	*aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSINTM,
	*			AW8622X_BIT_SYSINTM_BST_OVPM_MASK,
	*		AW8622X_BIT_SYSINTM_BST_OVPM_ON);
	*/
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSINTM,
			       AW8622X_BIT_SYSINTM_UVLM_MASK,
			       AW8622X_BIT_SYSINTM_UVLM_ON);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSINTM,
			       AW8622X_BIT_SYSINTM_OCDM_MASK,
			       AW8622X_BIT_SYSINTM_OCDM_ON);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSINTM,
			       AW8622X_BIT_SYSINTM_OTM_MASK,
			       AW8622X_BIT_SYSINTM_OTM_ON);
}

irqreturn_t aw8622x_irq(int irq, void *data)
{
	struct aw8622x *aw8622x = data;
	unsigned char reg_val = 0;
	unsigned int buf_len = 0;
	unsigned char glb_state_val = 0;

	aw_dev_info(aw8622x->dev, "%s enter\n", __func__);

	if(aw8622x->tiktap_stop_flag == false) {
		return IRQ_HANDLED;
	}

	aw8622x_i2c_read(aw8622x, AW8622X_REG_SYSINT, &reg_val);
	aw_dev_info(aw8622x->dev, "%s: reg SYSINT=0x%02X\n", __func__, reg_val);
	if (reg_val & AW8622X_BIT_SYSINT_UVLI)
		aw_dev_err(aw8622x->dev, "%s chip uvlo int error\n", __func__);
	if (reg_val & AW8622X_BIT_SYSINT_OCDI)
		aw_dev_err(aw8622x->dev, "%s chip over current int error\n",
			   __func__);
	if (reg_val & AW8622X_BIT_SYSINT_OTI)
		aw_dev_err(aw8622x->dev, "%s chip over temperature int error\n",
			   __func__);
	if (reg_val & AW8622X_BIT_SYSINT_DONEI)
		aw_dev_info(aw8622x->dev, "%s chip playback done\n", __func__);

	if (reg_val & AW8622X_BIT_SYSINT_FF_AEI) {
		aw_dev_info(aw8622x->dev, "%s: aw8622x rtp fifo almost empty\n",
			    __func__);
		if (aw8622x->rtp_init) {
			while ((!aw8622x_haptic_rtp_get_fifo_afs(aw8622x)) &&
			       (aw8622x->play_mode ==
				AW8622X_HAPTIC_RTP_MODE)) {
				mutex_lock(&aw8622x->rtp_lock);
				if (!aw8622x->rtp_cnt) {
					aw_dev_info(aw8622x->dev, "%s:aw8622x->rtp_cnt is 0!\n",
						    __func__);
					mutex_unlock(&aw8622x->rtp_lock);
					break;
				}
#ifdef AW_ENABLE_RTP_PRINT_LOG
				aw_dev_info(aw8622x->dev,
					"%s: aw8622x rtp mode fifo update, cnt=%d\n",
					__func__, aw8622x->rtp_cnt);
#endif
				if (!aw8622x->rtp_container) {
					aw_dev_info(aw8622x->dev,
						"%s:aw8622x->rtp_container is null, break!\n",
						__func__);
					mutex_unlock(&aw8622x->rtp_lock);
					break;
				}
				if ((aw8622x->rtp_container->len - aw8622x->rtp_cnt) <
				    (aw8622x->ram.base_addr >> 2)) {
					buf_len =
					    aw8622x->rtp_container->len - aw8622x->rtp_cnt;
				} else {
					buf_len = (aw8622x->ram.base_addr >> 2);
				}
				aw8622x->rtp_update_flag =
				    aw8622x_i2c_writes(aw8622x,
						AW8622X_REG_RTPDATA,
						&aw8622x->rtp_container->data
						[aw8622x->rtp_cnt],
						buf_len);
				aw8622x->rtp_cnt += buf_len;
				aw8622x_i2c_read(aw8622x, AW8622X_REG_GLBRD5,
						 &glb_state_val);
				if ((aw8622x->rtp_cnt == aw8622x->rtp_container->len)
				    || ((glb_state_val & 0x0f) == 0)) {
					if (aw8622x->rtp_cnt ==
						aw8622x->rtp_container->len)
						aw_dev_info(aw8622x->dev,
							"%s: rtp load completely! glb_state_val=%02x aw8622x->rtp_cnt=%d\n",
							__func__, glb_state_val,
							aw8622x->rtp_cnt);
					else
						aw_dev_err(aw8622x->dev,
							"%s rtp load failed!! glb_state_val=%02x aw8622x->rtp_cnt=%d\n",
							__func__, glb_state_val,
							aw8622x->rtp_cnt);

					aw8622x_haptic_set_rtp_aei(aw8622x,
								false);
					aw8622x->rtp_cnt = 0;
					aw8622x->rtp_init = 0;
					mutex_unlock(&aw8622x->rtp_lock);
					break;
				}
				mutex_unlock(&aw8622x->rtp_lock);
			}
		} else {
			aw_dev_info(aw8622x->dev, "%s: aw8622x rtp init = %d, init error\n",
				    __func__, aw8622x->rtp_init);
		}
	}

	if (reg_val & AW8622X_BIT_SYSINT_FF_AFI)
		aw_dev_info(aw8622x->dev, "%s: aw8622x rtp mode fifo almost full!\n",
			    __func__);

	if (aw8622x->play_mode != AW8622X_HAPTIC_RTP_MODE)
		aw8622x_haptic_set_rtp_aei(aw8622x, false);

	aw_dev_info(aw8622x->dev, "%s exit\n", __func__);

	return IRQ_HANDLED;
}

char aw8622x_check_qualify(struct aw8622x *aw8622x)
{
	unsigned char reg = 0;
	int ret = 0;

	ret = aw8622x_i2c_read(aw8622x, AW8622X_REG_EFRD9, &reg);
	if (ret < 0) {
		aw_dev_err(aw8622x->dev, "%s: failed to read register 0x64: %d\n",
			   __func__, ret);
		return ret;
	}
	if ((reg & 0x80) == 0x80)
		return 1;
	aw_dev_err(aw8622x->dev, "%s: register 0x64 error: 0x%02x\n",
			__func__, reg);
	return 0;
}

static const struct of_device_id vibr_of_ids[] = {
	{ .compatible = "mediatek,vibrator", },
	{}
};

static int vib_probe(struct platform_device *pdev)
{
	int ret = 0;

	pr_info(VIB_TAG "probe enter\n");

	ret = devm_led_classdev_register(&pdev->dev, &led_vibr);
	if (ret < 0) {
		pr_err(VIB_TAG "led class register fail\n");
		return ret;
	}

	pr_info(VIB_TAG "probe done\n");

	return 0;
}

static int vib_remove(struct platform_device *pdev)
{
	devm_led_classdev_unregister(&pdev->dev, &led_vibr);

	return 0;
}

static void vib_shutdown(struct platform_device *pdev)
{
	pr_info(VIB_TAG "shutdown: enter!\n");
}


static struct platform_driver vibrator_driver = {
	.probe = vib_probe,
	.remove = vib_remove,
	.shutdown = vib_shutdown,
	.driver = {
			.name = VIB_DEVICE,
			.owner = THIS_MODULE,
#ifdef CONFIG_OF
			.of_match_table = vibr_of_ids,
#endif
		   },
};

static int vib_mod_init(void)
{
	s32 ret;

	ret = platform_driver_register(&vibrator_driver);
	if (ret) {
		pr_err(VIB_TAG "Unable to register driver (%d)\n", ret);
		return ret;
	}
	pr_info(VIB_TAG "init Done\n");

	return 0;
}

static void vib_mod_exit(void)
{
	pr_info(VIB_TAG "%s: Done\n", __func__);
}

module_init(vib_mod_init);
module_exit(vib_mod_exit);
