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
#ifndef __S5KGD2SM_OTP_CAM_CAL_H__
#define __S5KGD2SM_OTP_CAM_CAL_H__
#include <linux/i2c.h>

unsigned int s5kgd2sm_selective_read_region(struct i2c_client *client, unsigned int addr,
	unsigned char *data, unsigned int size);

#endif /* __CAM_CAL_H */

