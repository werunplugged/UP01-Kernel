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
#include <linux/interrupt.h>
#include <linux/debugfs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/power_supply.h>
#include <linux/pm_qos.h>
#include <linux/proc_fs.h>
#ifdef CONFIG_PM_WAKELOCKS
#include <linux/pm_wakeup.h>
#else
#include <linux/wakelock.h>
#endif
#include <linux/jiffies.h>
#include <linux/vmalloc.h>
#include "aw8624.h"
#include "aw8624_reg.h"
#include "haptic.h"
/******************************************************
 *
 * Value
 *
 ******************************************************/
static char *aw8624_ram_name = "aw8624_haptic.bin";
static char aw8624_rtp_name[][AW8624_RTP_NAME_MAX] = {
	{"aw8624_osc_rtp_24K_5s.bin"},
	{"aw8624_rtp.bin"},
	{"aw8624_rtp_lighthouse.bin"},
	{"aw8624_rtp_silk.bin"},
};
struct aw8624_dts_info aw8624_dts_data;
struct pm_qos_request aw8624_pm_qos_req_vb;

/******************************************************
*
* functions
*
******************************************************/
static void aw8624_interrupt_clear(struct aw8624 *aw8624);
static void aw8624_haptic_upload_lra(struct aw8624 *aw8624, unsigned int flag);
static int aw8624_haptic_stop(struct aw8624 *aw8624);
static int aw8624_analyse_duration_range(struct aw8624 *aw8624);

/******************************************************
*
* aw8624 i2c write/read
*
******************************************************/
static int aw8624_i2c_write(struct aw8624 *aw8624,
		 unsigned char reg_addr, unsigned char reg_data)
{
	int ret = -1;
	unsigned char cnt = 0;

	while (cnt < AW8624_I2C_RETRIES) {
		ret =
		i2c_smbus_write_byte_data(aw8624->i2c, reg_addr, reg_data);
		if (ret < 0) {
			aw_dev_err(aw8624->dev, "%s: i2c_write cnt=%d error=%d\n",
				__func__, cnt, ret);
		} else {
			break;
		}
		cnt++;
		usleep_range(2000, 3000);
	}

	return ret;
}

int aw8624_i2c_read(struct aw8624 *aw8624,
		unsigned char reg_addr, unsigned char *reg_data)
{
	int ret = -1;
	unsigned char cnt = 0;

	while (cnt < AW8624_I2C_RETRIES) {
		ret = i2c_smbus_read_byte_data(aw8624->i2c, reg_addr);
		if (ret < 0) {
			aw_dev_err(aw8624->dev, "%s: i2c_read cnt=%d error=%d\n",
				__func__, cnt, ret);
		} else {
			*reg_data = ret;
			break;
		}
		cnt++;
		usleep_range(2000, 3000);
	}

	return ret;
}

int aw8624_i2c_write_bits(struct aw8624 *aw8624,
	unsigned char reg_addr, unsigned int mask, unsigned char reg_data)
{
	unsigned char reg_val = 0;

	aw8624_i2c_read(aw8624, reg_addr, &reg_val);
	reg_val &= mask;
	reg_val |= reg_data;
	aw8624_i2c_write(aw8624, reg_addr, reg_val);

	return 0;
}

int aw8624_i2c_writes(struct aw8624 *aw8624,
		unsigned char reg_addr, unsigned char *buf, unsigned int len)
{
	int ret = -1;
	unsigned char *data;

	data = kmalloc(len+1, GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	data[0] = reg_addr;
	memcpy(&data[1], buf, len);

	ret = i2c_master_send(aw8624->i2c, data, len+1);
	if (ret < 0)
		aw_dev_err(aw8624->dev,
			"%s: i2c master send error\n", __func__);

	kfree(data);

	return ret;
}

static void aw8624_interrupt_clear(struct aw8624 *aw8624)
{
	unsigned char reg_val = 0;

	aw8624_i2c_read(aw8624, AW8624_REG_SYSINT, &reg_val);
	aw_dev_info(aw8624->dev, "%s: reg SYSINT=0x%x\n", __func__, reg_val);
}

/*****************************************************
 *
 * ram update
 *
 *****************************************************/
static void aw8624_rtp_loaded(const struct firmware *cont, void *context)
{
	struct aw8624 *aw8624 = context;

	if (!cont) {
		aw_dev_err(aw8624->dev, "%s: failed to read %s\n",
			__func__, aw8624_rtp_name[aw8624->rtp_file_num]);
		release_firmware(cont);
		return;
	}

	aw_dev_info(aw8624->dev, "%s: loaded %s - size: %zu\n", __func__,
		aw8624_rtp_name[aw8624->rtp_file_num], cont ? cont->size : 0);

	/* aw8624 rtp update */
	aw8624->rtp_container = vmalloc(cont->size+sizeof(int));
	if (!aw8624->rtp_container) {
		release_firmware(cont);
		aw_dev_err(aw8624->dev,
			"%s: Error allocating memory\n", __func__);
		return;
	}
	aw8624->rtp_container->len = cont->size;
	aw_dev_info(aw8624->dev,
		"%s: rtp size = %d\n", __func__, aw8624->rtp_container->len);
	memcpy(aw8624->rtp_container->data, cont->data, cont->size);
	release_firmware(cont);

	aw8624->rtp_init = 1;
	aw_dev_info(aw8624->dev, "%s: rtp update complete\n", __func__);
}

static int aw8624_rtp_update(struct aw8624 *aw8624)
{
	aw_dev_info(aw8624->dev, "%s enter\n", __func__);

	return request_firmware_nowait(THIS_MODULE,
					FW_ACTION_HOTPLUG,
					aw8624_rtp_name[aw8624->rtp_file_num],
					aw8624->dev,
					GFP_KERNEL,
					aw8624,
					aw8624_rtp_loaded);
}

static int aw8624_haptic_juge_RTP_is_going_on(struct aw8624 *aw8624)
{
	unsigned char rtp_state = 0;
	unsigned char mode = 0;
	unsigned char glb_st = 0;

	aw8624_i2c_read(aw8624, AW8624_REG_SYSCTRL, &mode);
	aw8624_i2c_read(aw8624, AW8624_REG_GLB_STATE, &glb_st);
	if ((mode & AW8624_BIT_SYSCTRL_PLAY_MODE_RTP) &&
		(glb_st == AW8624_BIT_GLBRD5_STATE_RTP_GO)) {
		rtp_state = 1;
	}
	return rtp_state;
}

static void aw8624_haptic_raminit(struct aw8624 *aw8624, bool flag)
{
	if (flag) {
		aw8624_i2c_write_bits(aw8624, AW8624_REG_SYSCTRL,
				       AW8624_BIT_SYSCTRL_RAMINIT_MASK,
				       AW8624_BIT_SYSCTRL_RAMINIT_EN);
	} else {
		aw8624_i2c_write_bits(aw8624, AW8624_REG_SYSCTRL,
				       AW8624_BIT_SYSCTRL_RAMINIT_MASK,
				       AW8624_BIT_SYSCTRL_RAMINIT_OFF);
	}
}

static int aw8624_container_update(struct aw8624 *aw8624,
		struct aw8624_container *aw8624_cont)
{
	unsigned int shift = 0;
	int i = 0;
	int ret = 0;
#ifdef AW_CHECK_RAM_DATA
	unsigned short check_sum = 0;
	unsigned char reg_val = 0;
#endif
	mutex_lock(&aw8624->lock);

	aw8624->ram.baseaddr_shift = 2;
	aw8624->ram.ram_shift = 4;

	/* RAMINIT Enable */
	aw8624_haptic_raminit(aw8624, true);

	/* base addr */
	shift = aw8624->ram.baseaddr_shift;
	aw8624->ram.base_addr = (unsigned int)((aw8624_cont->data[0+shift]<<8) |
		(aw8624_cont->data[1+shift]));
	aw_dev_info(aw8624->dev,
		"%s: base_addr=0x%4x\n", __func__,
		aw8624->ram.base_addr);

	aw8624_i2c_write(aw8624,
			AW8624_REG_BASE_ADDRH,
			aw8624_cont->data[0+shift]);
	aw8624_i2c_write(aw8624,
			AW8624_REG_BASE_ADDRL,
			aw8624_cont->data[1+shift]);

	aw8624_i2c_write(aw8624,
			AW8624_REG_FIFO_AEH,
			(unsigned char)((aw8624->ram.base_addr>>2)>>8));
	aw8624_i2c_write(aw8624,
			AW8624_REG_FIFO_AEL,
			(unsigned char)((aw8624->ram.base_addr>>2)&0x00FF));
	aw8624_i2c_write(aw8624,
			AW8624_REG_FIFO_AFH,
			(unsigned char)((aw8624->ram.base_addr
			- (aw8624->ram.base_addr>>2))>>8));
	aw8624_i2c_write(aw8624,
			AW8624_REG_FIFO_AFL,
			(unsigned char)((aw8624->ram.base_addr
			-(aw8624->ram.base_addr>>2))&0x00FF));

	/* ram */
	shift = aw8624->ram.baseaddr_shift;
	aw8624_i2c_write(aw8624,
			AW8624_REG_RAMADDRH, aw8624_cont->data[0+shift]);
	aw8624_i2c_write(aw8624,
			AW8624_REG_RAMADDRL, aw8624_cont->data[1+shift]);
	shift = aw8624->ram.ram_shift;
	for (i = shift; i < aw8624_cont->len; i++) {
		aw8624_i2c_write(aw8624,
				AW8624_REG_RAMDATA, aw8624_cont->data[i]);
	}

#ifdef	AW_CHECK_RAM_DATA
	shift = aw8624->ram.baseaddr_shift;
	aw8624_i2c_write_bits(aw8624, AW8624_REG_RAMADDRH,
			       AW8624_BIT_RAMADDRH_MASK,
			       aw8624_cont->data[0 + shift]);
	aw8624_i2c_write(aw8624, AW8624_REG_RAMADDRL,
			  aw8624_cont->data[1 + shift]);
	shift = aw8624->ram.ram_shift;
	for (i = shift; i < aw8624_cont->len; i++) {
		aw8624_i2c_read(aw8624, AW8624_REG_RAMDATA, &reg_val);
		/*
		*aw_dev_info(aw8624->dev,
		*	"%s aw8624_cont->data=0x%02X, ramdata=0x%02X\n",
		*	__func__, aw8624_cont->data[i], reg_val);
		*/
		if (reg_val != aw8624_cont->data[i]) {
			aw_dev_err(aw8624->dev,
				"%s: ram check error addr=0x%04x, file_data=0x%02X, ram_data=0x%02X\n",
				__func__, i, aw8624_cont->data[i], reg_val);
			ret = -ERANGE;
			break;
		}
		check_sum += reg_val;
	}
	if (!ret) {
		aw8624_i2c_read(aw8624, AW8624_REG_BASE_ADDRH, &reg_val);
		check_sum += reg_val;
		aw8624_i2c_read(aw8624, AW8624_REG_BASE_ADDRL, &reg_val);
		check_sum += reg_val;

		if (check_sum != aw8624->ram.check_sum) {
			aw_dev_err(aw8624->dev, "%s: ram data check sum error, check_sum=0x%04x\n",
				__func__, check_sum);
			ret = -ERANGE;
		} else {
			aw_dev_info(aw8624->dev, "%s: ram data check sum pass, check_sum=0x%04x\n",
				 __func__, check_sum);
		}
	}

#endif
	/* RAMINIT Disable */
	aw8624_haptic_raminit(aw8624, false);

	mutex_unlock(&aw8624->lock);
	aw_dev_info(aw8624->dev, "%s exit\n", __func__);
	return ret;
}

static int aw8624_haptic_get_ram_number(struct aw8624 *aw8624)
{
	unsigned char i = 0;
	unsigned char reg_val = 0;
	unsigned char ram_data[3];
	unsigned int first_wave_addr = 0;

	aw_dev_info(aw8624->dev, "%s enter!\n", __func__);
	if (!aw8624->ram_init) {
		aw_dev_err(aw8624->dev,
			   "%s: ram init faild, ram_num = 0!\n",
			   __func__);
		return -EPERM;
	}

	mutex_lock(&aw8624->lock);
	/* RAMINIT Enable */
	aw8624_haptic_raminit(aw8624, true);
	aw8624_haptic_stop(aw8624);
	aw8624_i2c_write(aw8624, AW8624_REG_RAMADDRH,
			  (unsigned char)(aw8624->ram.base_addr >> 8));
	aw8624_i2c_write(aw8624, AW8624_REG_RAMADDRL,
			  (unsigned char)(aw8624->ram.base_addr & 0x00ff));
	for (i = 0; i < 3; i++) {
		aw8624_i2c_read(aw8624, AW8624_REG_RAMDATA, &reg_val);
		ram_data[i] = reg_val;
	}
	first_wave_addr = (ram_data[1] << 8 | ram_data[2]);
	aw8624->ram.ram_num =
			(first_wave_addr - aw8624->ram.base_addr - 1) / 4;
	aw_dev_info(aw8624->dev,
		    "%s: ram_version = 0x%02x\n", __func__, ram_data[0]);
	aw_dev_info(aw8624->dev,
		    "%s: first waveform addr = 0x%04x\n",
		    __func__, first_wave_addr);
	aw_dev_info(aw8624->dev,
		    "%s: ram_num = %d\n", __func__, aw8624->ram.ram_num);
	/* RAMINIT Disable */
	aw8624_haptic_raminit(aw8624, false);
	mutex_unlock(&aw8624->lock);

	return 0;
}

static void aw8624_ram_loaded(const struct firmware *cont, void *context)
{
	struct aw8624 *aw8624 = context;
	struct aw8624_container *aw8624_fw;
	unsigned short check_sum = 0;
	int i = 0;
	int ret = 0;
#ifdef AW_READ_BIN_FLEXBALLY
	static unsigned char load_cont;
	int ram_timer_val = 1000;

	load_cont++;
#endif
	aw_dev_info(aw8624->dev, "%s enter\n", __func__);
	if (!cont) {
		aw_dev_err(aw8624->dev,
			"%s: failed to read %s\n",
			__func__, aw8624_ram_name);
		release_firmware(cont);
#ifdef AW_READ_BIN_FLEXBALLY
		if (load_cont <= 20) {
			schedule_delayed_work(&aw8624->ram_work,
					msecs_to_jiffies(ram_timer_val));
			aw_dev_info(aw8624->dev, "%s:start hrtimer: load_cont=%d\n",
					__func__, load_cont);
		}
#endif
		return;
	}

	aw_dev_info(aw8624->dev,
		"%s: loaded %s - size: %zu\n", __func__, aw8624_ram_name,
		cont ? cont->size : 0);

	/* check sum */
	for (i = 2; i < cont->size; i++)
		check_sum += cont->data[i];

	if (check_sum == (unsigned short)((cont->data[0]<<8)|(cont->data[1]))) {
		aw_dev_info(aw8624->dev,
			"%s: check sum pass : 0x%04x\n", __func__, check_sum);
		aw8624->ram.check_sum = check_sum;
	} else {
		aw_dev_err(aw8624->dev, "%s: check sum err: check_sum=0x%04x\n",
		__func__, check_sum);
		return;
	}

	/* aw8624 ram update */
	aw8624_fw = kzalloc(cont->size+sizeof(int), GFP_KERNEL);
	if (!aw8624_fw) {
		release_firmware(cont);
		aw_dev_err(aw8624->dev,
			"%s: Error allocating memory\n", __func__);
		return;
	}
	aw8624_fw->len = cont->size;
	memcpy(aw8624_fw->data, cont->data, cont->size);
	release_firmware(cont);

	ret = aw8624_container_update(aw8624, aw8624_fw);
	if (ret) {
		kfree(aw8624_fw);
		aw8624->ram.len = 0;
		aw_dev_err(aw8624->dev, "%s: ram firmware update failed!\n",
			__func__);
	} else {
		aw8624->ram_init = 1;
		aw8624->ram.len = aw8624_fw->len;
		kfree(aw8624_fw);
		aw_dev_info(aw8624->dev,
			"%s: ram firmware update complete\n", __func__);
	}
	aw8624_haptic_get_ram_number(aw8624);
	if (aw8624->IsUsedIRQ)
		aw8624_rtp_update(aw8624);

}

static int aw8624_ram_update(struct aw8624 *aw8624)
{
	aw8624->ram_init = 0;
	aw8624->rtp_init = 0;
	return request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
				aw8624_ram_name, aw8624->dev, GFP_KERNEL,
				aw8624, aw8624_ram_loaded);
}

static void aw8624_ram_work_routine(struct work_struct *work)
{
	struct aw8624 *aw8624 =
		container_of(work, struct aw8624, ram_work.work);

	aw8624_ram_update(aw8624);

}

int aw8624_ram_init(struct aw8624 *aw8624)
{
	int ram_timer_val = 8000;

	INIT_DELAYED_WORK(&aw8624->ram_work, aw8624_ram_work_routine);
	schedule_delayed_work(&aw8624->ram_work,
				msecs_to_jiffies(ram_timer_val));

	return 0;
}

/*****************************************************
 *
 * haptic control
 *
 *****************************************************/

static int aw8624_haptic_play_init(struct aw8624 *aw8624)
{
	if (aw8624->play_mode == AW8624_HAPTIC_CONT_MODE) {
		aw8624_i2c_write(aw8624,
			AW8624_REG_SW_BRAKE,
			(unsigned char)(aw8624_dts_data.aw8624_sw_brake[0]));
	} else {
		aw8624_i2c_write(aw8624,
			AW8624_REG_SW_BRAKE,
			(unsigned char)(aw8624_dts_data.aw8624_sw_brake[1]));
	}
	return 0;
}

static int aw8624_haptic_active(struct aw8624 *aw8624)
{
	aw8624_haptic_play_init(aw8624);
	aw8624_i2c_write_bits(aw8624,
				AW8624_REG_SYSCTRL,
				AW8624_BIT_SYSCTRL_WORK_MODE_MASK,
				AW8624_BIT_SYSCTRL_ACTIVE);
	aw8624_interrupt_clear(aw8624);
	aw8624_i2c_write_bits(aw8624,
				AW8624_REG_SYSINTM,
				AW8624_BIT_SYSINTM_UVLO_MASK,
				AW8624_BIT_SYSINTM_UVLO_EN);
	return 0;
}

static int aw8624_haptic_play_mode(struct aw8624 *aw8624,
							unsigned char play_mode)
{
	switch (play_mode) {
	case AW8624_HAPTIC_STANDBY_MODE:
		aw_dev_info(aw8624->dev, "%s: enter standby mode\n", __func__);
		aw8624->play_mode = AW8624_HAPTIC_STANDBY_MODE;
		aw8624_i2c_write_bits(aw8624, AW8624_REG_SYSINTM,
				AW8624_BIT_SYSINTM_UVLO_MASK,
				AW8624_BIT_SYSINTM_UVLO_OFF);
		aw8624_i2c_write_bits(aw8624,
				AW8624_REG_SYSCTRL,
				AW8624_BIT_SYSCTRL_WORK_MODE_MASK,
				AW8624_BIT_SYSCTRL_STANDBY);
		break;
	case AW8624_HAPTIC_RAM_MODE:
		aw_dev_info(aw8624->dev, "%s: enter ram mode\n", __func__);
		aw8624->play_mode = AW8624_HAPTIC_RAM_MODE;
		aw8624_i2c_write_bits(aw8624,
				AW8624_REG_SYSCTRL,
				AW8624_BIT_SYSCTRL_PLAY_MODE_MASK,
				AW8624_BIT_SYSCTRL_PLAY_MODE_RAM);
		aw8624_haptic_active(aw8624);
		break;
	case AW8624_HAPTIC_RAM_LOOP_MODE:
		aw_dev_info(aw8624->dev, "%s: enter ram loop mode\n", __func__);
		aw8624->play_mode = AW8624_HAPTIC_RAM_LOOP_MODE;
		aw8624_i2c_write_bits(aw8624,
				AW8624_REG_SYSCTRL,
				AW8624_BIT_SYSCTRL_PLAY_MODE_MASK,
				AW8624_BIT_SYSCTRL_PLAY_MODE_RAM);
		aw8624_haptic_active(aw8624);
		break;
	case AW8624_HAPTIC_RTP_MODE:
		aw_dev_info(aw8624->dev, "%s: enter rtp mode\n", __func__);
		aw8624->play_mode = AW8624_HAPTIC_RTP_MODE;
		aw8624_i2c_write_bits(aw8624,
				AW8624_REG_SYSCTRL,
				AW8624_BIT_SYSCTRL_PLAY_MODE_MASK,
				AW8624_BIT_SYSCTRL_PLAY_MODE_RTP);
		aw8624_haptic_active(aw8624);
		break;
	case AW8624_HAPTIC_TRIG_MODE:
		aw_dev_info(aw8624->dev, "%s: enter trig mode\n", __func__);
		aw8624->play_mode = AW8624_HAPTIC_TRIG_MODE;
		aw8624_i2c_write_bits(aw8624,
				AW8624_REG_SYSCTRL,
				AW8624_BIT_SYSCTRL_PLAY_MODE_MASK,
				AW8624_BIT_SYSCTRL_PLAY_MODE_RAM);
		aw8624_haptic_active(aw8624);
		break;
	case AW8624_HAPTIC_CONT_MODE:
		aw_dev_info(aw8624->dev, "%s: enter cont mode\n", __func__);
		aw8624->play_mode = AW8624_HAPTIC_CONT_MODE;
		aw8624_i2c_write_bits(aw8624,
				AW8624_REG_SYSCTRL,
				AW8624_BIT_SYSCTRL_PLAY_MODE_MASK,
				AW8624_BIT_SYSCTRL_PLAY_MODE_CONT);
		aw8624_haptic_active(aw8624);
		break;
	default:
		dev_err(aw8624->dev, "%s: play mode %d err",
				__func__, play_mode);
		break;
	}
	return 0;
}

static int aw8624_haptic_play_go(struct aw8624 *aw8624, bool flag)
{
	aw_dev_dbg(aw8624->dev, "%s enter, flag = %d\n", __func__, flag);
	if (!flag) {
#ifdef KERNEL_VERSION_49
		do_gettimeofday(&aw8624->current_time);
		aw8624->interval_us = (aw8624->current_time.tv_sec -
		aw8624->pre_enter_time.tv_sec) * 1000000 +
		(aw8624->current_time.tv_usec-aw8624->pre_enter_time.tv_usec);
#else

		aw8624->current_time = ktime_get();
		aw8624->interval_us = ktime_to_us(ktime_sub(aw8624->current_time,
						aw8624->pre_enter_time));
#endif


		if (aw8624->interval_us < 2000) {
			aw_dev_info(aw8624->dev, "%s:aw8624->interval_us=%d\n",
					__func__, aw8624->interval_us);
			mdelay(2);
		}
	}
	if (flag == true) {
		aw8624_i2c_write_bits(aw8624, AW8624_REG_GO,
			AW8624_BIT_GO_MASK, AW8624_BIT_GO_ENABLE);
#ifdef KERNEL_VERSION_49
		do_gettimeofday(&aw8624->pre_enter_time);
#else
		aw8624->pre_enter_time = ktime_get();
#endif

	} else {
		aw8624_i2c_write_bits(aw8624, AW8624_REG_GO,
			AW8624_BIT_GO_MASK, AW8624_BIT_GO_DISABLE);
	}
	return 0;
}

static int aw8624_haptic_stop_delay(struct aw8624 *aw8624)
{
	unsigned char reg_val = 0;
	unsigned int cnt = 100;

	while (cnt--) {
		aw8624_i2c_read(aw8624, AW8624_REG_GLB_STATE, &reg_val);
		if ((reg_val&0x0f) == 0x00) {
			aw_dev_info(aw8624->dev,
				"%s enter standby, reg glb_state=0x%02x\n",
				__func__, reg_val);
			return 0;
		}
		mdelay(2);

		aw_dev_info(aw8624->dev,
			"%s wait for standby, reg glb_state=0x%02x\n",
			__func__, reg_val);
	}
	aw_dev_err(aw8624->dev,
		"%s do not enter standby automatically\n", __func__);
	return 0;
}

static int aw8624_haptic_stop(struct aw8624 *aw8624)
{
	aw_dev_info(aw8624->dev, "%s enter\n", __func__);
	aw8624_haptic_play_go(aw8624, false);
	aw8624_haptic_stop_delay(aw8624);
	aw8624_haptic_play_mode(aw8624, AW8624_HAPTIC_STANDBY_MODE);

	return 0;
}

static int aw8624_haptic_start(struct aw8624 *aw8624)
{
	aw8624_haptic_active(aw8624);
	aw8624_haptic_play_go(aw8624, true);

	return 0;
}

static int aw8624_haptic_set_wav_seq(struct aw8624 *aw8624,
		unsigned char wav, unsigned char seq)
{
	aw8624_i2c_write(aw8624, AW8624_REG_WAVSEQ1+wav, seq);
	return 0;
}

static int aw8624_haptic_set_wav_loop(struct aw8624 *aw8624,
		unsigned char wav, unsigned char loop)
{
	unsigned char tmp = 0;

	if (wav%2) {
		tmp = loop<<0;
		aw8624_i2c_write_bits(aw8624, AW8624_REG_WAVLOOP1+(wav/2),
			AW8624_BIT_WAVLOOP_SEQNP1_MASK, tmp);
	} else {
		tmp = loop<<4;
		aw8624_i2c_write_bits(aw8624, AW8624_REG_WAVLOOP1+(wav/2),
			AW8624_BIT_WAVLOOP_SEQN_MASK, tmp);
	}

	return 0;
}

static int
aw8624_haptic_set_repeat_wav_seq(struct aw8624 *aw8624, unsigned char seq)
{
	aw8624_haptic_set_wav_seq(aw8624, 0x00, seq);
	aw8624_haptic_set_wav_loop(aw8624,
				0x00,
				AW8624_BIT_WAVLOOP_INIFINITELY);

	return 0;
}

static int aw8624_haptic_set_gain(struct aw8624 *aw8624, unsigned char gain)
{
	aw8624_i2c_write(aw8624, AW8624_REG_DATDBG, gain);
	return 0;
}

static int aw8624_haptic_set_pwm(struct aw8624 *aw8624, unsigned char mode)
{
	aw8624->pwm_mode = mode;

	switch (mode) {
	case AW8624_PWM_48K:
		aw8624_i2c_write_bits(aw8624,
				AW8624_REG_PWMDBG,
				AW8624_BIT_PWMDBG_PWM_MODE_MASK,
				AW8624_BIT_PWMDBG_PWM_48K);
		break;
	case AW8624_PWM_24K:
		aw8624_i2c_write_bits(aw8624,
				AW8624_REG_PWMDBG,
				AW8624_BIT_PWMDBG_PWM_MODE_MASK,
				AW8624_BIT_PWMDBG_PWM_24K);
		break;
	case AW8624_PWM_12K:
		aw8624_i2c_write_bits(aw8624,
				AW8624_REG_PWMDBG,
				AW8624_BIT_PWMDBG_PWM_MODE_MASK,
				AW8624_BIT_PWMDBG_PWM_12K);
		break;
	default:
		break;
	}
	return 0;
}

static int
aw8624_haptic_play_repeat_seq(struct aw8624 *aw8624, unsigned char flag)
{
	if (flag) {
		aw8624_haptic_play_mode(aw8624, AW8624_HAPTIC_RAM_LOOP_MODE);
		aw8624_haptic_start(aw8624);
	}

	return 0;
}

static int aw8624_haptic_play_wav_seq(struct aw8624 *aw8624,
				       unsigned char flag)
{
	aw_dev_info(aw8624->dev, "%s enter\n", __func__);
	if (flag) {
		aw8624_haptic_play_mode(aw8624, AW8624_HAPTIC_RAM_MODE);
		aw8624_haptic_start(aw8624);
	}
	return 0;
}


static int aw8624_haptic_swicth_motorprotect_config(struct aw8624 *aw8624,
		unsigned char addr, unsigned char val)
{
	if (addr == 1) {
		aw8624_i2c_write_bits(aw8624,
				AW8624_REG_DETCTRL,
				AW8624_BIT_DETCTRL_PROTECT_MASK,
				AW8624_BIT_DETCTRL_PROTECT_SHUTDOWN);
		aw8624_i2c_write_bits(aw8624,
				AW8624_REG_PWMPRC,
				AW8624_BIT_PWMPRC_PRC_EN_MASK,
				AW8624_BIT_PWMPRC_PRC_ENABLE);
		aw8624_i2c_write_bits(aw8624,
				AW8624_REG_PRLVL,
				AW8624_BIT_PRLVL_PR_EN_MASK,
				AW8624_BIT_PRLVL_PR_ENABLE);
	} else if (addr == 0) {
		aw8624_i2c_write_bits(aw8624,
				AW8624_REG_DETCTRL,
				AW8624_BIT_DETCTRL_PROTECT_MASK,
				AW8624_BIT_DETCTRL_PROTECT_NO_ACTION);
		aw8624_i2c_write_bits(aw8624,
				AW8624_REG_PWMPRC,
				AW8624_BIT_PWMPRC_PRC_EN_MASK,
				AW8624_BIT_PWMPRC_PRC_DISABLE);
		aw8624_i2c_write_bits(aw8624,
				AW8624_REG_PRLVL,
				AW8624_BIT_PRLVL_PR_EN_MASK,
				AW8624_BIT_PRLVL_PR_DISABLE);
	} else if (addr == 0x2d) {
		aw8624_i2c_write_bits(aw8624, AW8624_REG_PWMPRC,
			AW8624_BIT_PWMPRC_PRCTIME_MASK, val);
	} else if (addr == 0x3e) {
		aw8624_i2c_write_bits(aw8624, AW8624_REG_PRLVL,
			AW8624_BIT_PRLVL_PRLVL_MASK, val);
	} else if (addr == 0x3f) {
		aw8624_i2c_write_bits(aw8624, AW8624_REG_PRTIME,
			AW8624_BIT_PRTIME_PRTIME_MASK, val);
	} else {
		 /*nothing to do;*/
	}

	 return 0;
}

static int aw8624_haptic_ram_config(struct aw8624 *aw8624, int duration)
{
	unsigned char wavseq = 0;
	unsigned char wavloop = 0;
	int ret = 0;

	if (aw8624->duration_time_flag < 0) {
		aw_dev_err(aw8624->dev,
			"%s: duration time error, array size = %d\n",
			__func__, aw8624->duration_time_size);
		return -ERANGE;
	}
	ret = aw8624_analyse_duration_range(aw8624);
	if (ret < 0)
		return ret;
	if ((duration > 0) && (duration <
				aw8624_dts_data.aw8624_duration_time[0])) {
		wavseq = 3;	/*3*/
		wavloop = 0;
	} else if ((duration >= aw8624_dts_data.aw8624_duration_time[0]) &&
		(duration < aw8624_dts_data.aw8624_duration_time[1])) {
		wavseq = 2;	/*2*/
		wavloop = 0;
	} else if ((duration >= aw8624_dts_data.aw8624_duration_time[1]) &&
		(duration < aw8624_dts_data.aw8624_duration_time[2])) {
		wavseq = 1;	/*1*/
		wavloop = 0;
	} else if (duration >= aw8624_dts_data.aw8624_duration_time[2]) {
		wavseq = 4;	/*4*/
		wavloop = 15;	/*long vibration*/
	} else {
		wavseq = 0;
		wavloop = 0;
	}

	aw8624_haptic_set_wav_seq(aw8624, 0, wavseq);
	aw8624_haptic_set_wav_loop(aw8624, 0, wavloop);
	aw8624_haptic_set_wav_seq(aw8624, 1, 0);
	aw8624_haptic_set_wav_loop(aw8624, 1, 0);

	return 0;
}

static int aw8624_haptic_select_pin(struct aw8624 *aw8624, unsigned char pin)
{
	if (pin == TRIG1) {
		aw8624_i2c_write_bits(aw8624,
				AW8624_REG_DBGCTRL,
				AW8624_BIT_DBGCTRL_INTN_TRG_SEL_MASK,
				AW8624_BIT_DBGCTRL_TRG_SEL_ENABLE);
		aw8624_i2c_write_bits(aw8624,
				AW8624_REG_TRG_CFG2,
				AW8624_BIT_TRGCFG2_TRG1_ENABLE_MASK,
				AW8624_BIT_TRGCFG2_TRG1_ENABLE);
		aw_dev_info(aw8624->dev, "%s: select TRIG1 pin\n", __func__);
	} else if (pin == IRQ) {
		aw8624_i2c_write_bits(aw8624,
				AW8624_REG_DBGCTRL,
				AW8624_BIT_DBGCTRL_INTN_TRG_SEL_MASK,
				AW8624_BIT_DBGCTRL_INTN_SEL_ENABLE);
		aw8624_i2c_write_bits(aw8624,
				AW8624_REG_TRG_CFG2,
				AW8624_BIT_TRGCFG2_TRG1_ENABLE_MASK,
				AW8624_BIT_TRGCFG2_TRG1_DISABLE);
		aw_dev_info(aw8624->dev, "%s: select INIT pin\n", __func__);
	} else
		aw_dev_err(aw8624->dev, "%s: There is no such option\n",
			__func__);
	return 0;
}
static int aw8624_haptic_trig1_param_init(struct aw8624 *aw8624)
{
	if (aw8624->IsUsedIRQ) {
		aw8624_haptic_select_pin(aw8624, IRQ);
		return 0;
	}
	aw8624->trig.trig_enable = aw8624_dts_data.trig_config[0];
	aw8624->trig.trig_edge = aw8624_dts_data.trig_config[1];
	aw8624->trig.trig_polar = aw8624_dts_data.trig_config[2];
	aw8624->trig.pos_sequence = aw8624_dts_data.trig_config[3];
	aw8624->trig.neg_sequence = aw8624_dts_data.trig_config[4];
	aw_dev_info(aw8624->dev, "%s: trig1 date init ok!\n", __func__);
	return 0;
}
static int aw8624_haptic_tirg1_param_config(struct aw8624 *aw8624)
{
	if (aw8624->IsUsedIRQ) {
		aw8624_haptic_select_pin(aw8624, IRQ);
		return 0;
	}
	if (aw8624->trig.trig_enable)
		aw8624_haptic_select_pin(aw8624, TRIG1);
	else
		aw8624_haptic_select_pin(aw8624, IRQ);

	aw8624_i2c_write_bits(aw8624, AW8624_REG_TRG_CFG1,
			AW8624_BIT_TRGCFG1_TRG1_EDGE_MASK,
			aw8624->trig.trig_edge);
	aw8624_i2c_write_bits(aw8624, AW8624_REG_TRG_CFG1,
			AW8624_BIT_TRGCFG1_TRG1_POLAR_MASK,
			aw8624->trig.trig_polar << 1);
	aw8624_i2c_write(aw8624, AW8624_REG_TRG1_SEQP,
			aw8624->trig.pos_sequence);
	aw8624_i2c_write(aw8624, AW8624_REG_TRG1_SEQN,
			aw8624->trig.neg_sequence);
	return 0;
}
static int aw8624_haptic_vbat_mode(struct aw8624 *aw8624, unsigned char flag)
{
	if (flag == AW8624_HAPTIC_VBAT_HW_COMP_MODE) {
		aw8624_i2c_write_bits(aw8624,
				AW8624_REG_ADCTEST,
				AW8624_BIT_DETCTRL_VBAT_MODE_MASK,
				AW8624_BIT_DETCTRL_VBAT_HW_COMP);
	} else {
		aw8624_i2c_write_bits(aw8624,
				AW8624_REG_ADCTEST,
				AW8624_BIT_DETCTRL_VBAT_MODE_MASK,
				AW8624_BIT_DETCTRL_VBAT_SW_COMP);
	}
	return 0;
}

static int aw8624_haptic_set_f0_preset(struct aw8624 *aw8624)
{
	unsigned int f0_reg = 0;

	f0_reg = 1000000000/(aw8624->f0_pre*aw8624_dts_data.aw8624_f0_coeff);
	aw8624_i2c_write(aw8624,
			AW8624_REG_F_PRE_H,
			(unsigned char)((f0_reg>>8)&0xff));
	aw8624_i2c_write(aw8624,
			AW8624_REG_F_PRE_L,
			(unsigned char)((f0_reg>>0)&0xff));

	return 0;
}

static int aw8624_haptic_read_f0(struct aw8624 *aw8624)
{
	int ret = 0;
	unsigned char reg_val = 0;
	unsigned int f0_reg = 0;
	unsigned long f0_tmp = 0;

	ret = aw8624_i2c_read(aw8624, AW8624_REG_F_LRA_F0_H, &reg_val);
	f0_reg = (reg_val<<8);
	ret = aw8624_i2c_read(aw8624, AW8624_REG_F_LRA_F0_L, &reg_val);
	f0_reg |= (reg_val<<0);
	if (!f0_reg || !aw8624_dts_data.aw8624_f0_coeff) {
		aw8624->f0 = 0;
		aw_dev_info(aw8624->dev, "%s : get f0 failed with the value becoming 0!\n",
								__func__);
		return -EPERM;
	}

	f0_tmp = 1000000000 / (f0_reg * aw8624_dts_data.aw8624_f0_coeff);
	aw8624->f0 = (unsigned int)f0_tmp;
	aw_dev_info(aw8624->dev, "%s f0=%d\n", __func__, aw8624->f0);
	return 0;
}

static int aw8624_haptic_read_cont_f0(struct aw8624 *aw8624)
{
	int ret = 0;
	unsigned char reg_val = 0;
	unsigned int f0_reg = 0;
	unsigned long f0_tmp = 0;

	ret = aw8624_i2c_read(aw8624, AW8624_REG_F_LRA_CONT_H, &reg_val);
	f0_reg = (reg_val<<8);
	ret = aw8624_i2c_read(aw8624, AW8624_REG_F_LRA_CONT_L, &reg_val);
	f0_reg |= (reg_val<<0);
	if (!f0_reg) {
		aw8624->cont_f0 = 0;
		aw_dev_info(aw8624->dev,
			"%s: failed to reading cont f0 with 0\n", __func__);
		return 0;
	}

	f0_tmp = 1000000000/(f0_reg*aw8624_dts_data.aw8624_f0_coeff);
	aw8624->cont_f0 = (unsigned int)f0_tmp;
	aw_dev_info(aw8624->dev, "%s cont_f0=%d\n", __func__, aw8624->cont_f0);
	return 0;
}

static int aw8624_haptic_read_beme(struct aw8624 *aw8624)
{
	int ret = 0;
	unsigned char reg_val = 0;

	ret = aw8624_i2c_read(aw8624, AW8624_REG_WAIT_VOL_MP, &reg_val);
	aw8624->max_pos_beme = (reg_val<<0);
	ret = aw8624_i2c_read(aw8624, AW8624_REG_WAIT_VOL_MN, &reg_val);
	aw8624->max_neg_beme = (reg_val<<0);

	aw_dev_info(aw8624->dev,
		"%s max_pos_beme=%d\n", __func__, aw8624->max_pos_beme);
	aw_dev_info(aw8624->dev,
		"%s max_neg_beme=%d\n", __func__, aw8624->max_neg_beme);

	return 0;
}

static int aw8624_vbat_monitor_detector(struct aw8624 *aw8624)
{
	unsigned char reg_val = 0;
	unsigned int vbat = 0;

	aw8624_haptic_stop(aw8624);
	/*step 1:EN_RAMINIT*/
	aw8624_haptic_raminit(aw8624, true);

	/*step 2 :launch power supply testing */
	aw8624_i2c_write_bits(aw8624,
				AW8624_REG_DETCTRL,
				AW8624_BIT_DETCTRL_VBAT_GO_MASK,
				AW8624_BIT_DETCTRL_VABT_GO_ENABLE);
	usleep_range(2000, 2500);

	aw8624_i2c_read(aw8624, AW8624_REG_VBATDET, &reg_val);
	vbat = 6100 * reg_val / 256;
	aw_dev_info(aw8624->dev, "%s get_vbat=%dmV\n",
		    __func__, vbat);
	/*step 3: return val*/
	aw8624_haptic_raminit(aw8624, false);

	return vbat;
}

static int aw8624_lra_resistance_detector(struct aw8624 *aw8624)
{
	unsigned char reg_val = 0;
	unsigned char reg_val_anactrl = 0;
	unsigned char reg_val_d2scfg = 0;
	unsigned int r_lra = 0;

	mutex_lock(&aw8624->lock);
	aw8624_i2c_read(aw8624, AW8624_REG_ANACTRL, &reg_val_anactrl);
	aw8624_i2c_read(aw8624, AW8624_REG_D2SCFG, &reg_val_d2scfg);
	aw8624_haptic_stop(aw8624);
	aw8624_haptic_raminit(aw8624, true);


	aw8624_i2c_write_bits(aw8624,
				AW8624_REG_ANACTRL,
				AW8624_BIT_ANACTRL_EN_IO_PD1_MASK,
				AW8624_BIT_ANACTRL_EN_IO_PD1_HIGH);

	aw8624_i2c_write_bits(aw8624,
				AW8624_REG_D2SCFG,
				AW8624_BIT_D2SCFG_CLK_ADC_MASK,
				AW8624_BIT_D2SCFG_CLK_ASC_1P5MHZ);

	aw8624_i2c_write_bits(aw8624,
				AW8624_REG_DETCTRL,
				AW8624_BIT_DETCTRL_RL_OS_MASK,
				AW8624_BIT_DETCTRL_RL_DETECT);
	aw8624_i2c_write_bits(aw8624,
				AW8624_REG_DETCTRL,
				AW8624_BIT_DETCTRL_DIAG_GO_MASK,
				AW8624_BIT_DETCTRL_DIAG_GO_ENABLE);
	usleep_range(3000, 3500);
	aw8624_i2c_read(aw8624, AW8624_REG_RLDET, &reg_val);
	r_lra = 298 * reg_val;
	/*len += snprintf(buf+len, PAGE_SIZE-len, "r_lra=%dmohm\n", r_lra);*/

	aw8624_i2c_write(aw8624, AW8624_REG_D2SCFG, reg_val_d2scfg);
	aw8624_i2c_write(aw8624, AW8624_REG_ANACTRL, reg_val_anactrl);
	aw8624_haptic_raminit(aw8624, false);
	mutex_unlock(&aw8624->lock);

	return r_lra;
}

static int aw8624_haptic_ram_vbat_comp(struct aw8624 *aw8624, bool flag)
{
	int temp_gain = 0;
	int vbat = 0;

	if (flag) {
		if (aw8624->ram_vbat_comp ==
		AW8624_HAPTIC_RAM_VBAT_COMP_ENABLE) {
			vbat = aw8624_vbat_monitor_detector(aw8624);
			temp_gain = aw8624->gain * AW8624_VBAT_REFER / vbat;
			if
			(temp_gain > (128*AW8624_VBAT_REFER/AW8624_VBAT_MIN)) {
				temp_gain =
					128*AW8624_VBAT_REFER/AW8624_VBAT_MIN;
				aw_dev_dbg(aw8624->dev, "%s gain limit=%d\n",
					__func__, temp_gain);
			}
			aw8624_haptic_set_gain(aw8624, temp_gain);
		} else {
			aw8624_haptic_set_gain(aw8624, aw8624->gain);
		}
	} else {
		aw8624_haptic_set_gain(aw8624, aw8624->gain);
	}

	return 0;
}

void aw8624_haptic_set_rtp_aei(struct aw8624 *aw8624, bool flag)
{
	if (flag) {
		aw8624_i2c_write_bits(aw8624,
					AW8624_REG_SYSINTM,
					AW8624_BIT_SYSINTM_FF_AE_MASK,
					AW8624_BIT_SYSINTM_FF_AE_EN);
	} else {
		aw8624_i2c_write_bits(aw8624,
					AW8624_REG_SYSINTM,
					AW8624_BIT_SYSINTM_FF_AE_MASK,
					AW8624_BIT_SYSINTM_FF_AE_OFF);
	}
}

static unsigned char aw8624_haptic_rtp_get_fifo_afi(struct aw8624 *aw8624)
{
	unsigned char ret = 0;
	unsigned char reg_val = 0;

	if (aw8624->osc_cali_flag == 1) {
		aw8624_i2c_read(aw8624, AW8624_REG_SYSST, &reg_val);
		reg_val &= AW8624_BIT_SYSST_FF_AFS;
		ret = reg_val >> 3;
	} else {
		aw8624_i2c_read(aw8624, AW8624_REG_SYSINT, &reg_val);
		reg_val &= AW8624_BIT_SYSINT_FF_AFI;
		ret = reg_val >> 3;
	}
	return ret;
}

unsigned char aw8624_haptic_rtp_get_fifo_afs(struct aw8624 *aw8624)
{
	unsigned char ret = 0;
	unsigned char reg_val = 0;

	aw8624_i2c_read(aw8624, AW8624_REG_SYSST, &reg_val);
	reg_val &= AW8624_BIT_SYSST_FF_AFS;
	ret = reg_val >> 3;

	return ret;
}

static int aw8624_haptic_rtp_init(struct aw8624 *aw8624)
{
	unsigned int buf_len = 0;
	unsigned char glb_st = 0;

	aw_dev_info(aw8624->dev, "%s enter\n", __func__);
	aw8624->rtp_cnt = 0;
	mutex_lock(&aw8624->rtp_lock);
	while ((!aw8624_haptic_rtp_get_fifo_afs(aw8624)) &&
	       (aw8624->play_mode == AW8624_HAPTIC_RTP_MODE)) {
		aw_dev_info(aw8624->dev,
			"%s rtp cnt = %d\n", __func__, aw8624->rtp_cnt);
		if (!aw8624->rtp_container) {
			aw_dev_info(aw8624->dev,
				"%s:aw8624_rtp is null break\n", __func__);
			break;
		}

		if (aw8624->rtp_cnt < aw8624->ram.base_addr) {
			if ((aw8624->rtp_container->len-aw8624->rtp_cnt) <
						(aw8624->ram.base_addr)) {
				buf_len = aw8624->rtp_container->len-aw8624->rtp_cnt;
			} else {
				buf_len = (aw8624->ram.base_addr);
			}
		} else if ((aw8624->rtp_container->len - aw8624->rtp_cnt) <
						(aw8624->ram.base_addr >> 2)) {
			buf_len = aw8624->rtp_container->len - aw8624->rtp_cnt;
		} else {
			buf_len = (aw8624->ram.base_addr >> 2);
		}
		aw_dev_info(aw8624->dev,
			"%s buf_len = %d\n", __func__, buf_len);
		aw8624_i2c_writes(aw8624, AW8624_REG_RTP_DATA,
				&aw8624->rtp_container->data[aw8624->rtp_cnt],
				buf_len);

		aw8624->rtp_cnt += buf_len;
		aw8624_i2c_read(aw8624, AW8624_REG_GLB_STATE, &glb_st);
		if (aw8624->rtp_cnt == aw8624->rtp_container->len ||
						((glb_st & 0x0f) == 0x00)) {
			if (aw8624->rtp_cnt == aw8624->rtp_container->len)
				aw_dev_info(aw8624->dev,
					"%s: rtp load completely! glb_st=%02x aw8624->rtp_cnt=%d\n",
					__func__, glb_st, aw8624->rtp_cnt);
			else
				aw_dev_err(aw8624->dev,
					"%s rtp load failed!! glb_st=%02x aw8624->rtp_cnt=%d\n",
					__func__, glb_st, aw8624->rtp_cnt);
			aw8624->rtp_cnt = 0;
			mutex_unlock(&aw8624->rtp_lock);
			return 0;
		}
	}

	if (aw8624->play_mode == AW8624_HAPTIC_RTP_MODE)
		aw8624_haptic_set_rtp_aei(aw8624, true);

	aw_dev_info(aw8624->dev, "%s exit\n", __func__);
	mutex_unlock(&aw8624->rtp_lock);
	return 0;
}

static void aw8624_rtp_work_routine(struct work_struct *work)
{
	const struct firmware *rtp_file;
	int ret = -1;
	unsigned char reg_val = 0;
	bool rtp_work_flag = false;
	unsigned int cnt = 200;

	struct aw8624 *aw8624 = container_of(work, struct aw8624, rtp_work);

	/* fw loaded */
	aw_dev_info(aw8624->dev, "%s enter\n", __func__);
	mutex_lock(&aw8624->rtp_lock);
	ret = request_firmware(&rtp_file,
	aw8624_rtp_name[aw8624->rtp_file_num], aw8624->dev);
	if (ret < 0) {
		aw_dev_err(aw8624->dev, "%s: failed to read %s\n", __func__,
		aw8624_rtp_name[aw8624->rtp_file_num]);
		mutex_unlock(&aw8624->rtp_lock);
		return;
	}
	aw8624->rtp_init = 0;
	vfree(aw8624->rtp_container);
	aw8624->rtp_container = vmalloc(rtp_file->size+sizeof(int));
	if (!aw8624->rtp_container) {
		release_firmware(rtp_file);
		aw_dev_err(aw8624->dev,
			"%s: error allocating memory\n", __func__);
		mutex_unlock(&aw8624->rtp_lock);
		return;
	}
	aw8624->rtp_container->len = rtp_file->size;
	aw_dev_info(aw8624->dev, "%s: rtp file [%s] size = %d\n", __func__,
	aw8624_rtp_name[aw8624->rtp_file_num], aw8624->rtp_container->len);
	memcpy(aw8624->rtp_container->data, rtp_file->data, rtp_file->size);
	mutex_unlock(&aw8624->rtp_lock);
	release_firmware(rtp_file);
	mutex_lock(&aw8624->lock);
	aw8624->rtp_init = 1;
	if (aw8624->IsUsedIRQ)
		aw8624_haptic_select_pin(aw8624, IRQ);
	aw8624_haptic_upload_lra(aw8624, AW8624_HAPTIC_RTP_CALI_LRA);
	/* rtp mode config */
	aw8624_haptic_play_mode(aw8624, AW8624_HAPTIC_RTP_MODE);

	/* haptic start */
	aw8624_haptic_start(aw8624);
	mutex_unlock(&aw8624->lock);
	usleep_range(2000, 2500);
	while (cnt) {
		aw8624_i2c_read(aw8624, AW8624_REG_GLB_STATE, &reg_val);
		if ((reg_val & 0x0f) == 0x08) {
			cnt = 0;
			rtp_work_flag = true;
			aw_dev_info(aw8624->dev, "%s RTP_GO! glb_state=0x08\n",
				    __func__);
		} else {
			cnt--;
			aw_dev_dbg(aw8624->dev, "%s wait for RTP_GO, glb_state=0x%02X\n",
				   __func__, reg_val);
		}
		usleep_range(2000, 2500);
	}
	if (rtp_work_flag) {
		aw8624_haptic_rtp_init(aw8624);
	} else {
		/* enter standby mode */
		aw8624_haptic_stop(aw8624);
		aw_dev_err(aw8624->dev, "%s failed to enter RTP_GO status!\n",
			   __func__);
	}

}

/*****************************************************
 *
 * haptic - audio
 *
 *****************************************************/
static enum hrtimer_restart
aw8624_haptic_audio_timer_func(struct hrtimer *timer)
{
	struct aw8624 *aw8624 =
	container_of(timer, struct aw8624, haptic_audio.timer);

	schedule_work(&aw8624->haptic_audio.work);
	hrtimer_start(&aw8624->haptic_audio.timer,
			ktime_set(aw8624->haptic_audio.timer_val/1000,
				(aw8624->haptic_audio.timer_val%1000)*1000000),
			HRTIMER_MODE_REL);
	return HRTIMER_NORESTART;
}

static void aw8624_haptic_audio_work_routine(struct work_struct *work)
{
	struct aw8624 *aw8624 =
	container_of(work, struct aw8624, haptic_audio.work);
	struct haptic_audio *haptic_audio = NULL;
	struct haptic_ctr *p_ctr = NULL;
	struct haptic_ctr *p_ctr_bak = NULL;
	unsigned int ctr_list_flag = 0;
	unsigned int ctr_list_input_cnt = 0;
	unsigned int ctr_list_output_cnt = 0;
	unsigned int ctr_list_diff_cnt = 0;
	unsigned int ctr_list_del_cnt = 0;
	int rtp_is_going_on = 0;

	aw_dev_info(aw8624->dev, "%s enter\n", __func__);
	haptic_audio = &(aw8624->haptic_audio);
	mutex_lock(&aw8624->haptic_audio.lock);
	memset(&aw8624->haptic_audio.ctr, 0, sizeof(struct haptic_ctr));
	ctr_list_flag = 0;
		list_for_each_entry_safe_reverse(p_ctr, p_ctr_bak,
				&(haptic_audio->ctr_list), list) {
		ctr_list_flag = 1;
		break;
	}
	if (ctr_list_flag == 0)
		aw_dev_info(aw8624->dev, "%s: ctr list empty\n", __func__);
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
				(AW8624_HAPTIC_CMD_ENABLE ==
					(AW8624_HAPTIC_CMD_HAPTIC & p_ctr->cmd))) {
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
		aw8624->haptic_audio.ctr.cnt = p_ctr->cnt;
		aw8624->haptic_audio.ctr.cmd = p_ctr->cmd;
		aw8624->haptic_audio.ctr.play = p_ctr->play;
		aw8624->haptic_audio.ctr.wavseq = p_ctr->wavseq;
		aw8624->haptic_audio.ctr.loop = p_ctr->loop;
		aw8624->haptic_audio.ctr.gain = p_ctr->gain;
		list_del(&p_ctr->list);
		kfree(p_ctr);
		break;
	}

	if (aw8624->haptic_audio.ctr.play) {
		aw_dev_info(aw8624->dev, "%s: cnt=%d, cmd=%d, play=%d, wavseq=%d, loop=%d, gain=%d\n",
			__func__,
			aw8624->haptic_audio.ctr.cnt,
			aw8624->haptic_audio.ctr.cmd,
			aw8624->haptic_audio.ctr.play,
			aw8624->haptic_audio.ctr.wavseq,
			aw8624->haptic_audio.ctr.loop,
			aw8624->haptic_audio.ctr.gain);
	}

	/* rtp mode jump */
	rtp_is_going_on = aw8624_haptic_juge_RTP_is_going_on(aw8624);
	if (rtp_is_going_on) {
		mutex_unlock(&aw8624->haptic_audio.lock);
		return;
	}
	mutex_unlock(&aw8624->haptic_audio.lock);

	if ((AW8624_HAPTIC_CMD_HAPTIC & aw8624->haptic_audio.ctr.cmd) ==
		AW8624_HAPTIC_CMD_ENABLE) {
		if (aw8624->haptic_audio.ctr.play ==
			AW8624_HAPTIC_PLAY_ENABLE) {
			aw_dev_info(aw8624->dev,
				"%s: haptic audio play start\n", __func__);
			mutex_lock(&aw8624->lock);
			aw8624_haptic_stop(aw8624);

			aw8624_haptic_play_mode(aw8624, AW8624_HAPTIC_RAM_MODE);

			aw8624_haptic_set_wav_seq(aw8624, 0x00,
					aw8624->haptic_audio.ctr.wavseq);

			aw8624_haptic_set_wav_loop(aw8624, 0x00,
					aw8624->haptic_audio.ctr.loop);

			aw8624_haptic_set_gain(aw8624,
					aw8624->haptic_audio.ctr.gain);

			aw8624_haptic_start(aw8624);
			mutex_unlock(&aw8624->lock);
		} else if (AW8624_HAPTIC_PLAY_STOP ==
			   aw8624->haptic_audio.ctr.play) {
			mutex_lock(&aw8624->lock);
			aw8624_haptic_stop(aw8624);
			mutex_unlock(&aw8624->lock);

		} else if (AW8624_HAPTIC_PLAY_GAIN ==
			   aw8624->haptic_audio.ctr.play) {
			mutex_lock(&aw8624->lock);
			aw8624_haptic_set_gain(aw8624,
					       aw8624->haptic_audio.ctr.gain);
			mutex_unlock(&aw8624->lock);
		}
	}
}

/*****************************************************
 *
 * haptic cont
 *
 *****************************************************/
static int aw8624_haptic_cont(struct aw8624 *aw8624)
{
	unsigned char brake0_level = 0;
	unsigned char en_brake1 = 0;
	unsigned char brake1_level = 0;
	unsigned char en_brake2 = 0;
	unsigned char brake2_level = 0;
	unsigned char brake2_p_num = 0;
	unsigned char brake1_p_num = 0;
	unsigned char brake0_p_num = 0;

	aw_dev_info(aw8624->dev, "%s enter\n.", __func__);
	/* work mode */
	aw8624_haptic_active(aw8624);
	aw8624_haptic_play_mode(aw8624, AW8624_HAPTIC_CONT_MODE);

	/* preset f0 */
	aw8624->f0_pre = aw8624->f0;
	aw8624_haptic_set_f0_preset(aw8624);

	/* lpf */
	aw8624_i2c_write_bits(aw8624,
				AW8624_REG_DATCTRL,
				AW8624_BIT_DATCTRL_FC_MASK,
				AW8624_BIT_DATCTRL_FC_1000HZ);
	aw8624_i2c_write_bits(aw8624,
				AW8624_REG_DATCTRL,
				AW8624_BIT_DATCTRL_LPF_ENABLE_MASK,
				AW8624_BIT_DATCTRL_LPF_ENABLE);

	/* brake */
	en_brake1 = aw8624_dts_data.aw8624_cont_brake[0][0];
	en_brake2 = aw8624_dts_data.aw8624_cont_brake[0][1];
	brake0_level = aw8624_dts_data.aw8624_cont_brake[0][2];
	brake1_level = aw8624_dts_data.aw8624_cont_brake[0][3];
	brake2_level = aw8624_dts_data.aw8624_cont_brake[0][4];
	brake0_p_num = aw8624_dts_data.aw8624_cont_brake[0][5];
	brake1_p_num = aw8624_dts_data.aw8624_cont_brake[0][6];
	brake2_p_num = aw8624_dts_data.aw8624_cont_brake[0][7];

	aw8624_i2c_write(aw8624,
			AW8624_REG_BRAKE0_CTRL,
			(brake0_level << 0));
	aw8624_i2c_write(aw8624,
			AW8624_REG_BRAKE1_CTRL,
			(en_brake1 << 7)|(brake1_level << 0));
	aw8624_i2c_write(aw8624,
			AW8624_REG_BRAKE2_CTRL,
			(en_brake2 << 7)|(brake2_level << 0));
	aw8624_i2c_write(aw8624,
			AW8624_REG_BRAKE_NUM,
			((brake2_p_num << 6)|(brake1_p_num << 3) |
			(brake0_p_num << 0)));

	/* cont config */
	aw8624_i2c_write_bits(aw8624,
				AW8624_REG_CONT_CTRL,
				AW8624_BIT_CONT_CTRL_ZC_DETEC_MASK,
				AW8624_BIT_CONT_CTRL_ZC_DETEC_ENABLE);
	aw8624_i2c_write_bits(aw8624,
				AW8624_REG_CONT_CTRL,
				AW8624_BIT_CONT_CTRL_WAIT_PERIOD_MASK,
				AW8624_BIT_CONT_CTRL_WAIT_1PERIOD);

	aw8624_i2c_write_bits(aw8624,
				AW8624_REG_CONT_CTRL,
				AW8624_BIT_CONT_CTRL_MODE_MASK,
				AW8624_BIT_CONT_CTRL_BY_GO_SIGNAL);

	aw8624_i2c_write_bits(aw8624,
				AW8624_REG_CONT_CTRL,
				AW8624_BIT_CONT_CTRL_EN_CLOSE_MASK,
				AW8624_BIT_CONT_CTRL_CLOSE_PLAYBACK);
	aw8624_i2c_write_bits(aw8624,
				AW8624_REG_CONT_CTRL,
				AW8624_BIT_CONT_CTRL_F0_DETECT_MASK,
				AW8624_BIT_CONT_CTRL_F0_DETECT_DISABLE);
	aw8624_i2c_write_bits(aw8624,
				AW8624_REG_CONT_CTRL,
				AW8624_BIT_CONT_CTRL_O2C_MASK,
				AW8624_BIT_CONT_CTRL_O2C_DISABLE);

	/* TD time */
	aw8624_i2c_write(aw8624,
			AW8624_REG_TD_H,
			(unsigned char)(aw8624->cont_td>>8));
	aw8624_i2c_write(aw8624,
			AW8624_REG_TD_L,
			(unsigned char)(aw8624->cont_td>>0));


	aw8624_i2c_write_bits(aw8624,
			AW8624_REG_BEMF_NUM,
			AW8624_BIT_BEMF_NUM_BRK_MASK,
			aw8624->cont_num_brk);
	aw8624_i2c_write(aw8624,
			AW8624_REG_TIME_NZC,
			0x1f);

	/* f0 driver level */
	aw8624_i2c_write(aw8624,
			AW8624_REG_DRV_LVL,
			aw8624->cont_drv_lvl);
	aw8624_i2c_write(aw8624,
			AW8624_REG_DRV_LVL_OV,
			aw8624->cont_drv_lvl_ov);

	/* cont play go */
	aw8624_haptic_play_go(aw8624, true);

	return 0;
}

/*****************************************************
 *
 * haptic f0 cali
 *
 *****************************************************/
static void aw8624_haptic_upload_lra(struct aw8624 *aw8624, unsigned int flag)
{
	switch (flag) {
	case AW8624_HAPTIC_F0_CALI_LRA:
		aw_dev_info(aw8624->dev, "%s f0_cali_lra=%d\n",
			__func__, aw8624->f0_calib_data);
		aw8624_i2c_write(aw8624, AW8624_REG_TRIM_LRA,
				 (char)aw8624->f0_calib_data);
		break;
	case AW8624_HAPTIC_RTP_CALI_LRA:
		aw_dev_info(aw8624->dev, "%s rtp_cali_lra=%d\n",
			__func__, aw8624->lra_calib_data);
		aw8624_i2c_write(aw8624, AW8624_REG_TRIM_LRA,
				 (char)aw8624->lra_calib_data);
		break;
	case AW8624_HAPTIC_ZERO:
		aw_dev_info(aw8624->dev,
			"%s write zero to trim_lra!\n", __func__);
		aw8624_i2c_write(aw8624, AW8624_REG_TRIM_LRA, 0);
		break;
	default:
		break;
	}
}
static int aw8624_haptic_get_f0(struct aw8624 *aw8624)
{
	int ret = 0;
	unsigned char reg_val = 0;
	unsigned char f0_pre_num = 0;
	unsigned char f0_wait_num = 0;
	unsigned char f0_repeat_num = 0;
	unsigned char f0_trace_num = 0;
	unsigned int t_f0_ms = 0;
	unsigned int t_f0_trace_ms = 0;
	unsigned char i = 0;
	unsigned int f0_cali_cnt = 50;


	aw_dev_info(aw8624->dev, "%s enter\n", __func__);

	aw8624->f0 = aw8624->f0_pre;
	/* f0 calibrate work mode */
	aw8624_haptic_stop(aw8624);
	aw8624_haptic_play_mode(aw8624, AW8624_HAPTIC_CONT_MODE);


	aw8624_i2c_write_bits(aw8624,
			AW8624_REG_CONT_CTRL,
			AW8624_BIT_CONT_CTRL_EN_CLOSE_MASK,
			AW8624_BIT_CONT_CTRL_OPEN_PLAYBACK);
	aw8624_i2c_write_bits(aw8624,
			AW8624_REG_CONT_CTRL,
			AW8624_BIT_CONT_CTRL_F0_DETECT_MASK,
			AW8624_BIT_CONT_CTRL_F0_DETECT_ENABLE);

	/* LPF */
	aw8624_i2c_write_bits(aw8624,
			AW8624_REG_DATCTRL,
			AW8624_BIT_DATCTRL_FC_MASK,
			AW8624_BIT_DATCTRL_FC_1000HZ);
	aw8624_i2c_write_bits(aw8624,
			AW8624_REG_DATCTRL,
			AW8624_BIT_DATCTRL_LPF_ENABLE_MASK,
			AW8624_BIT_DATCTRL_LPF_ENABLE);

	/* preset f0 */
	aw8624_haptic_set_f0_preset(aw8624);
	/* f0 driver level */
	aw8624_i2c_write(aw8624, AW8624_REG_DRV_LVL, aw8624->cont_drv_lvl);
	/* f0 trace parameter */
	if (!aw8624->f0_pre) {
		aw_dev_info(aw8624->dev, "%s:fail to get t_f0_ms\n", __func__);
		return 0;
	}

	f0_pre_num = aw8624_dts_data.aw8624_f0_trace_parameter[0];
	f0_wait_num = aw8624_dts_data.aw8624_f0_trace_parameter[1];
	f0_repeat_num = aw8624_dts_data.aw8624_f0_trace_parameter[2];
	f0_trace_num = aw8624_dts_data.aw8624_f0_trace_parameter[3];
	aw8624_i2c_write(aw8624,
			AW8624_REG_NUM_F0_1,
			(f0_pre_num<<4)|(f0_wait_num<<0));
	aw8624_i2c_write(aw8624,
			AW8624_REG_NUM_F0_2,
			(f0_repeat_num<<0));
	aw8624_i2c_write(aw8624,
			AW8624_REG_NUM_F0_3,
			(f0_trace_num<<0));

	/* clear aw8624 interrupt */
	ret = aw8624_i2c_read(aw8624, AW8624_REG_SYSINT, &reg_val);

	/* play go and start f0 calibration */
	aw8624_haptic_play_go(aw8624, true);

	/* f0 trace time */
	t_f0_ms = 1000*10 / aw8624->f0_pre;
	t_f0_trace_ms =
	    t_f0_ms * (f0_pre_num + f0_wait_num +
		       (f0_trace_num + f0_wait_num) * (f0_repeat_num - 1));
	aw_dev_info(aw8624->dev, "%s: t_f0_trace_ms = %dms\n",
		__func__, t_f0_trace_ms);
	usleep_range(t_f0_trace_ms * 1000, t_f0_trace_ms * 1000 + 500);

	for (i = 0; i < f0_cali_cnt; i++) {
		ret = aw8624_i2c_read(aw8624, AW8624_REG_GLB_STATE, &reg_val);
		/* f0 calibrate done */
		if ((reg_val & 0x0f) == 0x00) {
			aw8624_haptic_read_f0(aw8624);
			aw8624_haptic_read_beme(aw8624);
			break;
		}
		usleep_range(10000, 10500);
		aw_dev_info(aw8624->dev, "%s f0 cali sleep 10ms,glb_state=0x%x\n",
							__func__, reg_val);
	}

	if (i == f0_cali_cnt)
		ret = -ERANGE;
	else
		ret = 0;

	aw8624_i2c_write_bits(aw8624,
			      AW8624_REG_CONT_CTRL,
			      AW8624_BIT_CONT_CTRL_EN_CLOSE_MASK,
			      AW8624_BIT_CONT_CTRL_CLOSE_PLAYBACK);
	aw8624_i2c_write_bits(aw8624,
			      AW8624_REG_CONT_CTRL,
			      AW8624_BIT_CONT_CTRL_F0_DETECT_MASK,
			      AW8624_BIT_CONT_CTRL_F0_DETECT_DISABLE);

	return ret;
}

static int aw8624_haptic_f0_calibration(struct aw8624 *aw8624)
{
	int ret = 0;
	unsigned char reg_val = 0;
	unsigned int f0_limit = 0;
	char f0_cali_lra = 0;
	int f0_cali_step = 0;
	/*int f0_dft_step = 0;*/

	if (aw8624_haptic_get_f0(aw8624)) {
		aw_dev_err(aw8624->dev,
			"%s get f0 error, user defafult f0\n", __func__);
	} else {
		 /* max and min limit */
		f0_limit = aw8624->f0;
		if (aw8624->f0*100 < aw8624_dts_data.aw8624_f0_pre *
		(100-aw8624_dts_data.aw8624_f0_cali_percen)) {
			f0_limit = aw8624_dts_data.aw8624_f0_pre;
		}
		if (aw8624->f0*100 > aw8624_dts_data.aw8624_f0_pre *
		(100+aw8624_dts_data.aw8624_f0_cali_percen)) {
			f0_limit = aw8624_dts_data.aw8624_f0_pre;
		}
		/* calculate cali step */
		f0_cali_step =
		100000*((int)f0_limit-(int)aw8624->f0_pre)/((int)f0_limit*25);

		if (f0_cali_step >= 0) {  /*f0_cali_step >= 0*/
			if (f0_cali_step % 10 >= 5) {
				f0_cali_step = f0_cali_step/10 + 1 +
					(aw8624->chipid_flag == 1 ? 32 : 16);
			} else {
				f0_cali_step = f0_cali_step/10 +
					(aw8624->chipid_flag == 1 ? 32 : 16);
			}
		} else { /*f0_cali_step < 0*/
			if (f0_cali_step % 10 <= -5) {
				f0_cali_step =
					(aw8624->chipid_flag == 1 ? 32 : 16) +
					(f0_cali_step/10 - 1);
			} else {
				f0_cali_step =
					(aw8624->chipid_flag == 1 ? 32 : 16) +
					f0_cali_step/10;
			}
		}

		if (aw8624->chipid_flag == 1) {
			if (f0_cali_step > 31)
				f0_cali_lra = (char)f0_cali_step - 32;
			else
				f0_cali_lra = (char)f0_cali_step + 32;
		} else {
			if (f0_cali_step < 16 ||
			(f0_cali_step > 31 && f0_cali_step < 48)) {
				f0_cali_lra = (char)f0_cali_step + 16;
			} else {
				f0_cali_lra = (char)f0_cali_step - 16;
			}
		}

		aw8624->f0_calib_data = (int)f0_cali_lra;
		/* update cali step */
		aw8624_haptic_upload_lra(aw8624,
					AW8624_HAPTIC_F0_CALI_LRA);
		aw8624_i2c_read(aw8624,
				AW8624_REG_TRIM_LRA,
				&reg_val);
		aw_dev_info(aw8624->dev,
			"%s final trim_lra=0x%02x\n", __func__, reg_val);
	}

	/* if (aw8624_haptic_get_f0(aw8624)) { */
	/* aw_dev_err(aw8624->dev,*/
	/*	"%s get f0 error, user defafult f0\n", __func__); */
	/* } */

	aw8624_haptic_play_mode(aw8624, AW8624_HAPTIC_STANDBY_MODE);
	aw8624_haptic_stop(aw8624);

	return ret;
}

/*****************************************************
 *
 * haptic fops
 *
 *****************************************************/
static int aw8624_file_open(struct inode *inode, struct file *file)
{
	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	file->private_data = (void *)g_aw8624;

	return 0;
}

static int aw8624_file_release(struct inode *inode, struct file *file)
{
	file->private_data = (void *)NULL;

	module_put(THIS_MODULE);

	return 0;
}

static long aw8624_file_unlocked_ioctl(struct file *file,
					unsigned int cmd, unsigned long arg)
{
	struct aw8624 *aw8624 = (struct aw8624 *)file->private_data;
	int ret = 0;

	dev_info(aw8624->dev, "%s: cmd=0x%x, arg=0x%lx\n",
		__func__, cmd, arg);

	mutex_lock(&aw8624->lock);

	if (_IOC_TYPE(cmd) != AW8624_HAPTIC_IOCTL_MAGIC) {
		dev_err(aw8624->dev, "%s: cmd magic err\n",
				__func__);
		mutex_unlock(&aw8624->lock);
		return -EINVAL;
	}

	switch (cmd) {
	default:
		dev_err(aw8624->dev, "%s, unknown cmd\n", __func__);
		break;
	}

	mutex_unlock(&aw8624->lock);

	return ret;
}

static ssize_t aw8624_file_read(struct file *filp,
				char *buff, size_t len, loff_t *offset)
{
	struct aw8624 *aw8624 = (struct aw8624 *)filp->private_data;
	int ret = 0;
	int i = 0;
	unsigned char reg_val = 0;
	unsigned char *pbuff = NULL;

	mutex_lock(&aw8624->lock);

	dev_info(aw8624->dev, "%s: len=%zu\n", __func__, len);

	switch (aw8624->fileops.cmd) {
	case AW8624_HAPTIC_CMD_READ_REG:
		pbuff = kzalloc(len, GFP_KERNEL);
		if (pbuff != NULL) {
			for (i = 0; i < len; i++) {
				aw8624_i2c_read(aw8624,
						aw8624->fileops.reg+i,
						&reg_val);
				pbuff[i] = reg_val;
			}
			ret = copy_to_user(buff, pbuff, len);
			if (ret) {
				dev_err
				(aw8624->dev, "%s: copy to user fail\n",
				__func__);
			}
			kfree(pbuff);
		} else {
			dev_err(aw8624->dev, "%s: alloc memory fail\n",
				__func__);
		}
		break;
	default:
		dev_err(aw8624->dev, "%s, unknown cmd %d\n",
			__func__, aw8624->fileops.cmd);
		break;
	}

	mutex_unlock(&aw8624->lock);

	for (i = 0; i < len; i++) {
		dev_info(aw8624->dev, "%s: buff[%d]=0x%02x\n",
				__func__, i, buff[i]);
	}

	return len;
}

static ssize_t aw8624_file_write(struct file *filp,
				const char *buff, size_t len, loff_t *off)
{
	struct aw8624 *aw8624 = (struct aw8624 *)filp->private_data;
	int i = 0;
	int ret = 0;
	unsigned char *pbuff = NULL;


	mutex_lock(&aw8624->lock);

	aw8624->fileops.cmd = buff[0];

	switch (aw8624->fileops.cmd) {
	case AW8624_HAPTIC_CMD_READ_REG:
		if (len == 2) {
			aw8624->fileops.reg = buff[1];
		} else {
			dev_err(aw8624->dev,
			"%s: read cmd len %zu err\n",
			__func__, len);
		}
		break;
	case AW8624_HAPTIC_CMD_WRITE_REG:
		if (len > 2) {
			pbuff = kzalloc(len, GFP_KERNEL);
			if (pbuff != NULL) {
				ret = copy_from_user(pbuff, buff, len);
				if (ret) {
					dev_err(aw8624->dev,
					"%s: copy from user fail\n",
					__func__);
				} else {
					for (i = 0; i < len - 2; i++) {
						dev_info
						(aw8624->dev,
						"%s: write reg0x%x = 0x%x\n",
						__func__,
						pbuff[1]+i, pbuff[i+2]);
						aw8624_i2c_write(aw8624,
							pbuff[1]+i,
							pbuff[2+i]);
					}
				}
				kfree(pbuff);
			} else {
				dev_err(aw8624->dev,
					"%s: alloc memory fail\n",
					__func__);
			}
		} else {
			dev_err(aw8624->dev,
				"%s: write cmd len %zu err\n",
				__func__, len);
		}
		break;
	default:
		dev_err(aw8624->dev,
			"%s, unknown cmd %d\n",
			__func__, aw8624->fileops.cmd);
		break;
	}

	mutex_unlock(&aw8624->lock);

	return len;
}

static const struct file_operations fops = {
	.owner = THIS_MODULE,
	.read = aw8624_file_read,
	.write = aw8624_file_write,
	.unlocked_ioctl = aw8624_file_unlocked_ioctl,
	.open = aw8624_file_open,
	.release = aw8624_file_release,
};

struct miscdevice aw8624_haptic_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = AW8624_HAPTIC_NAME,
	.fops = &fops,
};

static int
aw8624_haptic_audio_ctr_list_insert(struct haptic_audio *haptic_audio,
				struct haptic_ctr *haptic_ctr,
				struct device *dev)
{
	struct haptic_ctr *p_new = NULL;

	p_new = (struct haptic_ctr *)kzalloc(
		sizeof(struct haptic_ctr), GFP_KERNEL);
	if (p_new == NULL) {
		aw_dev_err(dev, "%s: kzalloc memory fail\n", __func__);
		return -ERANGE;
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

static int aw8624_haptic_audio_ctr_list_clear(struct haptic_audio *haptic_audio)
{
	struct haptic_ctr *p_ctr = NULL;
	struct haptic_ctr *p_ctr_bak = NULL;

	list_for_each_entry_safe_reverse(p_ctr, p_ctr_bak,
				&(haptic_audio->ctr_list), list) {
		list_del(&p_ctr->list);
		kfree(p_ctr);
	}

	return 0;
}

static int aw8624_haptic_audio_off(struct aw8624 *aw8624)
{
	aw_dev_info(aw8624->dev, "%s: enter\n", __func__);
	mutex_lock(&aw8624->lock);
	aw8624_haptic_set_gain(aw8624, 0x80);
	aw8624_haptic_stop(aw8624);
	aw8624->gun_type = 0xff;
	aw8624->bullet_nr = 0;
	aw8624_haptic_audio_ctr_list_clear(&aw8624->haptic_audio);
	mutex_unlock(&aw8624->lock);
	return 0;
}

static int aw8624_haptic_audio_init(struct aw8624 *aw8624)
{

	aw_dev_info(aw8624->dev, "%s: enter\n", __func__);
	aw8624_haptic_set_wav_seq(aw8624, 0x01, 0x00);

	return 0;
}

static int aw8624_haptic_offset_calibration(struct aw8624 *aw8624)
{
	unsigned int cont = 2000;
	unsigned char reg_val = 0;
	unsigned char reg_val_sysctrl = 0;

	aw_dev_info(aw8624->dev, "%s enter\n", __func__);
	aw8624_i2c_read(aw8624, AW8624_REG_SYSCTRL, &reg_val_sysctrl);
	aw8624_i2c_write_bits(aw8624,
				AW8624_REG_SYSCTRL,
				AW8624_BIT_SYSCTRL_RAMINIT_MASK,
				AW8624_BIT_SYSCTRL_RAMINIT_EN);
	aw8624_i2c_write_bits(aw8624,
				AW8624_REG_DETCTRL,
				AW8624_BIT_DETCTRL_DIAG_GO_MASK,
				AW8624_BIT_DETCTRL_DIAG_GO_ENABLE);
	while (1) {
		aw8624_i2c_read(aw8624, AW8624_REG_DETCTRL, &reg_val);
		if ((reg_val & 0x01) == 0 || cont == 0)
			break;
		cont--;
	}
	if (cont == 0)
		aw_dev_err(aw8624->dev, "%s calibration offset failed!\n",
			   __func__);

	aw8624_i2c_write(aw8624, AW8624_REG_SYSCTRL, reg_val_sysctrl);
	return 0;

}

int aw8624_haptic_init(struct aw8624 *aw8624)
{
	int ret = 0;
	unsigned char i = 0;
	unsigned char reg_val = 0, reg_flag = 0;
	unsigned char bemf_config = 0;

	ret = misc_register(&aw8624_haptic_misc);
	if (ret) {
		dev_err(aw8624->dev, "%s: misc fail: %d\n", __func__, ret);
		return ret;
	}

	/* haptic audio */
	aw8624->haptic_audio.delay_val = 23;
	aw8624->haptic_audio.timer_val = 23;
	INIT_LIST_HEAD(&(aw8624->haptic_audio.ctr_list));
	hrtimer_init(&aw8624->haptic_audio.timer,
			CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	aw8624->haptic_audio.timer.function = aw8624_haptic_audio_timer_func;
	INIT_WORK(&aw8624->haptic_audio.work, aw8624_haptic_audio_work_routine);
	aw8624->gun_type = 0xff;
	aw8624->bullet_nr = 0x00;
	mutex_init(&aw8624->haptic_audio.lock);
	/* haptic init */
	mutex_lock(&aw8624->lock);
	ret = aw8624_i2c_read(aw8624, AW8624_REG_EF_RDATAH, &reg_flag);
	if ((ret >= 0) && ((reg_flag & 0x1) == 1)) {
		aw8624->chipid_flag = 1;
	} else {
		dev_err(aw8624->dev,
		"%s: to read register AW8624_REG_EF_RDATAH: %d\n",
		__func__, ret);
	}

	aw8624->activate_mode = aw8624_dts_data.aw8624_mode;
	ret = aw8624_i2c_read(aw8624, AW8624_REG_WAVSEQ1, &reg_val);
	aw8624->index = reg_val & 0x7F;
	ret = aw8624_i2c_read(aw8624, AW8624_REG_DATDBG, &reg_val);
	aw8624->gain = reg_val & 0xFF;
	for (i = 0; i < AW8624_SEQUENCER_SIZE; i++) {
		ret = aw8624_i2c_read(aw8624, AW8624_REG_WAVSEQ1+i, &reg_val);
		aw8624->seq[i] = reg_val;
	}

	aw8624_haptic_play_mode(aw8624, AW8624_HAPTIC_STANDBY_MODE);
	aw8624_haptic_set_pwm(aw8624, AW8624_PWM_24K);

	aw8624_haptic_swicth_motorprotect_config(aw8624, 0x0, 0x0);
	/*trig config*/
	aw8624_haptic_trig1_param_init(aw8624);
	aw8624_haptic_tirg1_param_config(aw8624);
	aw8624_haptic_offset_calibration(aw8624);
	aw8624_haptic_vbat_mode(aw8624, AW8624_HAPTIC_VBAT_HW_COMP_MODE);
	mutex_unlock(&aw8624->lock);

	/* f0 calibration */
	aw8624->f0_pre = aw8624_dts_data.aw8624_f0_pre;
	aw8624->cont_drv_lvl = aw8624_dts_data.aw8624_cont_drv_lvl;
	aw8624->cont_drv_lvl_ov = aw8624_dts_data.aw8624_cont_drv_lvl_ov;
	aw8624->cont_td = aw8624_dts_data.aw8624_cont_td;
	aw8624->cont_zc_thr = aw8624_dts_data.aw8624_cont_zc_thr;
	aw8624->cont_num_brk = aw8624_dts_data.aw8624_cont_num_brk;
	aw8624->ram_vbat_comp = AW8624_HAPTIC_RAM_VBAT_COMP_ENABLE;
	mutex_lock(&aw8624->lock);
	aw8624_i2c_write_bits(aw8624, AW8624_REG_R_SPARE,
		AW8624_BIT_R_SPARE_MASK, AW8624_BIT_R_SPARE_ENABLE);
	/*LRA trim source select register*/
	aw8624_i2c_write_bits(aw8624,
				AW8624_REG_ANACTRL,
				AW8624_BIT_ANACTRL_LRA_SRC_MASK,
				AW8624_BIT_ANACTRL_LRA_SRC_REG);
	aw8624_haptic_upload_lra(aw8624, AW8624_HAPTIC_ZERO);
	aw8624_haptic_f0_calibration(aw8624);
	mutex_unlock(&aw8624->lock);

	/*brake*/
	mutex_lock(&aw8624->lock);
	aw8624_i2c_write(aw8624,
			AW8624_REG_SW_BRAKE,
			(unsigned char)(aw8624_dts_data.aw8624_sw_brake[0]));
	aw8624_i2c_write(aw8624, AW8624_REG_THRS_BRA_END, 0x00);
	aw8624_i2c_write_bits(aw8624,
			AW8624_REG_WAVECTRL,
			AW8624_BIT_WAVECTRL_NUM_OV_DRIVER_MASK,
			AW8624_BIT_WAVECTRL_NUM_OV_DRIVER);
	aw8624->f0_value = 20000 / aw8624_dts_data.aw8624_f0_pre + 1;
	/* zero cross */
	aw8624_i2c_write(aw8624,
			AW8624_REG_ZC_THRSH_H,
			(unsigned char)(aw8624->cont_zc_thr>>8));
	aw8624_i2c_write(aw8624,
			AW8624_REG_ZC_THRSH_L,
			(unsigned char)(aw8624->cont_zc_thr>>0));
	aw8624_i2c_write(aw8624,
			AW8624_REG_TSET,
			aw8624_dts_data.aw8624_tset);

	/* bemf */
	bemf_config = aw8624_dts_data.aw8624_bemf_config[0];
	aw8624_i2c_write(aw8624, AW8624_REG_BEMF_VTHH_H, bemf_config);
	bemf_config = aw8624_dts_data.aw8624_bemf_config[1];
	aw8624_i2c_write(aw8624, AW8624_REG_BEMF_VTHH_L, bemf_config);
	bemf_config = aw8624_dts_data.aw8624_bemf_config[2];
	aw8624_i2c_write(aw8624, AW8624_REG_BEMF_VTHL_H, bemf_config);
	bemf_config = aw8624_dts_data.aw8624_bemf_config[3];
	aw8624_i2c_write(aw8624, AW8624_REG_BEMF_VTHL_L, bemf_config);
	mutex_unlock(&aw8624->lock);

	return ret;
}

/*****************************************************
 *
 * vibrator
 *
 *****************************************************/
#ifdef TIMED_OUTPUT
static int aw8624_vibrator_get_time(struct timed_output_dev *to_dev)
{
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);

	if (hrtimer_active(&aw8624->timer)) {
		ktime_t r = hrtimer_get_remaining(&aw8624->timer);

		return ktime_to_ms(r);
	}

	return 0;
}

static void aw8624_vibrator_enable(struct timed_output_dev *to_dev, int value)
{
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);

	aw_dev_info(aw8624->dev, "%s enter\n", __func__);
	if (!aw8624->ram_init) {
		aw_dev_err(aw8624->dev, "%s: ram init failed, not allow to play!\n",
		       __func__);
		return;
	}
	mutex_lock(&aw8624->lock);
	aw8624_haptic_stop(aw8624);
	if (value > 0) {
		aw8624_haptic_upload_lra(aw8624, AW8624_HAPTIC_F0_CALI_LRA);
		aw8624_haptic_ram_vbat_comp(aw8624, false);
		aw8624_haptic_play_wav_seq(aw8624, value);
	}

	mutex_unlock(&aw8624->lock);
}

#else
static void
aw8624_vibrator_enable(struct led_classdev *dev, enum led_brightness value)
{
	struct aw8624 *aw8624 = container_of(dev, struct aw8624, cdev);

	aw_dev_info(aw8624->dev, "%s enter\n", __func__);
	if (!aw8624->ram_init) {
		aw_dev_err(aw8624->dev, "%s: ram init failed, not allow to play!\n",
		       __func__);
		return;
	}
	mutex_lock(&aw8624->lock);
	aw8624_haptic_stop(aw8624);
	if (value > 0) {
		aw8624_haptic_upload_lra(aw8624, AW8624_HAPTIC_F0_CALI_LRA);
		aw8624_haptic_ram_vbat_comp(aw8624, false);
		aw8624_haptic_play_wav_seq(aw8624, value);
	}

	mutex_unlock(&aw8624->lock);


}

#endif

static ssize_t aw8624_state_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif


	return snprintf(buf, PAGE_SIZE, "%d\n", aw8624->state);
}

static ssize_t aw8624_state_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t aw8624_duration_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif
	ktime_t time_rem;
	s64 time_ms = 0;

	if (hrtimer_active(&aw8624->timer)) {
		time_rem = hrtimer_get_remaining(&aw8624->timer);
		time_ms = ktime_to_ms(time_rem);
	}

	return snprintf(buf, PAGE_SIZE, "%lld\n", time_ms);
}

static ssize_t aw8624_duration_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	/* setting 0 on duration is NOP for now */
	if (val <= 0)
		return count;

	if (val <= aw8624->f0_value)
		val = aw8624->f0_value;
	rc = aw8624_haptic_ram_config(aw8624, val);
	if (rc < 0)
		return rc;
	aw8624->duration = val;
	return count;
}

static ssize_t aw8624_activate_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif

	/* For now nothing to show */
	return snprintf(buf, PAGE_SIZE, "%d\n", aw8624->state);
}

static ssize_t aw8624_activate_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif
	unsigned int val = 0;
	int rc = 0;

	if (!aw8624->ram_init) {
		aw_dev_err(aw8624->dev, "%s: ram init failed, not allow to play!\n",
		       __func__);
		return count;
	}
	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	if (val != 0 && val != 1)
		return count;

	aw_dev_info(aw8624->dev, "%s: value=%d\n", __func__, val);

	mutex_lock(&aw8624->lock);
	hrtimer_cancel(&aw8624->timer);

	aw8624->state = val;

	/*aw8624_haptic_stop(aw8624);*/

	mutex_unlock(&aw8624->lock);
	schedule_work(&aw8624->vibrator_work);

	return count;
}

static ssize_t aw8624_activate_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif

	return snprintf(buf, PAGE_SIZE, "%d\n",
			aw8624->activate_mode);
}

static ssize_t aw8624_activate_mode_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif

	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	mutex_lock(&aw8624->lock);
	aw8624->activate_mode = val;
	mutex_unlock(&aw8624->lock);
	return count;
}


static ssize_t aw8624_index_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif

	unsigned char reg_val = 0;

	aw8624_i2c_read(aw8624, AW8624_REG_WAVSEQ1, &reg_val);
	aw8624->index = reg_val;

	return snprintf(buf, PAGE_SIZE, "%d\n", aw8624->index);
}

static ssize_t aw8624_index_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;
	if (val > aw8624->ram.ram_num) {
		aw_dev_err(aw8624->dev,
			   "%s: input value out of range!\n", __func__);
		return count;
	}
	aw_dev_info(aw8624->dev, "%s: value=%d\n", __func__, val);

	mutex_lock(&aw8624->lock);
	aw8624->index = val;
	aw8624_haptic_set_repeat_wav_seq(aw8624, aw8624->index);
	mutex_unlock(&aw8624->lock);
	return count;
}

static ssize_t aw8624_gain_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif

	return snprintf(buf, PAGE_SIZE, "0x%02x\n", aw8624->gain);
}

static ssize_t aw8624_gain_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	aw_dev_info(aw8624->dev, "%s: value=%d\n", __func__, val);

	mutex_lock(&aw8624->lock);
	aw8624->gain = val;
	aw8624_haptic_set_gain(aw8624, aw8624->gain);
	mutex_unlock(&aw8624->lock);
	return count;
}

static ssize_t aw8624_seq_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif
	size_t count = 0;
	unsigned char i = 0;
	unsigned char reg_val = 0;

	for (i = 0; i < AW8624_SEQUENCER_SIZE; i++) {
		aw8624_i2c_read(aw8624, AW8624_REG_WAVSEQ1+i, &reg_val);
		count += snprintf(buf+count, PAGE_SIZE-count,
			"seq%d: 0x%02x\n", i+1, reg_val);
		aw8624->seq[i] |= reg_val;
	}
	return count;
}

static ssize_t aw8624_seq_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif
	unsigned int databuf[2] = {0, 0};

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
		if (databuf[0] > AW8624_SEQUENCER_SIZE ||
		    databuf[1] > aw8624->ram.ram_num) {
			aw_dev_err(aw8624->dev, "%s input value out of range\n",
				__func__);
			return count;
		}
		aw_dev_info(aw8624->dev, "%s: seq%d=0x%x\n",
			__func__, databuf[0], databuf[1]);
		mutex_lock(&aw8624->lock);
		aw8624->seq[databuf[0]] = (unsigned char)databuf[1];
		aw8624_haptic_set_wav_seq(aw8624, (unsigned char)databuf[0],
			aw8624->seq[databuf[0]]);
		mutex_unlock(&aw8624->lock);
	}
	return count;
}

static ssize_t aw8624_loop_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif

	size_t count = 0;
	unsigned char i = 0;
	unsigned char reg_val = 0;

	for (i = 0; i < AW8624_SEQUENCER_LOOP_SIZE; i++) {
		aw8624_i2c_read(aw8624, AW8624_REG_WAVLOOP1+i, &reg_val);
		aw8624->loop[i*2+0] = (reg_val>>4)&0x0F;
		aw8624->loop[i*2+1] = (reg_val>>0)&0x0F;

		count += snprintf(buf+count, PAGE_SIZE-count,
			"seq%d_loop: 0x%02x\n", i*2+1, aw8624->loop[i*2+0]);
		count += snprintf(buf+count, PAGE_SIZE-count,
			"seq%d_loop: 0x%02x\n", i*2+2, aw8624->loop[i*2+1]);
	}
	return count;
}

static ssize_t aw8624_loop_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif
	unsigned int databuf[2] = {0, 0};

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
		aw_dev_info(aw8624->dev, "%s: seq%d loop=0x%x\n",
			__func__, databuf[0], databuf[1]);
		mutex_lock(&aw8624->lock);
		aw8624->loop[databuf[0]] = (unsigned char)databuf[1];
		aw8624_haptic_set_wav_loop(aw8624, (unsigned char)databuf[0],
			aw8624->loop[databuf[0]]);
		mutex_unlock(&aw8624->lock);
	}

	return count;
}

static ssize_t aw8624_reg_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif

	ssize_t len = 0;
	unsigned char i = 0;
	unsigned char reg_val = 0;

	aw_dev_info(aw8624->dev, "%s enter\n", __func__);
	for (i = 0; i < AW8624_REG_MAX; i++) {
		if (!(aw8624_reg_access[i]&REG_RD_ACCESS))
			continue;
		aw8624_i2c_read(aw8624, i, &reg_val);
		len += snprintf(buf+len, PAGE_SIZE-len, "reg:0x%02x=0x%02x\n",
				i, reg_val);
	}
	return len;
}

static ssize_t aw8624_reg_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif

	unsigned int databuf[2] = {0, 0};

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
		aw8624_i2c_write(aw8624,
				(unsigned char)databuf[0],
				(unsigned char)databuf[1]);
	}

	return count;
}

static ssize_t aw8624_rtp_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif

	ssize_t len = 0;

	len += snprintf(buf+len, PAGE_SIZE-len, "rtp_cnt = %d\n",
			aw8624->rtp_cnt);

	return len;
}

static ssize_t aw8624_rtp_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif


	unsigned int val = 0;
	int rc = 0;

	if (!(aw8624->IsUsedIRQ))
		return rc;
	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	mutex_lock(&aw8624->lock);
	aw8624_haptic_stop(aw8624);
	aw8624_haptic_set_rtp_aei(aw8624, false);
	aw8624_interrupt_clear(aw8624);
	mutex_unlock(&aw8624->lock);
	if (val < (sizeof(aw8624_rtp_name)/AW8624_RTP_NAME_MAX)) {
		aw8624->rtp_file_num = val;
		if (val) {
			aw_dev_info(aw8624->dev,
				"%s: aw8624_rtp_name[%d]: %s\n", __func__,
				val, aw8624_rtp_name[val]);
			schedule_work(&aw8624->rtp_work);
		} else
			aw_dev_err(aw8624->dev,
				"%s: rtp_file_num 0x%02X over max value\n",
				__func__, aw8624->rtp_file_num);
	} else {
		aw_dev_err(aw8624->dev, "%s: rtp_file_num 0x%02x over max value\n",
			__func__, aw8624->rtp_file_num);
	}

	return count;
}


static ssize_t aw8624_ram_update_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif
	ssize_t len = 0;
	unsigned char reg_val_sysctrl = 0;
	unsigned char reg_val = 0;
	unsigned int i = 0;

	aw8624_i2c_read(aw8624, AW8624_REG_SYSCTRL, &reg_val_sysctrl);
	/* RAMINIT Enable */
	aw8624_i2c_write_bits(aw8624,
				AW8624_REG_SYSCTRL,
				AW8624_BIT_SYSCTRL_RAMINIT_MASK,
				AW8624_BIT_SYSCTRL_RAMINIT_EN);
	aw8624_i2c_write(aw8624, AW8624_REG_RAMADDRH,
			  (unsigned char)(aw8624->ram.base_addr >> 8));
	aw8624_i2c_write(aw8624, AW8624_REG_RAMADDRL,
			  (unsigned char)(aw8624->ram.base_addr & 0x00ff));
	len += snprintf(buf + len, PAGE_SIZE - len,
			"haptic_ram len = %d\n", aw8624->ram.len);
	for (i = 0; i < aw8624->ram.len; i++) {
		aw8624_i2c_read(aw8624, AW8624_REG_RAMDATA, &reg_val);
		if (i % 5 == 0)
			len += snprintf(buf + len,
					PAGE_SIZE - len,
					"0x%02X\n", reg_val);
		else
			len += snprintf(buf + len,
					PAGE_SIZE - len,
					"0x%02X,", reg_val);
	}
	len += snprintf(buf + len, PAGE_SIZE - len, "\n");
	/* RAMINIT Disable */
	aw8624_i2c_write(aw8624, AW8624_REG_SYSCTRL, reg_val_sysctrl);
	return len;
}

static ssize_t aw8624_ram_update_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif


	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	if (val)
		aw8624_ram_update(aw8624);

	return count;
}

static ssize_t aw8624_f0_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif


	ssize_t len = 0;
	unsigned char temp = 0;

	mutex_lock(&aw8624->lock);
	aw8624_i2c_read(aw8624, AW8624_REG_TRIM_LRA, &temp);
	aw8624_haptic_upload_lra(aw8624, AW8624_HAPTIC_ZERO);
	aw8624_haptic_get_f0(aw8624);
	aw8624_i2c_write(aw8624, AW8624_REG_TRIM_LRA, temp);
	mutex_unlock(&aw8624->lock);
	len += snprintf(buf+len, PAGE_SIZE-len, "%d\n", aw8624->f0);
	aw_dev_info(aw8624->dev, "len = %zd, buf=%s", len, buf);
	return len;
}

static ssize_t aw8624_f0_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	return count;
}


static ssize_t aw8624_cali_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif

	ssize_t len = 0;
	unsigned char temp = 0;

	mutex_lock(&aw8624->lock);
	aw8624_i2c_read(aw8624, AW8624_REG_TRIM_LRA, &temp);
	aw8624_haptic_upload_lra(aw8624, AW8624_HAPTIC_F0_CALI_LRA);
	aw8624_haptic_get_f0(aw8624);
	aw8624_i2c_write(aw8624, AW8624_REG_TRIM_LRA, temp);
	mutex_unlock(&aw8624->lock);

	len += snprintf(buf+len, PAGE_SIZE-len, "%d\n", aw8624->f0);
	return len;
}

static ssize_t
aw8624_cali_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif


	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	if (val) {
		mutex_lock(&aw8624->lock);
		aw8624_haptic_upload_lra(aw8624, AW8624_HAPTIC_ZERO);
		aw8624_haptic_f0_calibration(aw8624);
		mutex_unlock(&aw8624->lock);
	}
	return count;
}

static ssize_t
aw8624_cont_show(struct device *dev, struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif

	ssize_t len = 0;

	aw8624_haptic_read_cont_f0(aw8624);
	len += snprintf(buf+len, PAGE_SIZE-len, "%d\n", aw8624->cont_f0);
	return len;
}

static ssize_t
aw8624_cont_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif

	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	aw8624_haptic_stop(aw8624);

	if (val)
		aw8624_haptic_cont(aw8624);
	return count;
}


static ssize_t
aw8624_cont_td_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif

	ssize_t len = 0;

	len += snprintf(buf+len, PAGE_SIZE-len,
			"cont_delay_time = 0x%04x\n",
			aw8624->cont_td);
	return len;
}

static ssize_t
aw8624_cont_td_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif
	int err, val;

	err = kstrtoint(buf, 16, &val);
	if (err != 0) {
		aw_dev_err(aw8624->dev, "%s format not match!", __func__);
		return count;
	}

	aw8624->cont_td = val;
	aw8624_i2c_write(aw8624,
			AW8624_REG_TD_H,
			(unsigned char)(val >> 8));
	aw8624_i2c_write(aw8624,
			AW8624_REG_TD_L,
			(unsigned char)(val >> 0));

	return count;
}

static ssize_t
aw8624_cont_drv_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif

	ssize_t len = 0;

	len += snprintf(buf+len, PAGE_SIZE-len,
			"cont drv level = 0x%02x\n",
			aw8624->cont_drv_lvl);
	len += snprintf(buf+len, PAGE_SIZE-len,
			"cont drv level overdrive= 0x%02x\n",
			aw8624->cont_drv_lvl_ov);
	return len;
}

static ssize_t
aw8624_cont_drv_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif

	unsigned int databuf[2] = {0, 0};

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
		aw8624->cont_drv_lvl = databuf[0];
		aw8624_i2c_write(aw8624,
				AW8624_REG_DRV_LVL,
				aw8624->cont_drv_lvl);
		aw8624->cont_drv_lvl_ov = databuf[1];
		aw8624_i2c_write(aw8624,
				AW8624_REG_DRV_LVL_OV,
				aw8624->cont_drv_lvl_ov);
	}
	return count;
}

static ssize_t
aw8624_cont_num_brk_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif

	ssize_t len = 0;

	len += snprintf(buf+len, PAGE_SIZE-len,
			"cont_brk_num = 0x%02x\n",
			aw8624->cont_num_brk);
	return len;
}

static ssize_t
aw8624_cont_num_brk_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif

	int err, val;

	err = kstrtoint(buf, 16, &val);
	if (err != 0) {
		aw_dev_err(aw8624->dev, "%s format not match!", __func__);
		return count;
	}

	aw8624->cont_num_brk = val;
	if (aw8624->cont_num_brk > 7)
		aw8624->cont_num_brk = 7;

	aw8624_i2c_write_bits(aw8624, AW8624_REG_BEMF_NUM,
		AW8624_BIT_BEMF_NUM_BRK_MASK, aw8624->cont_num_brk);

	return count;
}

static ssize_t
aw8624_cont_zc_thr_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif
	ssize_t len = 0;

	len += snprintf(buf+len, PAGE_SIZE-len,
			"cont_zero_cross_thr = 0x%04x\n",
			aw8624->cont_zc_thr);
	return len;
}

static ssize_t
aw8624_cont_zc_thr_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif

	int err, val;

	err = kstrtoint(buf, 0, &val);
	if (err != 0) {
		aw_dev_err(aw8624->dev, "%s format not match!", __func__);
		return count;
	}
	aw_dev_info(aw8624->dev, "%s: val=%d\n", __func__, val);
	if (val == 1) {
		aw8624_i2c_write(aw8624,
				AW8624_REG_ZC_THRSH_H,
				(unsigned char)(val >> 8));
		aw8624_i2c_write(aw8624,
				AW8624_REG_ZC_THRSH_L,
				(unsigned char)(val >> 0));
	}
	return count;
}

static ssize_t
aw8624_vbat_monitor_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif

	ssize_t len = 0;
	unsigned int vbat = 0;

	mutex_lock(&aw8624->lock);
	vbat = aw8624_vbat_monitor_detector(aw8624);
	mutex_unlock(&aw8624->lock);
	len += snprintf(buf+len, PAGE_SIZE-len, "vbat_monitor = %d\n", vbat);

	return len;
}

static ssize_t
aw8624_vbat_monitor_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	return count;
}

static ssize_t
aw8624_lra_resistance_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif
	ssize_t len = 0;
	unsigned int r_lra = 0;

	r_lra = aw8624_lra_resistance_detector(aw8624);

	len += snprintf(buf+len, PAGE_SIZE-len, "lra_resistance = %d\n", r_lra);
	return len;
}


static ssize_t
aw8624_lra_resistance_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	return count;
}



static ssize_t
aw8624_prctmode_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif

	ssize_t len = 0;
	unsigned char reg_val = 0;

	aw8624_i2c_read(aw8624, AW8624_REG_RLDET, &reg_val);

	len += snprintf(buf+len, PAGE_SIZE-len,
			"prctmode = %d\n", reg_val&0x20);
	return len;
}


static ssize_t
aw8624_prctmode_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif
	unsigned int databuf[2] = {0, 0};
	unsigned int addr = 0;
	unsigned int val = 0;

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
		addr = databuf[0];
		val = databuf[1];
		mutex_lock(&aw8624->lock);
		aw8624_haptic_swicth_motorprotect_config(aw8624, addr, val);
		mutex_unlock(&aw8624->lock);
	}
	return count;
}

static ssize_t
aw8624_ram_vbat_comp_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif
	ssize_t len = 0;

	len += snprintf(buf+len, PAGE_SIZE-len,
			"ram_vbat_comp = %d\n",
			aw8624->ram_vbat_comp);

	return len;
}


static ssize_t
aw8624_ram_vbat_comp_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif

	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	mutex_lock(&aw8624->lock);
	if (val)
		aw8624->ram_vbat_comp = AW8624_HAPTIC_RAM_VBAT_COMP_ENABLE;
	else
		aw8624->ram_vbat_comp = AW8624_HAPTIC_RAM_VBAT_COMP_DISABLE;

	mutex_unlock(&aw8624->lock);

	return count;
}

static ssize_t
aw8624_haptic_audio_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif

	ssize_t len = 0;

	len += snprintf(buf+len, PAGE_SIZE-len, "%d\n",
			aw8624->haptic_audio.ctr.cnt);
	return len;
}

static ssize_t
aw8624_haptic_audio_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif
	unsigned int databuf[6] = {0};
	int rtp_is_going_on = 0;
	struct haptic_ctr *hap_ctr = NULL;

	rtp_is_going_on = aw8624_haptic_juge_RTP_is_going_on(aw8624);
	if (rtp_is_going_on) {
		aw_dev_info(aw8624->dev,
			"%s: RTP is runing, stop audio haptic\n", __func__);
		return count;
	}
	if (!aw8624->ram_init) {
		aw_dev_err(aw8624->dev,
			"%s: ram init failed, not allow to play!\n",
			__func__);
		return count;
	}

	if (sscanf(buf, "%d %d %d %d %d %d",
		&databuf[0], &databuf[1], &databuf[2],
		&databuf[3], &databuf[4], &databuf[5]) == 6) {
		if (databuf[2]) {
			aw_dev_dbg(aw8624->dev,
				"%s: cnt=%d, cmd=%d, play=%d, wavseq=%d, loop=%d, gain=%d\n",
				__func__,
				databuf[0], databuf[1], databuf[2],
				databuf[3], databuf[4], databuf[5]);
			hap_ctr = (struct haptic_ctr *)kzalloc(sizeof(struct haptic_ctr),
								GFP_KERNEL);
			if (hap_ctr == NULL) {
				aw_dev_err(aw8624->dev,
					"%s: kzalloc memory fail\n", __func__);
				return count;
			}
			mutex_lock(&aw8624->haptic_audio.lock);
			hap_ctr->cnt = (unsigned char)databuf[0];
			hap_ctr->cmd = (unsigned char)databuf[1];
			hap_ctr->play = (unsigned char)databuf[2];
			hap_ctr->wavseq = (unsigned char)databuf[3];
			hap_ctr->loop = (unsigned char)databuf[4];
			hap_ctr->gain = (unsigned char)databuf[5];
			aw8624_haptic_audio_ctr_list_insert(&aw8624->haptic_audio,
							hap_ctr, aw8624->dev);

			if (hap_ctr->cmd == 0xff) {
				aw_dev_info(aw8624->dev,
					"%s: haptic_audio stop\n", __func__);
				if (hrtimer_active(&aw8624->haptic_audio.timer)) {
					aw_dev_info(aw8624->dev,
						"%s: cancel haptic_audio_timer\n",
						__func__);
					hrtimer_cancel(&aw8624->haptic_audio.timer);
					aw8624->haptic_audio.ctr.cnt = 0;
					aw8624_haptic_audio_off(aw8624);
				}
			} else {
				if (hrtimer_active(&aw8624->haptic_audio.timer)) {
				} else {
					aw_dev_info(aw8624->dev, "%s: start haptic_audio_timer\n",
						__func__);
					aw8624_haptic_audio_init(aw8624);
					hrtimer_start(&aw8624->haptic_audio.timer,
					ktime_set(aw8624->haptic_audio.delay_val/1000,
					(aw8624->haptic_audio.delay_val%1000)*1000000),
					HRTIMER_MODE_REL);
				}
			}
		}
		mutex_unlock(&aw8624->haptic_audio.lock);
		kfree(hap_ctr);


	}
	return count;
}

static ssize_t
aw8624_haptic_audio_time_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{

#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif
	ssize_t len = 0;

	len += snprintf(buf+len, PAGE_SIZE-len,
			"haptic_audio.delay_val=%dus\n",
			aw8624->haptic_audio.delay_val);
	len += snprintf(buf+len, PAGE_SIZE-len,
			"haptic_audio.timer_val=%dus\n",
			aw8624->haptic_audio.timer_val);
	return len;
}

static ssize_t
aw8624_haptic_audio_time_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif

	unsigned int databuf[2] = {0};

	if (sscanf(buf, "%d %d", &databuf[0], &databuf[1]) == 2) {
		aw8624->haptic_audio.delay_val = databuf[0];
		aw8624->haptic_audio.timer_val = databuf[1];
	}
	return count;
}

static int aw8624_clock_OSC_trim_calibration
		(unsigned long int theory_time, unsigned long int real_time)
{
	unsigned int real_code = 0;
	unsigned int LRA_TRIM_CODE = 0;
	unsigned int DFT_LRA_TRIM_CODE = 0;
	unsigned int Not_need_cali_threshold = 10;/*0.1 percent not need cali*/

	aw_dev_info(g_aw8624->dev, "%s enter\n", __func__);
	if (theory_time == real_time) {
		aw_dev_info(g_aw8624->dev,
			"aw_osctheory_time == real_time:%ld,", real_time);
		aw_dev_info(g_aw8624->dev,
			"theory_time = %ld not need to cali\n", theory_time);
		return 0;
	} else if (theory_time < real_time) {
		if ((real_time - theory_time) > (theory_time / 25)) {
			aw_dev_info(g_aw8624->dev,
				"%s: failed not to cali\n", __func__);
			return DFT_LRA_TRIM_CODE;
		}

		if ((real_time - theory_time) <
				(Not_need_cali_threshold*theory_time/10000)) {
			aw_dev_info(g_aw8624->dev, "aw_oscmicrosecond:%ld,theory_time = %ld\n",
							real_time, theory_time);
			return DFT_LRA_TRIM_CODE;
		}

		real_code = ((real_time - theory_time) * 4000) / theory_time;
		real_code = ((real_code%10 < 5) ? 0 : 1) + real_code/10;
		real_code = 32 + real_code;
	} else if (theory_time > real_time) {
		if ((theory_time - real_time) > (theory_time / 25)) {
			aw_dev_info(g_aw8624->dev, "failed not to cali\n");
			return DFT_LRA_TRIM_CODE;
		}
		if ((theory_time - real_time) <
				(Not_need_cali_threshold * theory_time/10000)) {
			aw_dev_info(g_aw8624->dev, "aw_oscmicrosecond:%ld,theory_time = %ld\n",
							real_time, theory_time);
			return DFT_LRA_TRIM_CODE;
		}
		real_code = ((theory_time - real_time) * 4000) / theory_time;
		real_code = ((real_code%10 < 5) ? 0 : 1) + real_code/10;
		real_code = 32 - real_code;
	}
	if (real_code > 31)
		LRA_TRIM_CODE = real_code - 32;
	else
		LRA_TRIM_CODE = real_code + 32;

	aw_dev_info(g_aw8624->dev, "aw_oscmicrosecond:%ld,theory_time = %ld,real_code =0X%02X,",
					real_time, theory_time, real_code);
	aw_dev_info(g_aw8624->dev, "LRA_TRIM_CODE 0X%02X\n", LRA_TRIM_CODE);

	return LRA_TRIM_CODE;
}

static int aw8624_rtp_trim_lra_calibration(struct aw8624 *aw8624)
{
	unsigned char reg_val = 0;
	unsigned int fre_val = 0;
	unsigned int theory_time = 0;
	unsigned int lra_rtim_code = 0;

	aw_dev_info(aw8624->dev, "%s enter\n", __func__);
	aw8624_i2c_read(aw8624, AW8624_REG_PWMDBG, &reg_val);
	fre_val = (reg_val & 0x006f) >> 5;

	if (fre_val == 3)
		theory_time = (aw8624->rtp_len / 12000) * 1000000; /*12K */
	if (fre_val == 2)
		theory_time = (aw8624->rtp_len / 24000) * 1000000; /*24K */
	if (fre_val == 1 || fre_val == 0)
		theory_time = (aw8624->rtp_len / 48000) * 1000000; /*48K */

	aw_dev_info(aw8624->dev, "microsecond:%ld  theory_time = %d\n",
					aw8624->microsecond, theory_time);

	lra_rtim_code = aw8624_clock_OSC_trim_calibration(theory_time,
							aw8624->microsecond);
	if (lra_rtim_code >= 0) {
		aw8624->lra_calib_data = lra_rtim_code;
		aw8624_i2c_write(aw8624, AW8624_REG_TRIM_LRA,
							(char)lra_rtim_code);
	}
	return 0;
}

static unsigned char aw8624_haptic_osc_read_int(struct aw8624 *aw8624)
{
	unsigned char reg_val = 0;

	aw8624_i2c_read(aw8624, AW8624_REG_DBGSTAT, &reg_val);
	return reg_val;
}

static int aw8624_rtp_osc_calibration(struct aw8624 *aw8624)
{
	const struct firmware *rtp_file;
	int ret = -1;
	unsigned int buf_len = 0;
	unsigned char osc_int_state = 0;

	aw8624->rtp_cnt = 0;
	aw8624->timeval_flags = 1;
	aw8624->osc_cali_flag = 1;

	aw_dev_info(aw8624->dev, "%s enter\n", __func__);
	/* fw loaded */
	ret = request_firmware(&rtp_file,
				aw8624_rtp_name[0],/*aw8624->rtp_file_num */
				aw8624->dev);
	if (ret < 0) {
		/*aw8624->rtp_file_num */
		aw_dev_err(aw8624->dev,
			"%s: failed to read %s\n",
			__func__, aw8624_rtp_name[0]);
		return ret;
	}

	/*awinic add stop,for irq interrupt during calibrate*/
	aw8624_haptic_stop(aw8624);
	aw8624->rtp_init = 0;
	mutex_lock(&aw8624->rtp_lock);
	vfree(aw8624->rtp_container);
	aw8624->rtp_container = vmalloc(rtp_file->size + sizeof(int));
	if (!aw8624->rtp_container) {
		release_firmware(rtp_file);
		mutex_unlock(&aw8624->rtp_lock);
		aw_dev_err(aw8624->dev,
			"%s: error allocating memory\n", __func__);
		return -ENOMEM;
	}
	aw8624->rtp_container->len = rtp_file->size;
	aw8624->rtp_len = rtp_file->size;
	/*aw8624->rtp_file_num */
	aw_dev_info(aw8624->dev, "%s: rtp file [%s] size = %d\n", __func__,
		aw8624_rtp_name[0], aw8624->rtp_container->len);
	memcpy(aw8624->rtp_container->data, rtp_file->data, rtp_file->size);
	release_firmware(rtp_file);
	mutex_unlock(&aw8624->rtp_lock);

	/* gain */
	aw8624_haptic_ram_vbat_comp(aw8624, false);

	/* rtp mode config */
	aw8624_haptic_play_mode(aw8624, AW8624_HAPTIC_RTP_MODE);

	aw8624_i2c_write_bits(aw8624, AW8624_REG_DBGCTRL,
			      AW8624_BIT_DBGCTRL_INT_MODE_MASK,
			      AW8624_BIT_DBGCTRL_INT_MODE_EDGE);
	disable_irq(gpio_to_irq(aw8624->irq_gpio));
	/* haptic start */
	aw8624_haptic_start(aw8624);
	pm_qos_add_request(&aw8624_pm_qos_req_vb, PM_QOS_CPU_DMA_LATENCY,
							AW8624_PM_QOS_VALUE_VB);
	while (1) {
		if (!aw8624_haptic_rtp_get_fifo_afi(aw8624)) {
			aw_dev_info(aw8624->dev,
				"%s: haptic_rtp_get_fifo_afi, rtp_cnt= %d\n",
				__func__, aw8624->rtp_cnt);
			mutex_lock(&aw8624->rtp_lock);
			if ((aw8624->rtp_container->len - aw8624->rtp_cnt) <
			    (aw8624->ram.base_addr >> 2))
				buf_len = aw8624->rtp_container->len - aw8624->rtp_cnt;
			else
				buf_len = (aw8624->ram.base_addr >> 2);

			if (aw8624->rtp_cnt != aw8624->rtp_container->len) {
				if (aw8624->timeval_flags == 1) {
#ifdef KERNEL_VERSION_49
					do_gettimeofday(&aw8624->start);
#else
					aw8624->kstart = ktime_get();
#endif
					aw8624->timeval_flags = 0;
				}
				aw8624->rtpupdate_flag =
				    aw8624_i2c_writes(aw8624,
						AW8624_REG_RTP_DATA,
						&aw8624->rtp_container->data[aw8624->
						rtp_cnt], buf_len);
				aw8624->rtp_cnt += buf_len;
			}
			mutex_unlock(&aw8624->rtp_lock);
		}

		osc_int_state = aw8624_haptic_osc_read_int(aw8624);
		if (osc_int_state&AW8624_BIT_SYSINT_DONEI) {
#ifdef KERNEL_VERSION_49
			do_gettimeofday(&aw8624->end);
#else
			aw8624->kend = ktime_get();
#endif
			aw_dev_info(aw8624->dev,
				"%s vincent playback aw8624->rtp_cnt= %d\n",
				__func__, aw8624->rtp_cnt);
			break;
		}
#ifdef KERNEL_VERSION_49
		do_gettimeofday(&aw8624->end);
		aw8624->microsecond =
			(aw8624->end.tv_sec - aw8624->start.tv_sec)*1000000 +
			(aw8624->end.tv_usec - aw8624->start.tv_usec);

#else
		aw8624->kend = ktime_get();
		aw8624->microsecond = ktime_to_us(ktime_sub(aw8624->kend,
							aw8624->kstart));

#endif
		if (aw8624->microsecond > AW8624_OSC_CALIBRATION_T_LENGTH) {
			aw_dev_info(aw8624->dev,
				"%s:vincent time out aw8624->rtp_cnt %d,",
				__func__, aw8624->rtp_cnt);
			aw_dev_info(aw8624->dev,
				"osc_int_state %02x\n", osc_int_state);
			break;
		}
	}
	pm_qos_remove_request(&aw8624_pm_qos_req_vb);
	enable_irq(gpio_to_irq(aw8624->irq_gpio));

	aw8624->osc_cali_flag = 0;
#ifdef KERNEL_VERSION_49
		aw8624->microsecond =
			(aw8624->end.tv_sec - aw8624->start.tv_sec)*1000000 +
			(aw8624->end.tv_usec - aw8624->start.tv_usec);

#else
		aw8624->microsecond = ktime_to_us(ktime_sub(aw8624->kend,
							aw8624->kstart));
#endif
	/*calibration osc*/
	aw_dev_info(aw8624->dev,
		"%s awinic_microsecond:%ld\n", __func__, aw8624->microsecond);
	aw_dev_info(aw8624->dev, "%s exit\n", __func__);
	return 0;
}

static ssize_t aw8624_osc_cali_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif
	ssize_t len = 0;

	len +=
		snprintf(buf + len, PAGE_SIZE - len, "lra_calib_data=%d\n",
			aw8624->lra_calib_data);

	return len;
}

static ssize_t aw8624_osc_cali_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;
	mutex_lock(&aw8624->lock);
	/* osc calibration flag start,Other behaviors are forbidden */
	aw8624->osc_cali_run = 1;
	if (val == 1) {
		aw8624_haptic_upload_lra(aw8624, AW8624_HAPTIC_ZERO);
		aw8624_rtp_osc_calibration(aw8624);
		aw8624_rtp_trim_lra_calibration(aw8624);
	} else if (val == 2) {
		aw8624_haptic_upload_lra(aw8624, AW8624_HAPTIC_RTP_CALI_LRA);
		aw8624_rtp_osc_calibration(aw8624);
	} else {
		aw_dev_err(aw8624->dev,
			"%s input value out of range\n", __func__);
	}
	aw8624->osc_cali_run = 0;
	/* osc calibration flag end,Other behaviors are permitted */
	mutex_unlock(&aw8624->lock);

	return count;
}




static enum hrtimer_restart aw8624_vibrator_timer_func(struct hrtimer *timer)
{
	struct aw8624 *aw8624 = container_of(timer, struct aw8624, timer);

	aw_dev_info(aw8624->dev, "%s enter\n", __func__);
	aw8624->state = 0;
	schedule_work(&aw8624->vibrator_work);

	return HRTIMER_NORESTART;
}



static void aw8624_vibrator_work_routine(struct work_struct *work)
{
	struct aw8624 *aw8624 =
	container_of(work, struct aw8624, vibrator_work);

	aw_dev_info(aw8624->dev, "%s enter\n", __func__);

	mutex_lock(&aw8624->lock);

	aw8624_haptic_stop(aw8624);

	aw8624_haptic_upload_lra(aw8624, AW8624_HAPTIC_F0_CALI_LRA);

	if (aw8624->state) {
		if (aw8624->activate_mode == AW8624_HAPTIC_ACTIVATE_RAM_MODE) {
			aw8624_haptic_ram_vbat_comp(aw8624, true);
			aw8624_haptic_play_repeat_seq(aw8624, true);
	} else if (aw8624->activate_mode == AW8624_HAPTIC_ACTIVATE_CONT_MODE) {
		aw8624_haptic_cont(aw8624);
	} else {
		 /*other mode*/
	}
		/* run ms timer */
		hrtimer_start(&aw8624->timer,
			ktime_set(aw8624->duration / 1000,
			(aw8624->duration % 1000) * 1000000),
			HRTIMER_MODE_REL);
#ifdef CONFIG_PM_WAKELOCKS
		__pm_stay_awake(&aw8624->wk_lock);
#else
		wake_lock(&aw8624->wk_lock);
#endif
		aw8624->wk_lock_flag = 1;
	} else {
		if (aw8624->wk_lock_flag == 1) {
#ifdef CONFIG_PM_WAKELOCKS
			__pm_relax(&aw8624->wk_lock);
#else
			wake_unlock(&aw8624->wk_lock);
#endif
			aw8624->wk_lock_flag = 0;
		}
		/*aw8624_haptic_stop(aw8624);*/
	}

	mutex_unlock(&aw8624->lock);
}



/******************************************************
 *
 * irq
 *
 ******************************************************/
void aw8624_interrupt_setup(struct aw8624 *aw8624)
{
	unsigned char reg_val = 0;

	aw8624_i2c_read(aw8624, AW8624_REG_SYSINT, &reg_val);
	aw_dev_info(aw8624->dev, "%s: reg SYSINT=0x%x\n", __func__, reg_val);

	aw8624_i2c_write_bits(aw8624, AW8624_REG_DBGCTRL,
		AW8624_BIT_DBGCTRL_INT_MODE_MASK,
		AW8624_BIT_DBGCTRL_INT_MODE_EDGE);

	aw8624_i2c_write_bits(aw8624, AW8624_REG_SYSINTM,
		AW8624_BIT_SYSINTM_UVLO_MASK, AW8624_BIT_SYSINTM_UVLO_EN);
	aw8624_i2c_write_bits(aw8624, AW8624_REG_SYSINTM,
		AW8624_BIT_SYSINTM_OCD_MASK, AW8624_BIT_SYSINTM_OCD_EN);
	aw8624_i2c_write_bits(aw8624, AW8624_REG_SYSINTM,
		AW8624_BIT_SYSINTM_OT_MASK, AW8624_BIT_SYSINTM_OT_EN);
}

static ssize_t aw8624_gun_type_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif

	return snprintf(buf, PAGE_SIZE, "0x%02x\n", aw8624->gun_type);
}

static ssize_t aw8624_gun_type_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif

	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	aw_dev_dbg(aw8624->dev, "%s: value=%d\n", __func__, val);

	mutex_lock(&aw8624->lock);
	aw8624->gun_type = val;
	mutex_unlock(&aw8624->lock);
	return count;
}

static ssize_t aw8624_bullet_nr_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif

	return snprintf(buf, PAGE_SIZE, "0x%02x\n", aw8624->bullet_nr);
}

static ssize_t aw8624_bullet_nr_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif

	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	aw_dev_dbg(aw8624->dev, "%s: value=%d\n", __func__, val);

	mutex_lock(&aw8624->lock);
	aw8624->bullet_nr = val;
	mutex_unlock(&aw8624->lock);
	return count;
}

static ssize_t aw8624_trig_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len,
				"trig: trig_enable=%d, trig_edge=%d, trig_polar=%d, pos_sequence=%d, neg_sequence=%d\n",
				aw8624->trig.trig_enable,
				aw8624->trig.trig_edge,
				aw8624->trig.trig_polar,
				aw8624->trig.pos_sequence,
				aw8624->trig.neg_sequence);
	return len;

}

static ssize_t aw8624_trig_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif
	unsigned int databuf[5] = { 0 };

	if (!aw8624->ram_init) {
		aw_dev_err(aw8624->dev,
			   "%s: ram init failed, not allow to play!\n",
			   __func__);
		return count;
	}
	if (sscanf(buf, "%d %d %d %d %d",
		&databuf[0], &databuf[1],
		&databuf[2], &databuf[3], &databuf[4]) == 5) {
		if (databuf[0] > 1)
			databuf[0] = 1;
		if (databuf[0] < 0)
			databuf[0] = 0;
		if (databuf[1] > 1)
			databuf[0] = 1;
		if (databuf[1] < 0)
			databuf[0] = 0;
		if (databuf[2] > 1)
			databuf[0] = 1;
		if (databuf[2] < 0)
			databuf[0] = 0;
		if (databuf[3] > aw8624->ram.ram_num ||
		    databuf[4] > aw8624->ram.ram_num) {
			aw_dev_err(aw8624->dev,
				   "%s: input seq value out of range!\n",
				   __func__);
			return count;
		}
		aw8624->trig.trig_enable = databuf[0];
		aw8624->trig.trig_edge = databuf[1];
		aw8624->trig.trig_polar = databuf[2];
		aw8624->trig.pos_sequence = databuf[3];
		aw8624->trig.neg_sequence = databuf[4];
		mutex_lock(&aw8624->lock);
		aw8624_haptic_tirg1_param_config(aw8624);
		mutex_unlock(&aw8624->lock);
	} else
		aw_dev_err(aw8624->dev,
				   "%s: please input five parameters\n",
				   __func__);
	return count;
}

static ssize_t aw8624_ram_num_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(to_dev, struct aw8624, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8624 *aw8624 = container_of(cdev, struct aw8624, cdev);
#endif
	ssize_t len = 0;

	aw8624_haptic_get_ram_number(aw8624);

	len += snprintf(buf + len, PAGE_SIZE - len,
			"ram_num = %d\n", aw8624->ram.ram_num);
	return len;

}

#if 0
/*
*   schedule_work is low priority.
*   aw8624 rtp mode can`t be interrupted.
*/
static irqreturn_t aw8624_irq(int irq, void *data)
{
	struct aw8624 *aw8624 = (struct aw8624 *)data;

	schedule_work(&aw8624->irq_work);
	return IRQ_HANDLED;
}

#endif
/*****************************************************
 *
 * device tree
 *
 *****************************************************/
static DEVICE_ATTR(state, 0664, aw8624_state_show, aw8624_state_store);
static DEVICE_ATTR(duration, 0664, aw8624_duration_show, aw8624_duration_store);
static DEVICE_ATTR(activate, 0664, aw8624_activate_show, aw8624_activate_store);
static DEVICE_ATTR(activate_mode, 0664,
		aw8624_activate_mode_show, aw8624_activate_mode_store);
static DEVICE_ATTR(index, 0664, aw8624_index_show, aw8624_index_store);
static DEVICE_ATTR(gain, 0664, aw8624_gain_show, aw8624_gain_store);
static DEVICE_ATTR(seq, 0664, aw8624_seq_show, aw8624_seq_store);
static DEVICE_ATTR(loop, 0664, aw8624_loop_show, aw8624_loop_store);
static DEVICE_ATTR(register, 0664, aw8624_reg_show, aw8624_reg_store);
static DEVICE_ATTR(ram_update, 0664,
		aw8624_ram_update_show, aw8624_ram_update_store);
static DEVICE_ATTR(f0, 0664, aw8624_f0_show, aw8624_f0_store);
static DEVICE_ATTR(cali, 0664, aw8624_cali_show, aw8624_cali_store);
static DEVICE_ATTR(cont, 0664, aw8624_cont_show, aw8624_cont_store);
static DEVICE_ATTR(cont_td, 0664, aw8624_cont_td_show, aw8624_cont_td_store);
static DEVICE_ATTR(cont_drv, 0664, aw8624_cont_drv_show, aw8624_cont_drv_store);
static DEVICE_ATTR(cont_num_brk, 0664,
		aw8624_cont_num_brk_show, aw8624_cont_num_brk_store);
static DEVICE_ATTR(cont_zc_thr, 0664,
		aw8624_cont_zc_thr_show, aw8624_cont_zc_thr_store);
static DEVICE_ATTR(vbat_monitor, 0664,
		aw8624_vbat_monitor_show, aw8624_vbat_monitor_store);
static DEVICE_ATTR(lra_resistance, 0664,
		aw8624_lra_resistance_show, aw8624_lra_resistance_store);
static DEVICE_ATTR(prctmode, 0664, aw8624_prctmode_show, aw8624_prctmode_store);
static DEVICE_ATTR(haptic_audio, 0664,
		aw8624_haptic_audio_show, aw8624_haptic_audio_store);
static DEVICE_ATTR(haptic_audio_time, 0664,
		aw8624_haptic_audio_time_show, aw8624_haptic_audio_time_store);
static DEVICE_ATTR(ram_vbat_comp, 0664,
		aw8624_ram_vbat_comp_show, aw8624_ram_vbat_comp_store);
static DEVICE_ATTR(rtp, 0664, aw8624_rtp_show, aw8624_rtp_store);
static DEVICE_ATTR(osc_cali, 0664, aw8624_osc_cali_show, aw8624_osc_cali_store);
static DEVICE_ATTR(gun_type, 0664, aw8624_gun_type_show, aw8624_gun_type_store);
static DEVICE_ATTR(bullet_nr, 0664,
		aw8624_bullet_nr_show, aw8624_bullet_nr_store);
static DEVICE_ATTR(trig, 0664,
		aw8624_trig_show, aw8624_trig_store);
static DEVICE_ATTR(ram_num, 0664, aw8624_ram_num_show, NULL);
static struct attribute *aw8624_vibrator_attributes[] = {
	&dev_attr_state.attr,
	&dev_attr_duration.attr,
	&dev_attr_activate.attr,
	&dev_attr_activate_mode.attr,
	&dev_attr_index.attr,
	&dev_attr_gain.attr,
	&dev_attr_seq.attr,
	&dev_attr_loop.attr,
	&dev_attr_register.attr,
	&dev_attr_ram_update.attr,
	&dev_attr_f0.attr,
	&dev_attr_cali.attr,
	&dev_attr_cont.attr,
	&dev_attr_cont_td.attr,
	&dev_attr_cont_drv.attr,
	&dev_attr_cont_num_brk.attr,
	&dev_attr_cont_zc_thr.attr,
	&dev_attr_vbat_monitor.attr,
	&dev_attr_lra_resistance.attr,
	&dev_attr_prctmode.attr,
	&dev_attr_haptic_audio.attr,
	&dev_attr_ram_vbat_comp.attr,
	&dev_attr_haptic_audio_time.attr,
	&dev_attr_rtp.attr,
	&dev_attr_osc_cali.attr,
	&dev_attr_gun_type.attr,
	&dev_attr_bullet_nr.attr,
	&dev_attr_trig.attr,
	&dev_attr_ram_num.attr,
	NULL
};

struct attribute_group aw8624_vibrator_attribute_group = {
	.attrs = aw8624_vibrator_attributes
};

#ifdef AW_TIKTAP
static int aw8624_tiktap_rtp_play(struct aw8624 *aw8624)
{
	unsigned int buf_len = 0;
	unsigned char glb_state_val = 0;

	aw_dev_info(aw8624->dev, "%s enter\n", __func__);
	pm_qos_add_request(&aw8624_pm_qos_req_vb, PM_QOS_CPU_DMA_LATENCY,
			   AW8624_PM_QOS_VALUE_VB);
	aw8624->rtp_cnt = 0;
	while (true) {
		if ((aw8624->play_mode != AW8624_HAPTIC_RTP_MODE) ||
		    (aw8624->tiktap_stop_flag == true)) {
			return 0;
		}
		if (aw8624_haptic_rtp_get_fifo_afs(aw8624)) {
			mdelay(1);
			continue;
		}
		aw_dev_info(aw8624->dev, "%s tiktap rtp_cnt = %d\n", __func__,
			    aw8624->rtp_cnt);
		aw_dev_info(aw8624->dev, "%s tiktap_rtp->len = %d\n", __func__,
			    aw8624->tiktap_rtp->len);
		if (!aw8624->tiktap_rtp) {
			aw_dev_info(aw8624->dev,
				    "%s:tiktap_rtp is null, break!\n",
				    __func__);
			break;
		}
		if (aw8624->rtp_cnt < (aw8624->ram.base_addr)) {
			if ((aw8624->tiktap_rtp->len - aw8624->rtp_cnt) <
			    (aw8624->ram.base_addr)) {
				buf_len = aw8624->tiktap_rtp->len - aw8624->rtp_cnt;
			} else {
				buf_len = aw8624->ram.base_addr;
			}
		}
		else if ((aw8624->tiktap_rtp->len - aw8624->rtp_cnt) <
			 (aw8624->ram.base_addr >> 2)) {
			buf_len = aw8624->tiktap_rtp->len - aw8624->rtp_cnt;
		} else {
			buf_len = aw8624->ram.base_addr >> 2;
		}
		aw_dev_info(aw8624->dev, "%s buf_len = %d\n", __func__,
			    buf_len);
		aw8624_i2c_writes(aw8624, AW8624_REG_RTP_DATA,
				   &aw8624->tiktap_rtp->data[aw8624->rtp_cnt],
				   buf_len);
		aw8624->rtp_cnt += buf_len;
		aw8624_i2c_read(aw8624, AW8624_REG_GLB_STATE, &glb_state_val);
		if ((aw8624->rtp_cnt == aw8624->tiktap_rtp->len) || ((glb_state_val & 0x0f) == 0x00)) {
			if (aw8624->rtp_cnt == aw8624->tiktap_rtp->len)
				aw_dev_info(aw8624->dev, "%s: tiktap_rtp update complete!\n",
					    __func__);
			else
				aw_dev_err(aw8624->dev, "%s: tiktap_rtp play suspend!\n",
					   __func__);
			aw8624->rtp_cnt = 0;
			pm_qos_remove_request(&aw8624_pm_qos_req_vb);
			return 0;
		}
	}

	if (aw8624->play_mode == AW8624_HAPTIC_RTP_MODE)
		aw8624_haptic_set_rtp_aei(aw8624, false);

	aw_dev_info(aw8624->dev, "%s exit\n", __func__);
	pm_qos_remove_request(&aw8624_pm_qos_req_vb);
	return 0;
}

static void aw8624_rtp_work_tiktap(struct work_struct *work)
{
	unsigned int cnt = 200;
	unsigned char reg_val = 0;
	bool rtp_work_flag = false;
	struct aw8624 *aw8624 = container_of(work, struct aw8624, rtp_tiktap);
	aw_dev_info(aw8624->dev, "%s enter\n", __func__);

	mutex_lock(&aw8624->lock);
	aw8624_haptic_stop(aw8624);
	aw8624_haptic_play_mode(aw8624, AW8624_HAPTIC_RTP_MODE);
	aw8624_haptic_play_go(aw8624, true);
	//usleep_range(2000, 2500);
	while (cnt)
	{
		aw8624_i2c_read(aw8624, AW8624_REG_GLB_STATE, &reg_val);
		if ((reg_val & 0x0f) == 0x08) {
			cnt = 0;
			rtp_work_flag = true;
			aw_dev_info(aw8624->dev, "%s RTP_GO! glb_state=0x08\n",
				    __func__);
		} else {
			cnt--;
			aw_dev_dbg(aw8624->dev,
				   "%s wait for RTP_GO, glb_state=0x%02X\n",
				   __func__, reg_val);
		}
		usleep_range(1000, 1500);
	}
	if (!rtp_work_flag) {
		aw_dev_err(aw8624->dev, "%s failed to enter RTP_GO status!\n",
			   __func__);
		aw8624_haptic_stop(aw8624);
	}
	mutex_unlock(&aw8624->lock);

	mutex_lock(&aw8624->rtp_lock);
	if (aw8624->tiktap_stop_flag == false) {
		aw8624_tiktap_rtp_play(aw8624);
	}
	mutex_unlock(&aw8624->rtp_lock);
}

static void aw8624_rtp_irq_work_tiktap(struct work_struct *work)
{
	unsigned int cnt = 200;
	unsigned char reg_val = 0;
	bool rtp_work_flag = false;
	struct aw8624 *aw8624 = container_of(work, struct aw8624,
					     rtp_irq_tiktap);
	aw_dev_info(aw8624->dev, "%s enter\n", __func__);

	mutex_lock(&aw8624->lock);
	aw8624_haptic_stop(aw8624);
	aw8624_haptic_set_rtp_aei(aw8624, false);
	aw8624_interrupt_clear(aw8624);
	aw8624_haptic_play_mode(aw8624, AW8624_HAPTIC_RTP_MODE);
	aw8624_haptic_play_go(aw8624, true);
	//usleep_range(2000, 2500);
	while (cnt) {
		aw8624_i2c_read(aw8624, AW8624_REG_GLB_STATE, &reg_val);
		if ((reg_val & 0x0f) == 0x08) {
			cnt = 0;
			rtp_work_flag = true;
			aw_dev_info(aw8624->dev, "%s RTP_GO! glb_state=0x08\n",
				    __func__);
		} else {
			cnt--;
			aw_dev_dbg(aw8624->dev,
				   "%s wait for RTP_GO, glb_state=0x%02X\n",
				   __func__, reg_val);
		}
		usleep_range(1000, 1500);
	}
	if (!rtp_work_flag) {
		aw_dev_err(aw8624->dev, "%s failed to enter RTP_GO status!\n",
			   __func__);
		aw8624_haptic_stop(aw8624);
	}
	mutex_unlock(&aw8624->lock);

	if (aw8624->tiktap_stop_flag == false) {
		aw8624_haptic_rtp_init(aw8624);
	}
}

static long aw8624_tiktap_unlocked_ioctl(struct file *file, unsigned int cmd,
					unsigned long arg)
{
	struct aw8624 *aw8624 = g_aw8624;
	unsigned char reg_addr = 0;
	unsigned char reg_data = 0;
	int ret = 0;
	unsigned int tmp = 0;
	unsigned int len = 0;

	switch (cmd) {
	case TIKTAP_GET_HWINFO:
		aw_dev_info(aw8624->dev, "%s cmd = TIKTAP_GET_HWINFO!\n", __func__);
		tmp = aw8624->chipid;
		if (copy_to_user((void __user *)arg, &tmp, sizeof(unsigned int)))
			ret = -EFAULT;
		break;
	case TIKTAP_SETTING_GAIN:
		aw_dev_info(aw8624->dev, "%s cmd = TIKTAP_SETTING_GAIN!, arg = 0x%02lx\n", __func__, arg);
		mutex_lock(&aw8624->lock);
		aw8624->gain = arg;
		aw8624_haptic_set_gain(aw8624, aw8624->gain);
		mutex_unlock(&aw8624->lock);
		break;
	case TIKTAP_GET_F0:
		aw_dev_info(aw8624->dev, "%s cmd = TIKTAP_GET_F0!\n", __func__);
		tmp = aw8624->f0;
		if (copy_to_user((void __user *)arg, &tmp, sizeof(unsigned int)))
			ret = -EFAULT;
		break;
	case TIKTAP_WRITE_REG:
		aw_dev_info(aw8624->dev, "%s cmd = TIKTAP_WRITE_REG!\n", __func__);
		reg_addr = (arg & 0xFF00) >> 8;
		reg_data = arg & 0x00FF;
		aw8624_i2c_write(aw8624, reg_addr, reg_data);
		break;
	case TIKTAP_READ_REG:
		aw_dev_info(aw8624->dev, "%s cmd = TIKTAP_READ_REG!\n", __func__);
		if (copy_from_user(&reg_addr, (void __user *)arg, sizeof(unsigned char))) {
			ret = -EFAULT;
			break;
		}
		aw8624_i2c_read(aw8624, reg_addr, &reg_data);
		if (copy_to_user((void __user *)arg, &reg_data, sizeof(unsigned char)))
			ret = -EFAULT;
		break;
	case TIKTAP_STOP_RTP_MODE:
		aw_dev_info(aw8624->dev, "%s cmd = TIKTAP_STOP_MODE!\n", __func__);
		aw8624->tiktap_stop_flag = true;
		aw8624->rtp_file_num = 0;
		schedule_work(&aw8624->rtp_work);
		break;
	case TIKTAP_STOP_MODE:
		aw_dev_info(aw8624->dev, "%s cmd = TIKTAP_STOP_MODE!\n", __func__);
		aw8624->tiktap_stop_flag = true;
		mutex_lock(&aw8624->lock);
		aw8624_haptic_stop(aw8624);
		mutex_unlock(&aw8624->lock);
		break;
	case TIKTAP_ON_MODE:
		aw_dev_info(aw8624->dev, "%s cmd = TIKTAP_ON_MODE!, arg = %ld\n", __func__, arg);
		tmp = arg;
		vfree(aw8624->tiktap_rtp);
		aw8624->tiktap_rtp = NULL;
		aw8624->tiktap_rtp = vmalloc(tmp);
		if (aw8624->tiktap_rtp == NULL) {
			aw_dev_err(aw8624->dev, "%s malloc tiktap_rtp memory failed\n", __func__);
			return -ENOMEM;
		}
		break;
	case TIKTAP_OFF_MODE:
		aw_dev_info(aw8624->dev, "%s cmd = TIKTAP_OFF_MODE!\n", __func__);
		aw8624->tiktap_stop_flag = true;
		vfree(aw8624->tiktap_rtp);
		break;
	case TIKTAP_RTP_MODE:
		aw_dev_info(aw8624->dev, "%s cmd = TIKTAP_RTP_MODE!\n", __func__);
		if (aw8624->tiktap_rtp == NULL) {
			aw_dev_err(aw8624->dev, "%s tiktap = NULL!\n", __func__);
			ret = -EFAULT;
			break;
		}
		aw8624->tiktap_stop_flag = true;
		if(copy_from_user(&len, (void __user *)arg, sizeof(int))) {
			ret = -EFAULT;
			break;
		}
		aw_dev_info(aw8624->dev, "%s tiktap->len = %d\n", __func__, len);
		if(copy_from_user(aw8624->tiktap_rtp->data, (void __user *)arg , len + sizeof(int))) {
			ret = -EFAULT;
			break;
		}
		aw8624->tiktap_rtp->len = len;
		aw8624->tiktap_stop_flag = false;
		schedule_work(&aw8624->rtp_tiktap);
		break;
	case TIKTAP_SETTING_SPEED:
		aw_dev_info(aw8624->dev, "%s cmd = TIKTAP_SETTING_SPEED!, arg = 0x%02lx\n", __func__, arg);
		aw8624_haptic_set_pwm(aw8624, arg);
		break;
	case TIKTAP_GET_SPEED:
		aw_dev_info(aw8624->dev, "%s cmd = TIKTAP_GET_SPEED!\n", __func__);
		tmp = aw8624->pwm_mode;
		if (copy_to_user((void __user *)arg, &tmp, sizeof(unsigned int)))
			ret = -EFAULT;
		break;
	default:
		aw_dev_info(aw8624->dev, "%s unknown cmd = %d\n", __func__, cmd);
		break;
	}
	return ret;
}

static ssize_t aw_buf_write_proc(struct file *filp,
				 const char __user *buffer,
				 size_t count, loff_t *off)
{
	struct aw8624 *aw8624 = g_aw8624;
	aw_dev_info(aw8624->dev, "%s, write count %zu\n", __func__, count);

	if (aw8624->ram.base_addr == 0x0000) {
		aw_dev_info(aw8624->dev, "%s: no set FIFO space for rtp play!\n",
			    __func__);
		return count;
	}

	mutex_lock(&aw8624->rtp_lock);
	aw_dev_info(aw8624->dev, "%s, copy form user start...\n", __func__);
	aw8624->rtp_init = 0;
	vfree(aw8624->rtp_container);
	aw8624->rtp_container = vmalloc(count + sizeof(int));
	if (!aw8624->rtp_container) {
		aw_dev_err(aw8624->dev, "%s: error allocating memory\n",
			   __func__);
		mutex_unlock(&aw8624->rtp_lock);
		return -EFAULT;
	}
	aw8624->rtp_container->len = count;
	if (copy_from_user(aw8624->rtp_container->data, buffer, count)) {
		aw_dev_err(aw8624->dev, "Failed copy from user\n");
		mutex_unlock(&aw8624->rtp_lock);
		return -EFAULT;
	}
	aw8624->rtp_init = 1;
	aw_dev_info(aw8624->dev, "%s, copy form user end...\n", __func__);
	mutex_unlock(&aw8624->rtp_lock);
	aw8624->tiktap_stop_flag = false;
	schedule_work(&aw8624->rtp_irq_tiktap);
	return count;
}

static const struct file_operations config_proc_ops = {
    .owner = THIS_MODULE,
    .write = aw_buf_write_proc,
    .unlocked_ioctl = aw8624_tiktap_unlocked_ioctl,
};
#endif

int aw8624_vibrator_init(struct aw8624 *aw8624)
{
	int ret = 0;

	aw_dev_info(aw8624->dev, "%s enter\n", __func__);

#ifdef TIMED_OUTPUT
	aw8624->to_dev.name = "awinic_vibrator";
	aw8624->to_dev.get_time = aw8624_vibrator_get_time;
	aw8624->to_dev.enable = aw8624_vibrator_enable;

	ret = timed_output_dev_register(&(aw8624->to_dev));
	if (ret < 0) {
		dev_err(aw8624->dev, "%s: fail to create timed output dev\n",
			__func__);
		return ret;
	}
	ret = sysfs_create_group(&aw8624->to_dev.dev->kobj,
				&aw8624_vibrator_attribute_group);
	if (ret < 0) {
		dev_err(aw8624->dev,
			"%s error creating sysfs attr files\n",
			__func__);
		return ret;
	}
#else
	aw8624->cdev.name = "awinic_vibrator";
	aw8624->cdev.brightness_set = aw8624_vibrator_enable;


	ret = devm_led_classdev_register(&aw8624->i2c->dev, &(aw8624->cdev));
	if (ret < 0) {
		dev_err(aw8624->dev, "%s: fail to create leds dev\n",
				__func__);
		return ret;
	}

	ret = sysfs_create_group(&aw8624->cdev.dev->kobj,
				&aw8624_vibrator_attribute_group);
	if (ret < 0) {
		dev_err(aw8624->dev, "%s error creating sysfs attr files\n",
			__func__);
		return ret;
	}
#endif

#ifdef AW_TIKTAP
	aw8624->aw_config_proc = NULL;
	aw8624->aw_config_proc = proc_create(AW_TIKTAP_PROCNAME, 0666,
				     NULL, &config_proc_ops);
	if (aw8624->aw_config_proc == NULL)
		dev_err(aw8624->dev, "create_proc_entry %s failed\n",
			AW_TIKTAP_PROCNAME);
	else
		dev_info(aw8624->dev, "create proc entry %s success\n",
			 AW_TIKTAP_PROCNAME);
	INIT_WORK(&aw8624->rtp_irq_tiktap, aw8624_rtp_irq_work_tiktap);
	INIT_WORK(&aw8624->rtp_tiktap, aw8624_rtp_work_tiktap);
#endif

	hrtimer_init(&aw8624->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	aw8624->timer.function = aw8624_vibrator_timer_func;
	INIT_WORK(&aw8624->vibrator_work, aw8624_vibrator_work_routine);

	if (aw8624->IsUsedIRQ)
		INIT_WORK(&aw8624->rtp_work, aw8624_rtp_work_routine);

	mutex_init(&aw8624->lock);
	mutex_init(&aw8624->rtp_lock);

	return 0;
}


irqreturn_t aw8624_irq(int irq, void *data)
{
	struct aw8624 *aw8624 = data;
	unsigned char reg_val = 0;
	unsigned char glb_st = 0;
	unsigned int buf_len = 0;

	aw8624_i2c_read(aw8624, AW8624_REG_SYSINT, &reg_val);
	if (reg_val & AW8624_BIT_SYSINT_UVLI)
		aw_dev_err(aw8624->dev, "%s chip uvlo int error\n", __func__);
	if (reg_val & AW8624_BIT_SYSINT_OCDI)
		aw_dev_err(aw8624->dev, "%s chip over current int error\n",
			   __func__);
	if (reg_val & AW8624_BIT_SYSINT_OTI)
		aw_dev_err(aw8624->dev, "%s chip over temperature int error\n",
			   __func__);
	if (reg_val & AW8624_BIT_SYSINT_DONEI)
		aw_dev_info(aw8624->dev, "%s chip playback done\n", __func__);


	if (reg_val & AW8624_BIT_SYSINT_UVLI) {
		aw8624_i2c_read(aw8624, AW8624_REG_GLB_STATE, &glb_st);
		if (glb_st == 0) {
			aw8624_i2c_write_bits(aw8624,
					      AW8624_REG_SYSINTM,
					      AW8624_BIT_SYSINTM_UVLO_MASK,
					      AW8624_BIT_SYSINTM_UVLO_OFF);
		}
	}

	if (reg_val & AW8624_BIT_SYSINT_FF_AEI) {
		aw_dev_info(aw8624->dev, "%s: aw8624 rtp fifo almost empty\n",
			    __func__);
		if (aw8624->rtp_init) {
			while ((!aw8624_haptic_rtp_get_fifo_afs(aw8624)) &&
			(aw8624->play_mode == AW8624_HAPTIC_RTP_MODE)) {
				mutex_lock(&aw8624->rtp_lock);
				if (!aw8624->rtp_cnt) {
					aw_dev_info(aw8624->dev, "%s:aw8624->rtp_cnt is 0!\n",
						    __func__);
					mutex_unlock(&aw8624->rtp_lock);
					break;
				}
#ifdef AW_ENABLE_RTP_PRINT_LOG
				aw_dev_info(aw8624->dev,
					"%s: aw8624 rtp mode fifo update, cnt=%d\n",
					__func__, aw8624->rtp_cnt);
#endif
				if (!aw8624->rtp_container) {
					aw_dev_info(aw8624->dev,
						"%s:aw8624->rtp_container is null, break!\n",
						__func__);
					mutex_unlock(&aw8624->rtp_lock);
					break;
				}
				if ((aw8624->rtp_container->len-aw8624->rtp_cnt) <
				(aw8624->ram.base_addr>>3)) {
					buf_len =
					aw8624->rtp_container->len-aw8624->rtp_cnt;
				} else {
					buf_len = (aw8624->ram.base_addr>>3);
				}
				aw8624_i2c_writes(aw8624,
					AW8624_REG_RTP_DATA,
					&aw8624->rtp_container->data[aw8624->rtp_cnt],
					buf_len);
				aw8624->rtp_cnt += buf_len;
				aw8624_i2c_read(aw8624, AW8624_REG_GLB_STATE,
								&glb_st);
				if ((aw8624->rtp_cnt == aw8624->rtp_container->len) ||
						((glb_st & 0x0f) == 0x00)) {
					if (aw8624->rtp_cnt ==
						aw8624->rtp_container->len)
						aw_dev_info(aw8624->dev,
							"%s: rtp load completely! glb_st=%02x aw8624->rtp_cnt=%d\n",
							__func__, glb_st,
							aw8624->rtp_cnt);
					else
						aw_dev_err(aw8624->dev,
							"%s rtp load failed!! glb_st=%02x aw8624->rtp_cnt=%d\n",
							__func__, glb_st,
							aw8624->rtp_cnt);
					aw8624_haptic_set_rtp_aei(aw8624,
								false);
					aw8624->rtp_cnt = 0;
					aw8624->rtp_init = 0;
					mutex_unlock(&aw8624->rtp_lock);
					break;
				}
				mutex_unlock(&aw8624->rtp_lock);
			}
		} else {
			aw_dev_err(aw8624->dev,
				    "%s: aw8624 rtp init = %d, init error\n",
				    __func__, aw8624->rtp_init);
		}
	}

	if (reg_val & AW8624_BIT_SYSINT_FF_AFI)
		aw_dev_info(aw8624->dev,
			"%s: aw8624 rtp mode fifo full\n", __func__);

	if (aw8624->play_mode != AW8624_HAPTIC_RTP_MODE)
		aw8624_haptic_set_rtp_aei(aw8624, false);

	/*aw8624_i2c_read(aw8624, AW8624_REG_SYSINT, &reg_val);*/
	/*aw8624_i2c_read(aw8624, AW8624_REG_SYSST, &reg_val);*/

	return IRQ_HANDLED;
}

static int aw8624_analyse_duration_range(struct aw8624 *aw8624)
{
	int i = 0;
	int ret = 0;
	int len = 0;
	int *duration_time = NULL;

	len = ARRAY_SIZE(aw8624_dts_data.aw8624_duration_time);
	duration_time = aw8624_dts_data.aw8624_duration_time;
	if (len < 2) {
		aw_dev_err(aw8624->dev, "%s: duration time range error\n",
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
		aw_dev_err(aw8624->dev, "%s: duration time range error\n",
			__func__);
		ret = -ERANGE;
	}
	return ret;
}

static int
aw8624_analyse_duration_array_size(struct aw8624 *aw8624, struct device_node *np)
{
	int ret = 0;

	ret = of_property_count_elems_of_size(np,
			"aw8624_vib_duration_time", 4);
	if (ret < 0) {
		aw8624->duration_time_flag = -1;
		aw_dev_info(aw8624->dev,
			"%s vib_duration_time not found\n", __func__);
		return ret;
	}
	aw8624->duration_time_size = ret;
	if (aw8624->duration_time_size > 3) {
		aw8624->duration_time_flag = -1;
		aw_dev_info(aw8624->dev,
			"%s vib_duration_time error, array size = %d\n",
			__func__, aw8624->duration_time_size);
		return -ERANGE;
	}
	return 0;
}

int aw8624_parse_dt(struct aw8624 *aw8624, struct device *dev,
		struct device_node *np) {
	unsigned int val = 0;
	/*unsigned int brake_ram_config[24];*/
	unsigned int brake_cont_config[24];
	unsigned int f0_trace_parameter[4];
	unsigned int bemf_config[4];
	unsigned int duration_time[3];
	unsigned int sw_brake[2];
	unsigned int trig_config_temp[5];
	int ret = 0;

	val =
	of_property_read_u32(np, "aw8624_vib_mode",
			&aw8624_dts_data.aw8624_mode);
	if (val != 0)
		aw_dev_info(aw8624->dev, "aw8624_vib_mode not found\n");
	val =
	of_property_read_u32(np, "aw8624_vib_f0_pre",
			&aw8624_dts_data.aw8624_f0_pre);
	if (val != 0)
		aw_dev_info(aw8624->dev, "aw8624_vib_f0_pre not found\n");
	val =
	of_property_read_u32(np, "aw8624_vib_f0_cali_percen",
				&aw8624_dts_data.aw8624_f0_cali_percen);
	if (val != 0)
		aw_dev_info(aw8624->dev,
			"aw8624_vib_f0_cali_percen not found\n");
	val =
	of_property_read_u32(np, "aw8624_vib_cont_drv_lev",
				&aw8624_dts_data.aw8624_cont_drv_lvl);
	if (val != 0)
		aw_dev_info(aw8624->dev,
			"aw8624_vib_cont_drv_lev not found\n");
	val =
	of_property_read_u32(np, "aw8624_vib_cont_drv_lvl_ov",
				&aw8624_dts_data.aw8624_cont_drv_lvl_ov);
	if (val != 0)
		aw_dev_info(aw8624->dev,
			"aw8624_vib_cont_drv_lvl_ov not found\n");
	val =
	of_property_read_u32(np, "aw8624_vib_cont_td",
				&aw8624_dts_data.aw8624_cont_td);
	if (val != 0)
		aw_dev_info(aw8624->dev, "aw8624_vib_cont_td not found\n");
	val =
	of_property_read_u32(np, "aw8624_vib_cont_zc_thr",
				&aw8624_dts_data.aw8624_cont_zc_thr);
	if (val != 0)
		aw_dev_info(aw8624->dev, "aw8624_vib_cont_zc_thr not found\n");
	val =
	of_property_read_u32(np, "aw8624_vib_cont_num_brk",
				&aw8624_dts_data.aw8624_cont_num_brk);
	if (val != 0)
		aw_dev_info(aw8624->dev,
			"aw8624_vib_cont_num_brk not found\n");
	val =
	of_property_read_u32(np, "aw8624_vib_f0_coeff",
				&aw8624_dts_data.aw8624_f0_coeff);
	if (val != 0)
		aw_dev_info(aw8624->dev,
			"aw8624_vib_f0_coeff not found\n");
	val = of_property_read_u32_array(np, "aw8624_vib_brake_cont_config",
		brake_cont_config, ARRAY_SIZE(brake_cont_config));
	if (val != 0)
		aw_dev_info(aw8624->dev,
			"%s vib_brake_cont_config not found\n", __func__);
	memcpy(aw8624_dts_data.aw8624_cont_brake,
		brake_cont_config, sizeof(brake_cont_config));

	val = of_property_read_u32_array(np, "aw8624_vib_f0_trace_parameter",
		f0_trace_parameter, ARRAY_SIZE(f0_trace_parameter));
	if (val != 0)
		aw_dev_info(aw8624->dev,
			"%s vib_f0_trace_parameter not found\n", __func__);
	memcpy(aw8624_dts_data.aw8624_f0_trace_parameter,
		f0_trace_parameter, sizeof(f0_trace_parameter));

	val = of_property_read_u32_array(np, "aw8624_vib_bemf_config",
		bemf_config, ARRAY_SIZE(bemf_config));
	if (val != 0)
		aw_dev_info(aw8624->dev,
			"%s vib_bemf_config not found\n", __func__);
	memcpy(aw8624_dts_data.aw8624_bemf_config,
		bemf_config, sizeof(bemf_config));

	val =
	of_property_read_u32_array(np, "aw8624_vib_sw_brake",
		sw_brake, ARRAY_SIZE(sw_brake));
	if (val != 0)
		aw_dev_info(aw8624->dev,
			"%s vib_wavseq not found\n", __func__);
	memcpy(aw8624_dts_data.aw8624_sw_brake,
		sw_brake, sizeof(sw_brake));

	val = of_property_read_u32(np, "aw8624_vib_tset",
		&aw8624_dts_data.aw8624_tset);
	if (val != 0)
		aw_dev_info(aw8624->dev, "%s vib_tset not found\n", __func__);
	val = of_property_read_u32_array(np, "aw8624_vib_duration_time",
		duration_time, ARRAY_SIZE(duration_time));
	if (val != 0)
		aw_dev_info(aw8624->dev,
			"%s vib_duration_time not found\n", __func__);
	ret = aw8624_analyse_duration_array_size(aw8624, np);
	if (!ret)
		memcpy(aw8624_dts_data.aw8624_duration_time,
				duration_time, sizeof(duration_time));
	val =
	    of_property_read_u32_array(np,
				"aw8624_vib_trig_config",
				trig_config_temp,
				ARRAY_SIZE(trig_config_temp));
	if (val != 0)
		aw_dev_info(aw8624->dev, "%s vib_trig_config not found\n",
			    __func__);
	memcpy(aw8624_dts_data.trig_config, trig_config_temp,
	       sizeof(trig_config_temp));
	return 0;
}
