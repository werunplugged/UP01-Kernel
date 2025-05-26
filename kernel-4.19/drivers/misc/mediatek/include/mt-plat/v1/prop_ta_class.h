/*
 * Proprietary TA Class Header
 *
 * Copyright (C) 2018, Richtek Technology Corp.
 * Author: CY Hunag <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_PROP_TA_CLASS_H__
#define __LINUX_PROP_TA_CLASS_H__

struct prop_ta_classdev;

struct prop_ta_device_ops {
	int (*get_verinfo)(struct prop_ta_classdev *ptc,
			   u8 *info, u32 length);
	int (*get_fwcode)(struct prop_ta_classdev *ptc,
			  u8 *code, u32 length);
	int (*get_datecode)(struct prop_ta_classdev *ptc,
			    u8 *code, u32 length);
	int (*get_min_voltage)(struct prop_ta_classdev *ptc, u32 *volt);
	int (*get_max_voltage)(struct prop_ta_classdev *ptc, u32 *volt);
	int (*get_min_current)(struct prop_ta_classdev *ptc, u32 *curr);
	int (*get_max_current)(struct prop_ta_classdev *ptc, u32 *curr);
	int (*get_output_voltage)(struct prop_ta_classdev *ptc, u32 *volt);
	int (*get_output_current)(struct prop_ta_classdev *ptc, u32 *curr);
	int (*get_status)(struct prop_ta_classdev *ptc, u32 *status);
	int (*get_temp1)(struct prop_ta_classdev *ptc, u32 *degree);
	int (*get_temp2)(struct prop_ta_classdev *ptc, u32 *degree);
	int (*set_output_control)(struct prop_ta_classdev *ptc,
				  u32 control);
	int (*set_mode)(struct prop_ta_classdev *ptc, u32 mode);
	int (*set_voltage)(struct prop_ta_classdev *ptc, u32 volt);
	int (*set_current)(struct prop_ta_classdev *ptc, u32 curr);
	int (*suspend)(struct prop_ta_classdev *ptc);
	int (*resume)(struct prop_ta_classdev *ptc);
	int (*set_voltage_current)(struct prop_ta_classdev *ptc, u32 volt,
				   u32 curr);
	int (*get_output_voltage_current)(struct prop_ta_classdev *ptc,
					  u32 *volt, u32 *curr);
	int (*authentication)(struct prop_ta_classdev *ptc);
	int (*enable_wdt)(struct prop_ta_classdev *ptc, bool en);
	int (*set_wdt)(struct prop_ta_classdev *ptc, u32 ms);
};

struct prop_ta_classdev {
	const char *name;
	struct device *dev;
	const struct prop_ta_device_ops *ops;
	const struct attribute_group	**groups;
};

/* API List */
extern int prop_ta_device_get_verinfo(struct prop_ta_classdev *ptc,
				      u8 *info, u32 length);
extern int prop_ta_device_get_fwcode(struct prop_ta_classdev *ptc,
				     u8 *code, u32 length);
extern int prop_ta_device_get_datecode(struct prop_ta_classdev *ptc,
				       u8 *code, u32 length);
extern int prop_ta_device_get_min_voltage(struct prop_ta_classdev *ptc,
					  u32 *volt);
extern int prop_ta_device_get_max_voltage(struct prop_ta_classdev *ptc,
					  u32 *volt);
extern int prop_ta_device_get_min_current(struct prop_ta_classdev *ptc,
					  u32 *curr);
extern int prop_ta_device_get_max_current(struct prop_ta_classdev *ptc,
					  u32 *curr);
extern int prop_ta_device_get_output_voltage(struct prop_ta_classdev *ptc,
					     u32 *volt);
extern int prop_ta_device_get_output_current(struct prop_ta_classdev *ptc,
					     u32 *curr);
extern int prop_ta_device_get_status(struct prop_ta_classdev *ptc, u32 *status);
extern int prop_ta_device_get_temp1(struct prop_ta_classdev *ptc, u32 *degree);
extern int prop_ta_device_get_temp2(struct prop_ta_classdev *ptc, u32 *degree);
extern int prop_ta_device_set_output_control(struct prop_ta_classdev *ptc,
					     u32 control);
extern int prop_ta_device_set_mode(struct prop_ta_classdev *ptc, u32 mode);
extern int prop_ta_device_set_voltage(struct prop_ta_classdev *ptc, u32 volt);
extern int prop_ta_device_set_current(struct prop_ta_classdev *ptc, u32 curr);
extern int prop_ta_device_set_voltage_current(struct prop_ta_classdev *ptc,
					      u32 volt, u32 curr);
extern int prop_ta_device_get_output_voltage_current(
	struct prop_ta_classdev *ptc, u32 *volt, u32 *curr);
extern int prop_ta_device_authentication(struct prop_ta_classdev *ptc);
extern int prop_ta_device_enable_wdt(struct prop_ta_classdev *ptc, bool en);
extern int prop_ta_device_set_wdt(struct prop_ta_classdev *ptc, u32 ms);

extern int prop_ta_classdev_register(struct device *parent,
				     struct prop_ta_classdev *ptc);
extern void prop_ta_classdev_unregister(struct prop_ta_classdev *ptc);
extern int devm_prop_ta_classdev_register(struct device *parent,
					  struct prop_ta_classdev *ptc);
extern struct prop_ta_classdev *prop_ta_device_get_by_name(const char *name);

#endif /* __LINUX_PROP_TA_CLASS_H__ */
