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
#include "mediatek/tpd.h"
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/cust_include/cust_project_all_config.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/fb.h>
#include <linux/platform_device.h>

#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif

#include <linux/freezer.h> 			//add for wait queue
#include <linux/input/mt.h>


#if __CUST_TP_GESTURE_SUPPORT__
#define TOUCH_IOC_MAGIC 'A'

//static spinlock_t gesture_lock;

#define UP_GESTURE_ENABLE			_IOW(TOUCH_IOC_MAGIC,	3, int)		//Enable gesture switch
#define UP_GESTURE_GENERATE		_IOR(TOUCH_IOC_MAGIC,	4, char*)	//Report specific gesture

char tpgesture_status = 0;
char tpgesture_value[10]={};

static wait_queue_head_t g_Waitq;
static DECLARE_WAIT_QUEUE_HEAD(g_Waitq);
static int flag_irq;

void tpgesture_hander(void)
{
	flag_irq = 1;
	wake_up(&g_Waitq);
}


static char tpgesture_status_value[5] = {};
static ssize_t show_tpgesture_value(struct device_driver *ddri, char *buf)
{
	printk("show tp gesture value is %s \n",tpgesture_value);
	return scnprintf(buf, PAGE_SIZE, "%s\n", tpgesture_value);
}
static ssize_t show_tpgesture_status_value(struct device_driver *ddri, char *buf)
{
	printk("show tp gesture status is %d\n",tpgesture_status);
	return scnprintf(buf, PAGE_SIZE, "%d\n", tpgesture_status);
}
static ssize_t store_tpgesture_status_value(struct device_driver *drv, const char *buffer, size_t count)
{
	if(!strncmp(buffer, "on", 2))
	{
		sprintf(tpgesture_status_value,"on");
		tpgesture_status = 1;//status --- on
	}
	else
	{
		sprintf(tpgesture_status_value,"off");
		tpgesture_status = 0;//status --- off
	}
	printk("store_tpgesture_status_value status is %s \n",tpgesture_status_value);
	return count;
}

static DRIVER_ATTR(tpgesture,  0664, show_tpgesture_value, NULL);
static DRIVER_ATTR(tpgesture_status,  0664, show_tpgesture_status_value, store_tpgesture_status_value);
static struct driver_attribute *gesture_attr_list[] = {
	&driver_attr_tpgesture,
	&driver_attr_tpgesture_status,
};

int gesture_create_attr(struct device_driver *driver)
{
    int idx, err = 0;
    int num = (int)(sizeof(gesture_attr_list)/sizeof(gesture_attr_list[0]));
	printk("add %s is start !!\n",__func__);
    if (driver == NULL)
        return -EINVAL;

    for (idx = 0; idx < num; idx++) {
        if ((err = driver_create_file(driver, gesture_attr_list[idx]))) {
            printk("driver_create_file (%s) = %d\n", gesture_attr_list[idx]->attr.name, err);
            break;
        }
    }
    return err;
}

int gesture_delete_attr(struct device_driver *driver)
{
    int idx , err = 0;
    int num = (int)(sizeof(gesture_attr_list)/sizeof(gesture_attr_list[0]));

    if (!driver)
        return -EINVAL;

    for (idx = 0; idx < num; idx++)
        driver_remove_file(driver, gesture_attr_list[idx]);

    return err;
}

static int tpd_misc_open(struct inode *inode, struct file *file)
{
	return nonseekable_open(inode, file);
}

static int tpd_misc_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long tpd_unlocked_ioctl(struct file *file,
			unsigned int cmd, unsigned long arg)
{
	/* char strbuf[256]; */
	void __user *data;
	long err = 0;

	int ioarg = 0;
	char temp[10]={};

	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok(VERIFY_WRITE,
			(void __user *)arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err = !access_ok(VERIFY_READ,
			(void __user *)arg, _IOC_SIZE(cmd));
	if (err) {
		pr_info("tpd: access error: %08X, (%2d, %2d)\n",
			cmd, _IOC_DIR(cmd), _IOC_SIZE(cmd));
		return -EFAULT;
	}

	switch (cmd) {
	case UP_GESTURE_ENABLE:
		err = copy_from_user(&ioarg, (unsigned int*)arg, sizeof(unsigned int));
		//spin_lock_irq(&gesture_lock);
		tpgesture_status = ioarg;
		//spin_unlock_irq(&gesture_lock);
		break;

    case UP_GESTURE_GENERATE:
		flag_irq = 0;
		wait_event_freezable(g_Waitq, 0 != flag_irq);
		data = (void __user *) arg;
		//spin_lock_irq(&gesture_lock);
		strcpy(temp,tpgesture_value);
		//spin_unlock_irq(&gesture_lock);
		err = copy_to_user(data, &temp, sizeof(temp));
		break;
	default:
		pr_info("tpd: unknown IOCTL: 0x%08x\n", cmd);
		err = -ENOIOCTLCMD;
		break;

	}

	return err;
}



static const struct file_operations tpd_fops = {
/* .owner = THIS_MODULE, */
	.open = tpd_misc_open,
	.release = tpd_misc_release,
	.unlocked_ioctl = tpd_unlocked_ioctl,
};

/*---------------------------------------------------------------------------*/
struct miscdevice tpd_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "touch",
	.fops = &tpd_fops,
};


const struct of_device_id touch_of_match[] = {
	{ .compatible = "mediatek,up_tpd", },
	{},
};



static int tpd_probe(struct platform_device *pdev)
{
	if (misc_register(&tpd_misc_device)){
		pr_info("mtk_tpd: tpd_misc_device register failed\n");
		return -1;
	}

	return 0;

}


static struct platform_driver tpd_driver = {

	.probe = tpd_probe,
	.driver = {
			.name = "mtk_up_tpd",
			.owner = THIS_MODULE,
			.of_match_table = touch_of_match,
	},
};


static int __init tpd_device_init(void)
{
	
	printk("MediaTek touch panel driver init\n");
	if (platform_driver_register(&tpd_driver) != 0)
		printk("unable to register touch panel driver.\n");

	if (gesture_create_attr(&tpd_driver.driver))
		printk("add device_create_file vibr_on fail!\n");
	return 0;
}
/* should never be called */
static void __exit tpd_device_exit(void)
{
	gesture_delete_attr(&tpd_driver.driver);

	printk("MediaTek touch panel driver exit\n");
	misc_deregister(&tpd_misc_device);


	platform_driver_unregister(&tpd_driver);
}

module_init(tpd_device_init);
module_exit(tpd_device_exit);

#endif

