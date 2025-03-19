/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 MediaTek Inc.
 */

#ifndef __AW883XX_EXT_H__
#define __AW883XX_EXT_H__
#include <linux/i2c.h>

extern int aw883xx_i2c_probe(struct i2c_client *i2c,
				const struct i2c_device_id *id);
extern int aw883xx_i2c_remove(struct i2c_client *i2c);

#endif /* __AW883XX_EXT_H__ */
