// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/backlight.h>
#include <linux/gpio/consumer.h>

#include "ktz8866.h"

/*****************************************************************************
 * GLobal Variable
 *****************************************************************************/
struct ktz8866 {
	struct device *dev;
	struct i2c_client *ktz8866_i2c;
	bool pwm_en;
	bool bias_en;
};

static struct ktz8866 *ktz8866_left;
static struct ktz8866 *ktz8866_right;
static DEFINE_MUTEX(read_lock);
/*****************************************************************************
 * Function Prototype
 *****************************************************************************/

/*****************************************************************************
 * Extern Area
 *****************************************************************************/

static int lcd_bl_write_byte(struct i2c_client *i2c, unsigned char addr, unsigned char value)
{
	int ret = 0;
	unsigned char write_data[2] = {0};

	write_data[0] = addr;
	write_data[1] = value;

	ret = i2c_master_send(i2c, write_data, 2);
	if (ret < 0)
		pr_info("%s i2c write data fail !!\n", __func__);

	return ret;
}

static int lcd_bl_read_byte(struct i2c_client *i2c, u8 regnum)
{
	u8 buffer[1], reg_value[1];
	int res = 0;

	mutex_lock(&read_lock);

	buffer[0] = regnum;

	res = i2c_master_send(i2c, buffer, 0x1);
	if (res <= 0)	{
		mutex_unlock(&read_lock);
		pr_info("read reg send res = %d\n", res);
		return res;
	}

	res = i2c_master_recv(i2c, reg_value, 0x1);
	if (res <= 0)	{
		mutex_unlock(&read_lock);
		pr_info("read reg send res = %d\n", res);
		return res;
	}
	pr_info("ktz8866 read left=0x%x\n",reg_value[0]);

	mutex_unlock(&read_lock);

	return reg_value[0];
}

int lcd_bl_set_led_brightness(int value)
{
	pr_info("%s:hyper bl = %d\n", __func__, value);

	if (value < 0) {
		pr_info("%s: invalid value=%d\n", __func__, value);
		return 0;
	}

	if (ktz8866_left && ktz8866_left->pwm_en) {
		if (value > 0) {
			pr_info("%s:left bl = %d\n", __func__, value);
			lcd_bl_write_byte(ktz8866_left->ktz8866_i2c, 0x02, 0xDA);
			lcd_bl_write_byte(ktz8866_left->ktz8866_i2c, 0x11, 0x37);
			lcd_bl_write_byte(ktz8866_left->ktz8866_i2c, 0x15, 0xA0);
			lcd_bl_write_byte(ktz8866_left->ktz8866_i2c, 0x08, 0x4F);
			lcd_bl_write_byte(ktz8866_left->ktz8866_i2c, 0x04, value & 0x07);
			lcd_bl_write_byte(ktz8866_left->ktz8866_i2c, 0x05, (value >> 3) & 0xFF);
		} else {
			pr_info("%s:left bl = %d\n", __func__, value);
			lcd_bl_write_byte(ktz8866_left->ktz8866_i2c, 0x04, 0x00);
			lcd_bl_write_byte(ktz8866_left->ktz8866_i2c, 0x05, 0x00);
		}
	}

	if (ktz8866_right && ktz8866_right->pwm_en) {
		if (value > 0) {
			pr_info("%s:right bl = %d\n", __func__, value);
			lcd_bl_write_byte(ktz8866_right->ktz8866_i2c, 0x02, 0xDA);
			lcd_bl_write_byte(ktz8866_right->ktz8866_i2c, 0x11, 0x37);
			lcd_bl_write_byte(ktz8866_right->ktz8866_i2c, 0x15, 0xA0);
			lcd_bl_write_byte(ktz8866_right->ktz8866_i2c, 0x08, 0x4F);
			lcd_bl_write_byte(ktz8866_right->ktz8866_i2c, 0x04, value & 0x07);
			lcd_bl_write_byte(ktz8866_right->ktz8866_i2c, 0x05, (value >> 3) & 0xFF);
		} else {
			pr_info("%s:right bl = %d\n", __func__, value);
			lcd_bl_write_byte(ktz8866_right->ktz8866_i2c, 0x04, 0x00);
			lcd_bl_write_byte(ktz8866_right->ktz8866_i2c, 0x05, 0x00);
		}
	}

	return 0;
}
EXPORT_SYMBOL(lcd_bl_set_led_brightness);

int lcd_set_bias(int enable)
{
	pr_info("%s,value = %d", __func__, enable);
	if (ktz8866_left && ktz8866_left->bias_en) {
		if (enable) {
			pr_info("%s:left en = %d\n", __func__, enable);
			lcd_bl_write_byte(ktz8866_left->ktz8866_i2c, 0x0C, 0x2E);
			lcd_bl_write_byte(ktz8866_left->ktz8866_i2c, 0x0D, 0x26);
			lcd_bl_write_byte(ktz8866_left->ktz8866_i2c, 0x0E, 0x26);

			lcd_bl_write_byte(ktz8866_left->ktz8866_i2c, 0x09, 0x9C);
			mdelay(5);
			lcd_bl_write_byte(ktz8866_left->ktz8866_i2c, 0x09, 0x9E);
		} else {
			pr_info("%s:left en = %d\n", __func__, enable);
			lcd_bl_write_byte(ktz8866_left->ktz8866_i2c, 0x09, 0x9C);
			mdelay(5);
			lcd_bl_write_byte(ktz8866_left->ktz8866_i2c, 0x09, 0x98);
		}
	}

	if (ktz8866_right && ktz8866_right->bias_en) {
		if (enable) {
			pr_info("%s:right en = %d\n", __func__, enable);
			lcd_bl_write_byte(ktz8866_right->ktz8866_i2c, 0x0C, 0x2E);
			lcd_bl_write_byte(ktz8866_right->ktz8866_i2c, 0x0D, 0x26);
			lcd_bl_write_byte(ktz8866_right->ktz8866_i2c, 0x0E, 0x26);

			lcd_bl_write_byte(ktz8866_right->ktz8866_i2c, 0x09, 0x9C);
			mdelay(5); /* delay 5ms */
			lcd_bl_write_byte(ktz8866_right->ktz8866_i2c, 0x09, 0x9E);
		} else {
			pr_info("%s:right en = %d\n", __func__, enable);
			lcd_bl_write_byte(ktz8866_right->ktz8866_i2c, 0x09, 0x9C);
			mdelay(5);
			lcd_bl_write_byte(ktz8866_right->ktz8866_i2c, 0x09, 0x98);
		}
	}

	return 0;
}
EXPORT_SYMBOL(lcd_set_bias);

static int ktz8866_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0;
	struct device *dev = &client->dev;
	struct ktz8866 *ktz8866_client;

	if (!client) {
		pr_info("%s i2c_client is NULL\n", __func__);
		return -EINVAL;
	}

	pr_info("%s, i2c address: %0x\n", __func__, client->addr);

	ktz8866_client = devm_kzalloc(dev, sizeof(*ktz8866_client), GFP_KERNEL);
	if (!ktz8866_client)
		return -ENOMEM;
	ktz8866_client->ktz8866_i2c = client;

	ktz8866_client->bias_en = of_property_read_bool(dev->of_node,
						  "ktz8866,bias-en");
	ktz8866_client->pwm_en = of_property_read_bool(dev->of_node,
						  "ktz8866,pwm-en");

	if (!ktz8866_left) {
		pr_info("probe for left\n");
		ktz8866_left = ktz8866_client;
	} else if (!ktz8866_right) {
		pr_info("probe for right\n");
		ktz8866_right = ktz8866_client;
	}

#ifdef CONFIG_MTK_DISP_NO_LK
	//8866 is initial in lk when have lk, we connot touch it in probe
	if (ktz8866_client->bias_en) {
		//write vsp/vsn reg
		pr_info("%s:bias en\n", __func__);
		ret = lcd_bl_write_byte(ktz8866_client->ktz8866_i2c, 0x0C, 0x2E);
		ret = lcd_bl_write_byte(ktz8866_client->ktz8866_i2c, 0x0D, 0x26);
		ret = lcd_bl_write_byte(ktz8866_client->ktz8866_i2c, 0x0E, 0x26);

		ret = lcd_bl_write_byte(ktz8866_client->ktz8866_i2c, 0x09, 0x9C);
		mdelay(5); /* delay 5ms */
		ret = lcd_bl_write_byte(ktz8866_client->ktz8866_i2c, 0x09, 0x9E);
	}
	//write backlight reg
	if (ktz8866_client->pwm_en) {
		pr_info("%s:pwm en\n", __func__);
		ret = lcd_bl_write_byte(ktz8866_client->ktz8866_i2c, 0x02, 0xDA);
		ret = lcd_bl_write_byte(ktz8866_client->ktz8866_i2c, 0x11, 0x37);
		ret = lcd_bl_write_byte(ktz8866_client->ktz8866_i2c, 0x15, 0xA0);
		ret = lcd_bl_write_byte(ktz8866_client->ktz8866_i2c, 0x08, 0x4F);
	}
#endif

	if (ret < 0)
		pr_info("[%s]:I2C write reg is fail!", __func__);
	else
		pr_info("[%s]:I2C write reg is success!", __func__);

	return ret;
}

static int ktz8866_remove(struct i2c_client *client)
{
	ktz8866_left = NULL;
	ktz8866_right = NULL;
	i2c_unregister_device(client);

	return 0;
}

static const struct i2c_device_id ktz8866_i2c_table[] = {
	{"ktz8866", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, ktz8866_i2c_table);

static const struct of_device_id ktz8866_match[] = {
	{ .compatible = "ktz,ktz8866" },
	{},
};
MODULE_DEVICE_TABLE(of, it6151_match);

static struct i2c_driver mtz8866_driver = {
	.id_table	= ktz8866_i2c_table,
	.probe		= ktz8866_probe,
	.remove		= ktz8866_remove,
	.driver		= {
		.name	= "ktz,ktz8866",
		.of_match_table = ktz8866_match,
	},
};
module_i2c_driver(mtz8866_driver);

MODULE_AUTHOR("henry tu <henry.tu@mediatek.com>");
MODULE_DESCRIPTION("Mediatek ktz8866 Driver");
MODULE_LICENSE("GPL");



