/*
 * Proprietary TA Class Driver
 *
 * Copyright (C) 2018, Richtek Technology Corp.
 * Author: CY Hunag <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include <mt-plat/v1/prop_ta_class.h>

static struct class *prop_ta_class;

int prop_ta_device_get_verinfo(struct prop_ta_classdev *ptc, u8 *info,
			       u32 length)
{
	return (ptc->ops && ptc->ops->get_verinfo)
		? ptc->ops->get_verinfo(ptc, info, length) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(prop_ta_device_get_verinfo);

int prop_ta_device_get_fwcode(struct prop_ta_classdev *ptc, u8 *code,
			      u32 length)
{
	return (ptc->ops && ptc->ops->get_fwcode)
		? ptc->ops->get_fwcode(ptc, code, length) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(prop_ta_device_get_fwcode);

int prop_ta_device_get_datecode(struct prop_ta_classdev *ptc, u8 *code,
				u32 length)
{
	return (ptc->ops && ptc->ops->get_datecode)
		? ptc->ops->get_datecode(ptc, code, length) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(prop_ta_device_get_datecode);

int prop_ta_device_get_min_voltage(struct prop_ta_classdev *ptc, u32 *volt)
{
	return (ptc->ops && ptc->ops->get_min_voltage)
		? ptc->ops->get_min_voltage(ptc, volt) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(prop_ta_device_get_min_voltage);

int prop_ta_device_get_max_voltage(struct prop_ta_classdev *ptc, u32 *volt)
{
	return (ptc->ops && ptc->ops->get_max_voltage)
		? ptc->ops->get_max_voltage(ptc, volt) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(prop_ta_device_get_max_voltage);

int prop_ta_device_get_min_current(struct prop_ta_classdev *ptc, u32 *curr)
{
	return (ptc->ops && ptc->ops->get_min_current)
		? ptc->ops->get_min_current(ptc, curr) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(prop_ta_device_get_min_current);

int prop_ta_device_get_max_current(struct prop_ta_classdev *ptc, u32 *curr)
{
	return (ptc->ops && ptc->ops->get_max_current)
		? ptc->ops->get_max_current(ptc, curr) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(prop_ta_device_get_max_current);

int prop_ta_device_get_output_voltage(struct prop_ta_classdev *ptc, u32 *volt)
{
	return (ptc->ops && ptc->ops->get_output_voltage)
		? ptc->ops->get_output_voltage(ptc, volt) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(prop_ta_device_get_output_voltage);

int prop_ta_device_get_output_current(struct prop_ta_classdev *ptc, u32 *curr)
{
	return (ptc->ops && ptc->ops->get_output_current)
		? ptc->ops->get_output_current(ptc, curr) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(prop_ta_device_get_output_current);

int prop_ta_device_get_status(struct prop_ta_classdev *ptc, u32 *status)
{
	return (ptc->ops && ptc->ops->get_status)
		? ptc->ops->get_status(ptc, status) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(prop_ta_device_get_status);

int prop_ta_device_get_temp1(struct prop_ta_classdev *ptc, u32 *degree)
{
	return (ptc->ops && ptc->ops->get_temp1)
		? ptc->ops->get_temp1(ptc, degree) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(prop_ta_device_get_temp1);

int prop_ta_device_get_temp2(struct prop_ta_classdev *ptc, u32 *degree)
{
	return (ptc->ops && ptc->ops->get_temp2)
		? ptc->ops->get_temp2(ptc, degree) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(prop_ta_device_get_temp2);

int prop_ta_device_set_output_control(struct prop_ta_classdev *ptc, u32 control)
{
	return (ptc->ops && ptc->ops->set_output_control)
		? ptc->ops->set_output_control(ptc, control) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(prop_ta_device_set_output_control);

int prop_ta_device_set_mode(struct prop_ta_classdev *ptc, u32 mode)
{
	return (ptc->ops && ptc->ops->set_mode)
		? ptc->ops->set_mode(ptc, mode) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(prop_ta_device_set_mode);

int prop_ta_device_set_voltage(struct prop_ta_classdev *ptc, u32 volt)
{
	return (ptc->ops && ptc->ops->set_voltage)
		? ptc->ops->set_voltage(ptc, volt) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(prop_ta_device_set_voltage);

int prop_ta_device_set_current(struct prop_ta_classdev *ptc, u32 curr)
{
	return (ptc->ops && ptc->ops->set_current)
		? ptc->ops->set_current(ptc, curr) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(prop_ta_device_set_current);

int prop_ta_device_set_voltage_current(struct prop_ta_classdev *ptc, u32 volt,
				       u32 curr)
{
	return (ptc->ops && ptc->ops->set_voltage_current)
		? ptc->ops->set_voltage_current(ptc, volt, curr) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(prop_ta_device_set_voltage_current);

int prop_ta_device_get_output_voltage_current(struct prop_ta_classdev *ptc,
					      u32 *volt, u32 *curr)
{
	return (ptc->ops && ptc->ops->get_output_voltage_current)
		? ptc->ops->get_output_voltage_current(ptc, volt, curr)
		: -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(prop_ta_device_get_output_voltage_current);

int prop_ta_device_authentication(struct prop_ta_classdev *ptc)
{
	return (ptc->ops && ptc->ops->authentication)
		? ptc->ops->authentication(ptc) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(prop_ta_device_authentication);

int prop_ta_device_enable_wdt(struct prop_ta_classdev *ptc, bool en)
{
	return (ptc->ops && ptc->ops->enable_wdt)
		? ptc->ops->enable_wdt(ptc, en) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(prop_ta_device_enable_wdt);

int prop_ta_device_set_wdt(struct prop_ta_classdev *ptc, u32 ms)
{
	return (ptc->ops && ptc->ops->set_wdt)
		? ptc->ops->set_wdt(ptc, ms) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(prop_ta_device_set_wdt);

#ifdef CONFIG_PM_SLEEP
static int prop_ta_classdev_suspend(struct device *dev)
{
	struct prop_ta_classdev *ptc = dev_get_drvdata(dev);

	return (ptc->ops && ptc->ops->suspend) ? ptc->ops->suspend(ptc) : 0;
}

static int prop_ta_classdev_resume(struct device *dev)
{
	struct prop_ta_classdev *ptc = dev_get_drvdata(dev);

	return (ptc->ops && ptc->ops->resume) ? ptc->ops->resume(ptc) : 0;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(prop_ta_class_pm_ops,
			 prop_ta_classdev_suspend, prop_ta_classdev_resume);

int prop_ta_classdev_register(struct device *parent,
			      struct prop_ta_classdev *ptc)
{
	ptc->dev = device_create_with_groups(prop_ta_class, parent, 0,
					      ptc, ptc->groups, "%s",
					      ptc->name);
	if (IS_ERR(ptc->dev))
		return PTR_ERR(ptc->dev);
	return 0;
}
EXPORT_SYMBOL_GPL(prop_ta_classdev_register);

void prop_ta_classdev_unregister(struct prop_ta_classdev *ptc)
{
	device_unregister(ptc->dev);
}
EXPORT_SYMBOL_GPL(prop_ta_classdev_unregister);

static void devm_prop_ta_classdev_release(struct device *dev, void *res)
{
	prop_ta_classdev_unregister(*(struct prop_ta_classdev **)res);
}

int devm_prop_ta_classdev_register(struct device *parent,
				   struct prop_ta_classdev *ptc)
{
	struct prop_ta_classdev **pptc;
	int rc;

	pptc = devres_alloc(devm_prop_ta_classdev_release,
			    sizeof(*pptc), GFP_KERNEL);
	if (!pptc)
		return -ENOMEM;
	rc = prop_ta_classdev_register(parent, ptc);
	if (rc < 0) {
		devres_free(pptc);
		return rc;
	}
	*pptc = ptc;
	devres_add(parent, pptc);
	return 0;
}
EXPORT_SYMBOL_GPL(devm_prop_ta_classdev_register);

static int prop_ta_match_device_by_name(struct device *dev, const void *data)
{
	const char *name = data;
	struct prop_ta_classdev *ptc = dev_get_drvdata(dev);

	return strcmp(ptc->name, name) == 0;
}

struct prop_ta_classdev *prop_ta_device_get_by_name(const char *name)
{
	struct prop_ta_classdev *ptc = NULL;
	struct device *dev = class_find_device(prop_ta_class, NULL, name,
						  prop_ta_match_device_by_name);

	if (dev)
		ptc = dev_get_drvdata(dev);
	return ptc;
}
EXPORT_SYMBOL_GPL(prop_ta_device_get_by_name);

static int __init prop_ta_init(void)
{
	prop_ta_class = class_create(THIS_MODULE, "prop_ta");
	if (IS_ERR(prop_ta_class))
		return PTR_ERR(prop_ta_class);
	prop_ta_class->pm = &prop_ta_class_pm_ops;
	return 0;
}
module_init(prop_ta_init);

static void __exit prop_ta_exit(void)
{
	class_destroy(prop_ta_class);
}
module_exit(prop_ta_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Proprietary TA Class driver");
MODULE_AUTHOR("CY Huang <cy_huang@richtek.com>");
MODULE_VERSION("1.0.0");
