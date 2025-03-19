// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Awinic Inc.
 */
#ifndef _HAPTIC_H_
#define _HAPTIC_H_
#include <linux/regmap.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/syscalls.h>


/*********************************************************
*
* marco
*
********************************************************/
#define AW_CHECK_RAM_DATA
#define AW_READ_BIN_FLEXBALLY
#define AW_OSC_COARSE_CALI
/* #define AW_ENABLE_RTP_PRINT_LOG */
#define AWINIC_DEV_NAME	("awinic_vibrator")

/********************************************
 * print information control
 *******************************************/
#define aw_dev_err(dev, format, ...) \
			pr_err("[%s]" format, dev_name(dev), ##__VA_ARGS__)

#define aw_dev_info(dev, format, ...) \
			pr_info("[%s]" format, dev_name(dev), ##__VA_ARGS__)

#define aw_dev_dbg(dev, format, ...) \
			pr_debug("[%s]" format, dev_name(dev), ##__VA_ARGS__)


#define AW_TIKTAP
#define AW_TIKTAP_PROCNAME "tiktap_buf"

#define TIKTAP_IOCTL_GROUP         0xFF
#define TIKTAP_GET_F0              _IO(TIKTAP_IOCTL_GROUP, 0x01)
#define TIKTAP_GET_HWINFO          _IO(TIKTAP_IOCTL_GROUP, 0x02)
#define TIKTAP_SET_FREQ            _IO(TIKTAP_IOCTL_GROUP, 0x03)
#define TIKTAP_SETTING_GAIN        _IO(TIKTAP_IOCTL_GROUP, 0x04)
#define TIKTAP_SETTING_SPEED       _IO(TIKTAP_IOCTL_GROUP, 0x05)
#define TIKTAP_SETTING_BSTVOL      _IO(TIKTAP_IOCTL_GROUP, 0x06)
#define TIKTAP_ON_MODE             _IO(TIKTAP_IOCTL_GROUP, 0x07)
#define TIKTAP_OFF_MODE            _IO(TIKTAP_IOCTL_GROUP, 0x08)
#define TIKTAP_RTP_MODE            _IO(TIKTAP_IOCTL_GROUP, 0x09)
#define TIKTAP_RTP_IRQ_MODE        _IO(TIKTAP_IOCTL_GROUP, 0x0A)
#define TIKTAP_STOP_MODE           _IO(TIKTAP_IOCTL_GROUP, 0x0B)
#define TIKTAP_STOP_RTP_MODE       _IO(TIKTAP_IOCTL_GROUP, 0x0C)
#define TIKTAP_WRITE_REG           _IO(TIKTAP_IOCTL_GROUP, 0x0D)
#define TIKTAP_READ_REG            _IO(TIKTAP_IOCTL_GROUP, 0x0E)
#define TIKTAP_BST_SWITCH          _IO(TIKTAP_IOCTL_GROUP, 0x0F)
#define TIKTAP_GET_SPEED           _IO(TIKTAP_IOCTL_GROUP, 0x10)

enum awinic_chip_name {
	AW86223 = 0,
	AW86224_5 = 1,
	AW8624 = 2,
};

/*awinic*/
struct awinic {
	struct i2c_client *i2c;
	struct device *dev;
	unsigned char name;
	bool IsUsedIRQ;

	unsigned int aw8622x_i2c_addr;
	int reset_gpio;
	int irq_gpio;
	int reset_gpio_ret;
	int irq_gpio_ret;

	struct aw8624 *aw8624;
	struct aw8622x *aw8622x;
};


struct ram {
	unsigned int len;
	unsigned int check_sum;
	unsigned int base_addr;
	unsigned char version;
	unsigned char ram_shift;
	unsigned char baseaddr_shift;
	unsigned char ram_num;
};

struct haptic_ctr {
	unsigned char cnt;
	unsigned char cmd;
	unsigned char play;
	unsigned char wavseq;
	unsigned char loop;
	unsigned char gain;
	struct list_head list;
};

struct haptic_audio {
	struct mutex lock;
	struct hrtimer timer;
	struct work_struct work;
	int delay_val;
	int timer_val;
	struct haptic_ctr ctr;
	struct list_head ctr_list;
};

extern struct aw8624 *g_aw8624;
extern struct aw8622x *g_aw8622x;
#endif
