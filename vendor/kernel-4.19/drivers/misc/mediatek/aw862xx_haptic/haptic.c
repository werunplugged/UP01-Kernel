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
#include "haptic.h"
#include "aw8624.h"
#include "aw8622x.h"
#include <linux/vmalloc.h>
/******************************************************
 *
 * Marco
 *
 ******************************************************/
#define AWINIC_DRIVER_VERSION	("v1.0.1")
#define AWINIC_I2C_NAME		("awinic_haptic")
#define AW_READ_CHIPID_RETRIES	(5)
#define AW_I2C_RETRIES		(2)
#define AW8624_CHIP_ID		(0x24)
#define AW8622X_CHIP_ID		(0x00)
#define AW_REG_ID		(0x00)
#define AW8622X_REG_EFRD9	(0x64)

struct aw8624 *g_aw8624;
struct aw8622x *g_aw8622x;

static int awinic_i2c_read(struct awinic *awinic,
		unsigned char reg_addr, unsigned char *reg_data)
{
	int ret = -1;
	unsigned char cnt = 0;

	while (cnt < AW_I2C_RETRIES) {
		ret = i2c_smbus_read_byte_data(awinic->i2c, reg_addr);
		if (ret < 0) {
			aw_dev_err(awinic->dev, "%s: i2c_read cnt=%d error=%d\n",
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

static int awinic_i2c_write(struct awinic *awinic,
		 unsigned char reg_addr, unsigned char reg_data)
{
	int ret = -1;
	unsigned char cnt = 0;

	while (cnt < AW_I2C_RETRIES) {
		ret =
		i2c_smbus_write_byte_data(awinic->i2c, reg_addr, reg_data);
		if (ret < 0) {
			aw_dev_err(awinic->dev, "%s: i2c_write cnt=%d error=%d\n",
				__func__, cnt, ret);
		} else {
			break;
		}
		cnt++;
		usleep_range(2000, 3000);
	}

	return ret;
}

static int awinic_hw_reset(struct awinic *awinic)
{
	aw_dev_info(awinic->dev, "%s enter\n", __func__);

	if (awinic && gpio_is_valid(awinic->reset_gpio)) {
		gpio_set_value_cansleep(awinic->reset_gpio, 0);
		usleep_range(1000, 2000);
		gpio_set_value_cansleep(awinic->reset_gpio, 1);
		usleep_range(3500, 4000);
	} else {
		dev_err(awinic->dev, "%s: failed\n", __func__);
	}
	return 0;
}

static int awinic_haptic_softreset(struct awinic *awinic)
{
	aw_dev_info(awinic->dev, "%s enter\n", __func__);
	awinic_i2c_write(awinic, AW_REG_ID, 0xAA);
	usleep_range(2000, 2500);
	return 0;
}
static int awinic_read_chipid(struct awinic *awinic)
{
	int ret = -1;
	unsigned char cnt = 0;
	unsigned char reg = 0;
	unsigned char ef_id = 0xff;


	while (cnt < AW_READ_CHIPID_RETRIES) {
		/* hardware reset */
		awinic_hw_reset(awinic);

		ret = awinic_i2c_read(awinic, AW_REG_ID, &reg);
		if (ret < 0) {
			aw_dev_err(awinic->dev,
				"%s: failed to read register AW_REG_ID: %d\n",
				__func__, ret);
		}

		switch (reg) {
		case AW8624_CHIP_ID:
			aw_dev_info(awinic->dev,
				"%s aw8624 detected\n", __func__);
			awinic->name = AW8624;
			awinic_haptic_softreset(awinic);
			return 0;
		case AW8622X_CHIP_ID:
			/* Distinguish products by AW8622X_REG_EFRD9. */
			awinic_i2c_read(awinic, AW8622X_REG_EFRD9, &ef_id);
			if ((ef_id & 0x41) == AW86224_5_EF_ID) {
				awinic->name = AW86224_5;
				aw_dev_info(awinic->dev,
					"%s aw86224_5 detected\n", __func__);
				awinic_haptic_softreset(awinic);
				return 0;
			} else if ((ef_id & 0x41) == AW86223_EF_ID) {
				awinic->name = AW86223;
				aw_dev_info(awinic->dev,
					"%s aw86223 detected\n", __func__);
				awinic_haptic_softreset(awinic);
				return 0;
			} else {
				aw_dev_info(awinic->dev,
					"%s unsupported ef_id = (0x%02X)\n",
					__func__, ef_id);
				break;
			}
		default:
			aw_dev_info(awinic->dev,
				"%s unsupported device revision (0x%x)\n",
			__func__, reg);
			break;
		}
		cnt++;

		usleep_range(2000, 3000);
	}

	return -EINVAL;
}
static int awinic_parse_dt(struct device *dev, struct awinic *awinic,
		struct device_node *np) {
	unsigned int val = 0;

	awinic->reset_gpio = of_get_named_gpio(np, "reset-gpio", 0);
	if (awinic->reset_gpio >= 0) {
		aw_dev_info(awinic->dev,
			"%s: reset gpio provided ok\n", __func__);
	} else {
		awinic->reset_gpio = -1;
		aw_dev_err(awinic->dev,
			"%s: no reset gpio provided, will not HW reset device\n",
			__func__);
		return -1;
	}

	awinic->irq_gpio = of_get_named_gpio(np, "irq-gpio", 0);
	if (awinic->irq_gpio < 0) {
		dev_err(dev, "%s: no irq gpio provided.\n", __func__);
		awinic->IsUsedIRQ = false;
	} else {
		aw_dev_info(awinic->dev,
			"%s: irq gpio provided ok.\n", __func__);
		awinic->IsUsedIRQ = true;
	}

	val = of_property_read_u32(np,
			"aw8622x_i2c_addr", &awinic->aw8622x_i2c_addr);
	if (val)
		aw_dev_err(awinic->dev,
			"%s:configure aw8622x_i2c_addr error\n", __func__);
	else
		aw_dev_info(awinic->dev,
			"%s: configure aw8622x_i2c_addr ok\n", __func__);
	return 0;
}

static int
awinic_i2c_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
{
	struct awinic *awinic;
	struct device_node *np = i2c->dev.of_node;
	int ret = -1;
	int irq_flags = 0;

	aw_dev_info(&i2c->dev, "%s enter\n", __func__);
	if (!i2c_check_functionality(i2c->adapter, I2C_FUNC_I2C)) {
		aw_dev_err(&i2c->dev, "check_functionality failed\n");
		return -EIO;
	}

	awinic = devm_kzalloc(&i2c->dev, sizeof(struct awinic), GFP_KERNEL);
	if (awinic == NULL)
		return -ENOMEM;

	awinic->dev = &i2c->dev;
	awinic->i2c = i2c;

	i2c_set_clientdata(i2c, awinic);
	/* aw862xx rst & int */
	if (np) {
		ret = awinic_parse_dt(&i2c->dev, awinic, np);
		if (ret) {
			aw_dev_err(&i2c->dev,
				"%s: failed to parse device tree node\n",
				__func__);
			goto err_parse_dt;
		}
	}
	if (gpio_is_valid(awinic->reset_gpio)) {
		ret = devm_gpio_request_one(&i2c->dev, awinic->reset_gpio,
			GPIOF_OUT_INIT_LOW, "awinic_rst");
		if (ret) {
			aw_dev_err(&i2c->dev,
				"%s: rst request failed\n", __func__);
			goto err_reset_gpio_request;
		}
	}

	if (gpio_is_valid(awinic->irq_gpio)) {
		ret = devm_gpio_request_one(&i2c->dev, awinic->irq_gpio,
			GPIOF_DIR_IN, "awinic_int");
		if (ret) {
			aw_dev_err(&i2c->dev,
				"%s: int request failed\n", __func__);
			goto err_irq_gpio_request;
		}
	}
	/* read chip id */
	ret = awinic_read_chipid(awinic);
	if (ret < 0) {
		i2c->addr = (u16)awinic->aw8622x_i2c_addr;
		aw_dev_info(&i2c->dev, "%s awinic->aw8622x_i2c_addr=0x%02x\n",
			   __func__, awinic->aw8622x_i2c_addr);
		ret = awinic_read_chipid(awinic);
		if (ret < 0) {
			aw_dev_err(&i2c->dev,
			"%s: awinic_read_chipid failed ret=%d\n",
			__func__, ret);
			goto err_id;
		}
	}
	/* awinic device name */
	if (i2c->dev.of_node)
		dev_set_name(&i2c->dev, "%s", AWINIC_DEV_NAME);
	else
		aw_dev_err(&i2c->dev, "%s failed to set device name: %d\n",
			   __func__, ret);
	/*aw8624*/
	if (awinic->name == AW8624) {
		awinic->aw8624 = devm_kzalloc(&i2c->dev,
					sizeof(struct aw8624), GFP_KERNEL);
		if (awinic->aw8624 == NULL) {
			if (gpio_is_valid(awinic->irq_gpio))
				devm_gpio_free(&i2c->dev, awinic->irq_gpio);
			if (gpio_is_valid(awinic->reset_gpio))
				devm_gpio_free(&i2c->dev, awinic->reset_gpio);
			devm_kfree(&i2c->dev, awinic);
			awinic = NULL;
			return -ENOMEM;
		}
		awinic->aw8624->dev = awinic->dev;
		awinic->aw8624->i2c = awinic->i2c;
		awinic->aw8624->reset_gpio = awinic->reset_gpio;
		awinic->aw8624->irq_gpio = awinic->irq_gpio;
		awinic->aw8624->IsUsedIRQ = awinic->IsUsedIRQ;
		if (np) {
			ret = aw8624_parse_dt(awinic->aw8624, &i2c->dev, np);
			if (ret) {
				aw_dev_err(&i2c->dev, "%s: failed to parse device tree node\n",
					__func__);
				goto err_aw8624_parse_dt;
			}
		}

		/* aw8624 irq */
		if (gpio_is_valid(awinic->aw8624->irq_gpio) &&
			!(awinic->aw8624->flags & AW8624_FLAG_SKIP_INTERRUPTS)) {
			/* register irq handler */
			aw8624_interrupt_setup(awinic->aw8624);
			irq_flags = IRQF_TRIGGER_FALLING | IRQF_ONESHOT;
			ret = devm_request_threaded_irq(&i2c->dev,
					gpio_to_irq(awinic->aw8624->irq_gpio),
					NULL, aw8624_irq, irq_flags,
					"aw8624", awinic->aw8624);
			if (ret != 0) {
				aw_dev_err(&i2c->dev, "%s: failed to request IRQ %d: %d\n",
					__func__,
					gpio_to_irq(awinic->aw8624->irq_gpio),
					ret);
				goto err_aw8624_irq;
			}
		}
		dev_set_drvdata(&i2c->dev, awinic->aw8624);
		g_aw8624 = awinic->aw8624;
		aw8624_vibrator_init(awinic->aw8624);
		aw8624_haptic_init(awinic->aw8624);
		aw8624_ram_init(awinic->aw8624);
		usleep_range(100000, 150000);

#ifdef CONFIG_PM_WAKELOCKS
#ifdef KERNEL_VERSION_414
		wakeup_source_init(&awinic->aw8624->wk_lock,
				"aw8624_wakelock");
#endif
#else
		wake_lock_init(&awinic->aw8624->wk_lock, WAKE_LOCK_SUSPEND,
				"aw8624_wakelock");
#endif

	}
	/* aw8622x */
	if (awinic->name == AW86223 || awinic->name == AW86224_5) {
		awinic->aw8622x = devm_kzalloc(&i2c->dev,
					sizeof(struct aw8622x), GFP_KERNEL);
		if (awinic == NULL) {
			if (gpio_is_valid(awinic->irq_gpio))
				devm_gpio_free(&i2c->dev, awinic->irq_gpio);
			if (gpio_is_valid(awinic->reset_gpio))
				devm_gpio_free(&i2c->dev, awinic->reset_gpio);
			devm_kfree(&i2c->dev, awinic);
			awinic = NULL;
			return -ENOMEM;
		}
		awinic->aw8622x->dev = awinic->dev;
		awinic->aw8622x->i2c = awinic->i2c;
		awinic->aw8622x->reset_gpio = awinic->reset_gpio;
		awinic->aw8622x->irq_gpio = awinic->irq_gpio;
		awinic->aw8622x->isUsedIntn = awinic->IsUsedIRQ;
		awinic->aw8622x->name = awinic->name;
		/* chip qualify */
		if (!aw8622x_check_qualify(awinic->aw8622x)) {
			aw_dev_err(&i2c->dev,
				"%s:unqualified chip!\n", __func__);
			goto err_aw8622x_check_qualify;
		}
		if (np) {
			ret = aw8622x_parse_dt(awinic->aw8622x, &i2c->dev, np);
			if (ret) {
				aw_dev_err(&i2c->dev,
					"%s: failed to parse device tree node\n",
					__func__);
				goto err_aw8622x_parse_dt;
			}
		}
		/* aw8622x irq */
		if (gpio_is_valid(awinic->aw8622x->irq_gpio) &&
		    !(awinic->aw8622x->flags & AW8622X_FLAG_SKIP_INTERRUPTS)) {
			/* register irq handler */
			aw8622x_interrupt_setup(awinic->aw8622x);
			irq_flags = IRQF_TRIGGER_FALLING | IRQF_ONESHOT;
			ret = devm_request_threaded_irq(&i2c->dev,
					gpio_to_irq(awinic->aw8622x->irq_gpio),
					NULL, aw8622x_irq, irq_flags,
					"aw8622x", awinic->aw8622x);
			if (ret != 0) {
				aw_dev_err(&i2c->dev,
					"%s: failed to request IRQ %d: %d\n",
					__func__,
					gpio_to_irq(awinic->aw8622x->irq_gpio),
					ret);
				goto err_aw8622x_irq;
			}
		} else {
			aw_dev_info(&i2c->dev,
				"%s skipping IRQ registration\n", __func__);
			/* disable feature support if gpio was invalid */
			awinic->aw8622x->flags |= AW8622X_FLAG_SKIP_INTERRUPTS;
		}
		dev_set_drvdata(&i2c->dev, awinic->aw8622x);
		g_aw8622x = awinic->aw8622x;
		aw8622x_vibrator_init(awinic->aw8622x);
		aw8622x_haptic_init(awinic->aw8622x);
		aw8622x_ram_work_init(awinic->aw8622x);
	}
	dev_set_drvdata(&i2c->dev, awinic);
	aw_dev_info(&i2c->dev, "%s probe completed successfully!\n", __func__);

	return 0;

err_aw8622x_irq:
err_aw8622x_parse_dt:
err_aw8622x_check_qualify:
	if (awinic->name == AW86223 || awinic->name == AW86224_5) {
		devm_kfree(&i2c->dev, awinic->aw8622x);
		awinic->aw8622x = NULL;
	}
err_aw8624_irq:
err_aw8624_parse_dt:
	if (awinic->name == AW8624) {
		devm_kfree(&i2c->dev, awinic->aw8624);
		awinic->aw8624 = NULL;
	}

err_id:
	if (gpio_is_valid(awinic->irq_gpio))
		devm_gpio_free(&i2c->dev, awinic->irq_gpio);
err_irq_gpio_request:
	if (gpio_is_valid(awinic->reset_gpio))
		devm_gpio_free(&i2c->dev, awinic->reset_gpio);
err_reset_gpio_request:
err_parse_dt:
	devm_kfree(&i2c->dev, awinic);
	awinic = NULL;
	return ret;

}

static int awinic_i2c_remove(struct i2c_client *i2c)
{
	struct awinic *awinic = i2c_get_clientdata(i2c);

	aw_dev_info(&i2c->dev, "%s enter\n", __func__);

	if (awinic->name == AW8624) {
		aw_dev_info(&i2c->dev, "%s chip is aw8624\n", __func__);
		cancel_delayed_work_sync(&awinic->aw8624->ram_work);
		misc_deregister(&aw8624_haptic_misc);
#ifdef AW_TIKTAP
		proc_remove(awinic->aw8624->aw_config_proc);
#endif
		cancel_work_sync(&awinic->aw8624->haptic_audio.work);
		hrtimer_cancel(&awinic->aw8624->haptic_audio.timer);
		if (awinic->aw8624->IsUsedIRQ)
			cancel_work_sync(&awinic->aw8624->rtp_work);
		cancel_work_sync(&awinic->aw8624->vibrator_work);
		hrtimer_cancel(&awinic->aw8624->timer);

		mutex_destroy(&awinic->aw8624->lock);
		mutex_destroy(&awinic->aw8624->rtp_lock);
		mutex_destroy(&awinic->aw8624->haptic_audio.lock);

		sysfs_remove_group(&awinic->aw8624->i2c->dev.kobj,
			&aw8624_vibrator_attribute_group);

		devm_free_irq(&awinic->aw8624->i2c->dev,
			gpio_to_irq(awinic->aw8624->irq_gpio), awinic->aw8624);

	} else if (awinic->name == AW86223 || awinic->name == AW86224_5) {
		aw_dev_info(&i2c->dev, "%s chip is aw8622x\n", __func__);
		cancel_delayed_work_sync(&g_aw8622x->ram_work);
		cancel_work_sync(&g_aw8622x->haptic_audio.work);
		hrtimer_cancel(&g_aw8622x->haptic_audio.timer);
		if (g_aw8622x->isUsedIntn)
			cancel_work_sync(&g_aw8622x->rtp_work);
		cancel_work_sync(&g_aw8622x->long_vibrate_work);
#ifdef AW_TIKTAP
		proc_remove(g_aw8622x->aw_config_proc);
		free_pages((unsigned long)g_aw8622x->start_buf, TIKTAP_MMAP_PAGE_ORDER);
		g_aw8622x->start_buf = NULL;
#endif
		hrtimer_cancel(&g_aw8622x->timer);
		mutex_destroy(&g_aw8622x->lock);
		mutex_destroy(&g_aw8622x->rtp_lock);
		mutex_destroy(&g_aw8622x->haptic_audio.lock);
		sysfs_remove_group(&g_aw8622x->i2c->dev.kobj,
			&aw8622x_vibrator_attribute_group);
		devm_free_irq(&g_aw8622x->i2c->dev,
			gpio_to_irq(g_aw8622x->irq_gpio), g_aw8622x);

	} else {
		aw_dev_err(&i2c->dev, "%s no chip\n", __func__);
		return -1;
	}

	return 0;
}

static const struct i2c_device_id awinic_i2c_id[] = {
	{ AWINIC_I2C_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, awinic_i2c_id);

static const struct of_device_id awinic_dt_match[] = {
	{ .compatible = "awinic,awinic_haptic" },
	{ },
};

static struct i2c_driver awinic_i2c_driver = {
	.driver = {
		.name = AWINIC_I2C_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(awinic_dt_match),
	},
	.probe = awinic_i2c_probe,
	.remove = awinic_i2c_remove,
	.id_table = awinic_i2c_id,
};

static int __init awinic_i2c_init(void)
{
	int ret = 0;

	pr_info("awinic driver version %s\n", AWINIC_DRIVER_VERSION);

	ret = i2c_add_driver(&awinic_i2c_driver);
	if (ret) {
		pr_err("fail to add awinic device into i2c\n");
		return ret;
	}

	return 0;
}

//late_initcall(awinic_i2c_init);
module_init(awinic_i2c_init);

static void __exit awinic_i2c_exit(void)
{
	i2c_del_driver(&awinic_i2c_driver);
}
module_exit(awinic_i2c_exit);

MODULE_DESCRIPTION("awinic Haptic Driver");
MODULE_LICENSE("GPL v2");
