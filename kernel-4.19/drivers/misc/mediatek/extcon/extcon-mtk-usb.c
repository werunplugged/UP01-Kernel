// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 MediaTek Inc.
 */

#include <linux/extcon-provider.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/usb/role.h>
#include <linux/workqueue.h>

#include "extcon-mtk-usb.h"
#if defined(CONFIG_CHARGER_PSC5415A) || defined(CONFIG_CHARGER_BCT24157)
#include <charger_class.h>
#endif

#ifdef CONFIG_TCPC_CLASS
#include "tcpm.h"
#endif

#ifdef CONFIG_MTK_USB_TYPEC_U3_MUX
#include "mux_switch.h"
#endif

#include <linux/cust_include/cust_project_all_config.h>

#ifdef __CUST_TCPC_POLARITY_DETECT__
enum up_tcpc_polarity{
	UP_TCPC_POLARITY_P,
	UP_TCPC_POLARITY_N,
	UP_TCPC_PLUGOUT
};
int up_tcpc_polarity_val = UP_TCPC_PLUGOUT;
void update_tcpc_polarity(int polarity)
{
	up_tcpc_polarity_val = polarity;

}
#endif

#ifdef __CUST_OTG_GPIO_SELECT__
       #if __CUST_OTG_GPIO_SELECT__
		#include <linux/of_gpio.h> 
		#include <linux/delay.h> 

		#define OTG_SELECT_GPIO_NAME       "otg_gpio_select"
		static int up_otg_select_gpio_num;
		void up_otg_gpio_set(bool en);
		int get_otg_select_gpio(void);
		static int up_parse_otg_dts(struct device_node *node, const char *gpio_name);

#endif
#endif

#ifdef __CUST_WIRELESS_CHARGE_SUPPORT__
       #if __CUST_WIRELESS_CHARGE_SUPPORT__
               #include <linux/delay.h> 
               #include <linux/of_gpio.h> 
               
               #define WIRELESS_CHARGE_GPIO_NAME       "wireless_charge_gpio"
               static int up_wireless_charge_gpio_num;
               
               int get_wireless_gpio(void);
               int up_parse_wireless_dts(struct device_node *node, const char *gpio_name);

       #ifdef CONFIG_WIRELESS_POWER_MT5728
               extern ssize_t mt5728_force_chipen_disable(void);
               extern ssize_t mt5728_chipen_ctrl_by_hardware(void);
               extern int is_reverse_charger_online(void);
       #endif

       #endif
#endif

#ifdef __CUST_UVC_VBUS_GPIO__


static int up_uvc_vbus_gpio_num;
static int up_uvc_switch_gpio_num;
static int up_parse_dts(struct device_node *node, const char *gpio_name)
{
	int gpio_num = 0;
	struct gpio_desc *desc;
	int ret = 0;

	if (node)
	{
		gpio_num = of_get_named_gpio(node, gpio_name, 0);
		if (gpio_num < 0)
		{
			pr_info("%s: of_get_named_gpio fail. \n", __func__);
			return -1;
		}
		else
		{
			pr_info("%s: of_get_named_gpio GPIO is %d.\n", __func__, gpio_num);
			desc = gpio_to_desc(gpio_num);
			if (!desc)
			{
				pr_info("%s: gpio_desc is null.\n", __func__);
				return -1;
			}
			else
				pr_info("%s: gpio_desc is not null.\n", __func__);

			if (gpio_is_valid(gpio_num))
				pr_info("%s: gpio number %d is valid. \n", __func__ ,gpio_num);

			ret = gpio_request(gpio_num, gpio_name);
			if (ret)
			{
				pr_info("%s: gpio_request fail. \n", __func__);
				return -1;
			}
			else
			{
				ret = gpio_direction_output(gpio_num, 1);
				if (ret)
				{
					pr_info("%s: gpio_direction_output failed. \n", __func__);
					return -1;
				}

				gpio_set_value(gpio_num, 0);
				pr_info("%s: gpio_get_value =%d. \n", __func__, gpio_get_value(gpio_num));

				return gpio_num;
			}
		}
	}
	else
	{
		pr_info("%s: get gpio num fail. \n", __func__);
		return -1;
	}
}
#endif



static struct mtk_extcon_info *g_extcon;

static const unsigned int usb_extcon_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_NONE,
};

static void mtk_usb_extcon_update_role(struct work_struct *work)
{
	struct usb_role_info *role = container_of(to_delayed_work(work),
					struct usb_role_info, dwork);
	struct mtk_extcon_info *extcon = role->extcon;
	unsigned int cur_dr, new_dr;

	cur_dr = extcon->c_role;
	new_dr = role->d_role;

	dev_info(extcon->dev, "cur_dr(%d) new_dr(%d)\n", cur_dr, new_dr);

	/* none -> device */
	if (cur_dr == DUAL_PROP_DR_NONE &&
			new_dr == DUAL_PROP_DR_DEVICE) {
		extcon_set_state_sync(extcon->edev, EXTCON_USB, true);
	/* none -> host */
	} else if (cur_dr == DUAL_PROP_DR_NONE &&
			new_dr == DUAL_PROP_DR_HOST) {
		extcon_set_state_sync(extcon->edev, EXTCON_USB_HOST, true);
	/* device -> none */
	} else if (cur_dr == DUAL_PROP_DR_DEVICE &&
			new_dr == DUAL_PROP_DR_NONE) {
		extcon_set_state_sync(extcon->edev, EXTCON_USB, false);
	/* host -> none */
	} else if (cur_dr == DUAL_PROP_DR_HOST &&
			new_dr == DUAL_PROP_DR_NONE) {
		extcon_set_state_sync(extcon->edev, EXTCON_USB_HOST, false);
	/* device -> host */
	} else if (cur_dr == DUAL_PROP_DR_DEVICE &&
			new_dr == DUAL_PROP_DR_HOST) {
		extcon_set_state_sync(extcon->edev, EXTCON_USB, false);
		extcon_set_state_sync(extcon->edev, EXTCON_USB_HOST, true);
	/* host -> device */
	} else if (cur_dr == DUAL_PROP_DR_HOST &&
			new_dr == DUAL_PROP_DR_DEVICE) {
		extcon_set_state_sync(extcon->edev, EXTCON_USB_HOST, false);
		extcon_set_state_sync(extcon->edev, EXTCON_USB, true);
	}

	/* usb role switch */
	if (extcon->role_sw) {
		if (new_dr == DUAL_PROP_DR_DEVICE)
			usb_role_switch_set_role(extcon->role_sw,
						USB_ROLE_DEVICE);
		else if (new_dr == DUAL_PROP_DR_HOST)
			usb_role_switch_set_role(extcon->role_sw,
						USB_ROLE_HOST);
		else
			usb_role_switch_set_role(extcon->role_sw,
						USB_ROLE_NONE);
	}

	extcon->c_role = new_dr;
	kfree(role);
}

static int mtk_usb_extcon_set_role(struct mtk_extcon_info *extcon,
						unsigned int role)
{
	struct usb_role_info *role_info;

	/* create and prepare worker */
	role_info = kzalloc(sizeof(*role_info), GFP_KERNEL);
	if (!role_info)
		return -ENOMEM;

	INIT_DELAYED_WORK(&role_info->dwork, mtk_usb_extcon_update_role);

	role_info->extcon = extcon;
	role_info->d_role = role;
	/* issue connection work */
	queue_delayed_work(extcon->extcon_wq, &role_info->dwork, 0);

	return 0;
}

#if !defined(CONFIG_USB_MTK_HDRC)
void mt_usb_connect()
{
	/* if (g_extcon)
		mtk_usb_extcon_set_role(extcon, DUAL_PROP_DR_DEVICE); */
}
EXPORT_SYMBOL(mt_usb_connect);

void mt_usb_disconnect()
{
	/* if (g_extcon)
		mtk_usb_extcon_set_role(extcon, DUAL_PROP_DR_NONE); */
}
EXPORT_SYMBOL(mt_usb_disconnect);
#endif

static int mtk_usb_extcon_psy_notifier(struct notifier_block *nb,
				unsigned long event, void *data)
{
	struct power_supply *psy = data;
	struct mtk_extcon_info *extcon = container_of(nb,
					struct mtk_extcon_info, psy_nb);
	union power_supply_propval pval;
	union power_supply_propval ival;
	union power_supply_propval tval;
	int ret;

	if (event != PSY_EVENT_PROP_CHANGED || psy != extcon->usb_psy)
		return NOTIFY_DONE;

	ret = power_supply_get_property(psy,
				POWER_SUPPLY_PROP_ONLINE, &pval);
	if (ret < 0) {
		dev_info(extcon->dev, "failed to get online prop\n");
		return NOTIFY_DONE;
	}

	ret = power_supply_get_property(psy,
				POWER_SUPPLY_PROP_AUTHENTIC, &ival);
	if (ret < 0) {
		dev_info(extcon->dev, "failed to get authentic prop\n");
		ival.intval = 0;
	}

	ret = power_supply_get_property(psy,
				POWER_SUPPLY_PROP_TYPE, &tval);
	if (ret < 0) {
		dev_info(extcon->dev, "failed to get usb type\n");
		return NOTIFY_DONE;
	}

	dev_info(extcon->dev, "online=%d, ignore_usb=%d, type=%d\n",
				pval.intval, ival.intval, tval.intval);

	if (ival.intval)
		return NOTIFY_DONE;

#ifdef CONFIG_TCPC_CLASS
	if (extcon->c_role == DUAL_PROP_DR_NONE && pval.intval &&
			(tval.intval == POWER_SUPPLY_TYPE_USB ||
			tval.intval == POWER_SUPPLY_TYPE_USB_CDP))
		mtk_usb_extcon_set_role(extcon, DUAL_PROP_DR_DEVICE);
#else
	if (pval.intval && (tval.intval == POWER_SUPPLY_TYPE_USB ||
			tval.intval == POWER_SUPPLY_TYPE_USB_CDP))
		mtk_usb_extcon_set_role(extcon, DUAL_PROP_DR_DEVICE);
	else
		mtk_usb_extcon_set_role(extcon, DUAL_PROP_DR_NONE);
#endif
	return NOTIFY_DONE;
}

static int mtk_usb_extcon_psy_init(struct mtk_extcon_info *extcon)
{
	int ret = 0;
	struct device *dev = extcon->dev;
	union power_supply_propval pval;
	union power_supply_propval ival;
	union power_supply_propval tval;

	extcon->usb_psy = devm_power_supply_get_by_phandle(dev, "charger");
	if (IS_ERR_OR_NULL(extcon->usb_psy)) {
		dev_err(dev, "fail to get usb_psy\n");
		extcon->usb_psy = NULL;
		return -EINVAL;
	}

	extcon->psy_nb.notifier_call = mtk_usb_extcon_psy_notifier;
	ret = power_supply_reg_notifier(&extcon->psy_nb);
	if (ret) {
		dev_err(dev, "fail to register notifer\n");
		return ret;
	}

	ret = power_supply_get_property(extcon->usb_psy,
				POWER_SUPPLY_PROP_ONLINE, &pval);
	if (ret < 0) {
		dev_info(extcon->dev, "failed to get online prop\n");
		return 0;
	}

	ret = power_supply_get_property(extcon->usb_psy,
				POWER_SUPPLY_PROP_AUTHENTIC, &ival);
	if (ret < 0) {
		dev_info(extcon->dev, "failed to get authentic prop\n");
		ival.intval = 0;
	}

	ret = power_supply_get_property(extcon->usb_psy,
				POWER_SUPPLY_PROP_USB_TYPE, &tval);
	if (ret < 0) {
		dev_info(extcon->dev, "failed to get usb type\n");
		return 0;
	}

	dev_info(extcon->dev, "online=%d, ignore_usb=%d, type=%d\n",
				pval.intval, ival.intval, tval.intval);

	if (ival.intval)
		return 0;

	if (pval.intval && (tval.intval == POWER_SUPPLY_USB_TYPE_SDP ||
			tval.intval == POWER_SUPPLY_USB_TYPE_CDP))
		mtk_usb_extcon_set_role(extcon, DUAL_PROP_DR_DEVICE);

	return 0;
}
extern void mobile_source_handler(bool open);
#if defined ADAPT_CHARGER_V1
#include <mt-plat/v1/charger_class.h>
static struct charger_device *primary_charger;

static int mtk_usb_extcon_set_vbus_v1(bool is_on) {
	if (!primary_charger) {
		primary_charger = get_charger_by_name("primary_chg");
		if (!primary_charger) {
			pr_info("%s: get primary charger device failed\n", __func__);
			return -ENODEV;
		}
	}
#if defined(CONFIG_MTK_GAUGE_VERSION) && (CONFIG_MTK_GAUGE_VERSION == 30)
	pr_info("%s: is_on=%d\n", __func__, is_on);
	if (is_on) {
#ifdef CONFIG_WIRELESS_POWER_MT5728
		if(is_reverse_charger_online()){
#ifdef __CUST_OTG_GPIO_SELECT__
       #if __CUST_OTG_GPIO_SELECT__
			gpio_direction_output(up_otg_select_gpio_num,0);
			gpio_set_value(up_otg_select_gpio_num, 0);		
#endif
#endif	
		#ifdef __CUST_WIRELESS_CHARGE_SUPPORT__	
			#if __CUST_WIRELESS_CHARGE_SUPPORT__
				pr_info("pull up the wireless gpio before enable OTG.\n");
				gpio_direction_output(up_wireless_charge_gpio_num, 0);
				gpio_set_value(up_wireless_charge_gpio_num, 0);//state:insert otg;chipen-pin(gpio26) output high
			#endif
		#endif		
			printk("UP_OTG_up_is_reverse_charger_online:%d\n",is_reverse_charger_online());
		}	
		else
#endif				
		{
		#ifdef CONFIG_WIRELESS_POWER_MT5728
			mt5728_force_chipen_disable();//state:insert otg;chipen-pin(gpio26) output high
		#endif
		
		#ifdef __CUST_WIRELESS_CHARGE_SUPPORT__	
			#if __CUST_WIRELESS_CHARGE_SUPPORT__
				pr_info("pull up the wireless gpio before enable OTG.\n");
				gpio_direction_output(up_wireless_charge_gpio_num, 0);
				gpio_set_value(up_wireless_charge_gpio_num, 1);//state:insert otg;chipen-pin(gpio26) output high
			#endif
		#endif	
		#ifdef __CUST_OTG_GPIO_SELECT__
       			#if __CUST_OTG_GPIO_SELECT__	
				up_otg_gpio_set(true);//state:insert otg;otg-en-pin(gpio25) output high
				printk("UP_OTG_up_otg_gpio_set_true\n");
			#endif
		#endif						
		}	
		charger_dev_enable_otg(primary_charger, true);
#ifdef __CUST_OTG_CURRENT__
		charger_dev_set_boost_current_limit(primary_charger,
			__CUST_OTG_CURRENT__);
#else
		charger_dev_set_boost_current_limit(primary_charger,
			1500000);
#endif
		#if 0
		{// # workaround
			charger_dev_kick_wdt(primary_charger);
			enable_boost_polling(true);
		}
		#endif
	#if defined(CONFIG_MACH_MT6853) || defined(CONFIG_MACH_MT6833) || defined(CONFIG_MACH_MT6877)
		mobile_source_handler(1);
	#endif
	} else {
		charger_dev_enable_otg(primary_charger, false);
#ifdef __CUST_OTG_GPIO_SELECT__
       #if __CUST_OTG_GPIO_SELECT__
		up_otg_gpio_set(false); //state:Pull out the otg;otg-en-pin(gpio25) input
		printk("UP_OTG_up_otg_gpio_set_false\n");
	#endif
#endif
#ifdef __CUST_WIRELESS_CHARGE_SUPPORT__
	#if __CUST_WIRELESS_CHARGE_SUPPORT__
		pr_info("pull down the wireless gpio after disable OTG.\n");	
			gpio_direction_input(up_wireless_charge_gpio_num); //state:Pull out the otg;chipen-pin(gpio26) output high			
	#endif
#endif

#ifdef CONFIG_WIRELESS_POWER_MT5728
pr_info("pull down the wireless gpio after disable OTG.\n");
		mt5728_chipen_ctrl_by_hardware(); //state:Pull out the otg;chipen-pin(gpio26) output high
		msleep(5);
#endif
		#if 0
			//# workaround
			enable_boost_polling(false);
		#endif
	#if defined(CONFIG_MACH_MT6853) || defined(CONFIG_MACH_MT6833) || defined(CONFIG_MACH_MT6877)
		mobile_source_handler(0);
	#endif
	}
#else
	if (is_on) {
		charger_dev_enable_otg(primary_charger, true);
		charger_dev_set_boost_current_limit(primary_charger,
			1500000);
	} else {
		charger_dev_enable_otg(primary_charger, false);
	}
#endif
		return 0;
}
#endif //ADAPT_CHARGER_V1

/*OTG BEGIN*/
#ifdef __CUST_OTG_GPIO_SELECT__
       #if __CUST_OTG_GPIO_SELECT__
static int up_parse_otg_dts(struct device_node *node, const char *gpio_name)
{
       int gpio_num = 0;
       struct gpio_desc *desc;
       int ret = 0;
       printk("%s: UP_OTG_up_parse_otg_dts \n", __func__);
       if (node)
       {
               gpio_num = of_get_named_gpio(node, gpio_name, 0);
               if (gpio_num < 0)
               {
                       printk("%s: UP_OTG_of_get_named_gpio fail. \n", __func__);
                       return -1;
               }
               else
               {
                       printk("%s: UP_OTG_of_get_named_gpio GPIO is %d.\n", __func__, gpio_num);
                       desc = gpio_to_desc(gpio_num);
                       if (!desc)
                       {
                               printk("%s: UP_OTG_gpio_desc is null.\n", __func__);
                               return -1;
                       }
                       else
                               printk("%s: UP_OTG_gpio_desc is not null.\n", __func__);

                       if (gpio_is_valid(gpio_num))
                               printk("%s: UP_OTG_gpio number %d is valid. \n", __func__ ,gpio_num);

                       ret = gpio_request(gpio_num, gpio_name);
                       if (ret)
                       {
                               printk("%s: UP_OTG_gpio_request fail. \n", __func__);
                               return -1;
                       }
                       else
                       {
                               ret = gpio_direction_output(gpio_num, 1);
                               if (ret)
                               {
                                       printk("%s: UP_OTG_gpio_direction_output failed. \n", __func__);
                                       return -1;
                               }

                               gpio_set_value(gpio_num, 0);
                               printk("%s: UP_OTG_gpio_get_value =%d. \n", __func__, gpio_get_value(gpio_num));

                               return gpio_num;
                       }
               }
       }
       else
       {
               printk("%s: UP_OTG_get gpio num fail. \n", __func__);
               return -1;
       }
}

int get_otg_select_gpio(void)
{
       struct device_node *node;
       printk("%s: UP_OTG_enter. \n", __func__);

       node = of_find_compatible_node(NULL, NULL, "mediatek,extcon-usb");
       if (node)
       {
               up_otg_select_gpio_num = up_parse_otg_dts(node, OTG_SELECT_GPIO_NAME);

               if (0 > up_otg_select_gpio_num)
               {
                       printk("%s: UP_OTG_up_parse_dts 1 fail. \n", __func__);
                       return -1;
               }
       }
       else
       {
               printk("%s: UP_OTG_cannot get the node: 'mediatek,usb3'.\n", __func__);
               return -ENODEV;
       }

       printk("%s: UP_OTG_end. \n", __func__);
       return 0;
}

void up_otg_gpio_set(bool en)
{
	if(en) {
		gpio_direction_output(up_otg_select_gpio_num,0);
		gpio_set_value(up_otg_select_gpio_num, 1);
		mdelay(5);
		printk("UP_OTG_pull up the otg select gpio after enable OTG gpio value = %d .\n",gpio_get_value(up_otg_select_gpio_num));
	} else {
		//gpio_set_value(up_otg_select_gpio_num, 0);
		gpio_direction_input(up_otg_select_gpio_num);
		mdelay(5);
		printk("UP_OTG_pull down the otg select gpio after disable OTG gpio value = %d .\n",gpio_get_value(up_otg_select_gpio_num));
	}
}
#endif
#endif


#ifdef __CUST_WIRELESS_CHARGE_SUPPORT__
	#if __CUST_WIRELESS_CHARGE_SUPPORT__
int up_parse_wireless_dts(struct device_node *node, const char *gpio_name)
{
	int gpio_num = 0;
	struct gpio_desc *desc;
	int ret = 0;

	if (node)
	{
		gpio_num = of_get_named_gpio(node, gpio_name, 0);
		if (gpio_num < 0)
		{
			pr_info("%s: of_get_named_gpio fail. \n", __func__);
			return -1;
		}
		else
		{
			pr_info("%s: of_get_named_gpio GPIO is %d.\n", __func__, gpio_num);
			desc = gpio_to_desc(gpio_num);
			if (!desc)
			{
				pr_info("%s: gpio_desc is null.\n", __func__);
				return -1;
			}
			else
				pr_info("%s: gpio_desc is not null.\n", __func__);

			if (gpio_is_valid(gpio_num))
				pr_info("%s: gpio number %d is valid. \n", __func__ ,gpio_num);

			ret = gpio_request(gpio_num, gpio_name);
			if (ret)
			{
				pr_info("%s: gpio_request fail. \n", __func__);
				return -1;
			}
			else
			{
				ret = gpio_direction_output(gpio_num, 1);
				if (ret)
				{
					pr_info("%s: gpio_direction_output failed. \n", __func__);
					return -1;
				}

				gpio_set_value(gpio_num, 0);
				pr_info("%s: gpio_get_value =%d. \n", __func__, gpio_get_value(gpio_num));

				return gpio_num; 
			}
		}
	}
	else
	{
		pr_info("%s: get gpio num fail. \n", __func__);
		return -1;
	}
}

int get_wireless_gpio(void)
{
	struct device_node *node;
	pr_info("%s: enter. \n", __func__);

	node = of_find_compatible_node(NULL, NULL, "mediatek,usb_boost");
	if (node)
	{
		up_wireless_charge_gpio_num = up_parse_wireless_dts(node, WIRELESS_CHARGE_GPIO_NAME);

		if (0 > up_wireless_charge_gpio_num)
		{
			pr_info("%s: up_parse_dts fail. \n", __func__);
			return -1;
		}
	}
	else
	{
		pr_info("%s: cannot get the node: 'mediatek,usb_boost'.\n", __func__);
		return -ENODEV;
	}

	pr_info("%s: end. \n", __func__);
	return 0;
}
void up_wireless_gpio_set(bool en)
{
	if(en) {
		gpio_direction_output(up_wireless_charge_gpio_num,0);
		gpio_set_value(up_wireless_charge_gpio_num, 1);
		mdelay(5);
		printk("UP_OTG_pull up the wireless charge gpio after enable OTG gpio value = %d .\n",gpio_get_value(up_wireless_charge_gpio_num));
	} else {
		gpio_direction_input(up_wireless_charge_gpio_num);
		mdelay(5);
		printk("UP_OTG_pull down the wireless charge gpio after disable OTG gpio value = %d .\n",gpio_get_value(up_wireless_charge_gpio_num));
	}
}
#endif
#endif

static int mtk_usb_extcon_set_vbus(struct mtk_extcon_info *extcon,
							bool is_on)
{
#if !defined(CONFIG_CHARGER_PSC5415A) || !defined(CONFIG_CHARGER_BCT24157)
	int ret;
#endif
#if defined ADAPT_CHARGER_V1
	ret = mtk_usb_extcon_set_vbus_v1(is_on);
#else
	struct device *dev = extcon->dev;
#if defined(CONFIG_CHARGER_PSC5415A) || defined(CONFIG_CHARGER_BCT24157)
       struct charger_device *chg_dev;
dev_err(dev, "vbus turn %s\n", is_on ? "on" : "off");
       chg_dev = get_charger_by_name("primary_chg");
       if (!chg_dev)
           return 0;   
#else
       struct regulator *vbus = extcon->vbus;
#endif
	/* vbus is optional */
#if defined(CONFIG_CHARGER_PSC5415A) || defined(CONFIG_CHARGER_BCT24157)
	if (extcon->vbus_on == is_on)
#else
	if (!vbus || extcon->vbus_on == is_on)
#endif
		return 0;

	dev_info(dev, "vbus turn %s\n", is_on ? "on" : "off");

	if (is_on) {
#if defined(CONFIG_CHARGER_PSC5415A) || defined(CONFIG_CHARGER_BCT24157)		
		charger_dev_enable_otg(chg_dev, is_on);
#else
		if (extcon->vbus_vol) {
			ret = regulator_set_voltage(vbus,
					extcon->vbus_vol, extcon->vbus_vol);
			if (ret) {
				dev_err(dev, "vbus regulator set voltage failed\n");
				return ret;
			}
		}

		if (extcon->vbus_cur) {
			ret = regulator_set_current_limit(vbus,
					extcon->vbus_cur, extcon->vbus_cur);
			if (ret) {
				dev_err(dev, "vbus regulator set current failed\n");
				return ret;
			}
		}

		ret = regulator_enable(vbus);
		if (ret) {
			dev_err(dev, "vbus regulator enable failed\n");
			return ret;
		}
		mobile_source_handler(1);
#endif
	} else {
#if defined(CONFIG_CHARGER_PSC5415A) || defined(CONFIG_CHARGER_BCT24157)
		charger_dev_enable_otg(chg_dev, is_on);
#else
		regulator_disable(vbus);
#endif
		mobile_source_handler(0);
	}

	extcon->vbus_on = is_on;

#endif //ADAPT_CHARGER_V1
	return 0;
}

#ifdef CONFIG_TCPC_CLASS
static int mtk_extcon_tcpc_notifier(struct notifier_block *nb,
		unsigned long event, void *data)
{
	struct tcp_notify *noti = data;
	struct mtk_extcon_info *extcon =
			container_of(nb, struct mtk_extcon_info, tcpc_nb);
	struct device *dev = extcon->dev;
	bool vbus_on;

	switch (event) {
	case TCP_NOTIFY_SOURCE_VBUS:
		dev_info(dev, "source vbus = %dmv\n",
				 noti->vbus_state.mv);
		vbus_on = (noti->vbus_state.mv) ? true : false;
		mtk_usb_extcon_set_vbus(extcon, vbus_on);
		break;
	case TCP_NOTIFY_TYPEC_STATE:
		dev_info(dev, "old_state=%d, new_state=%d\n",
				noti->typec_state.old_state,
				noti->typec_state.new_state);

#ifdef CONFIG_MTK_USB_TYPEC_U3_MUX
		if ((noti->typec_state.new_state == TYPEC_ATTACHED_SNK ||
			noti->typec_state.new_state == TYPEC_ATTACHED_NORP_SRC ||
			noti->typec_state.new_state == TYPEC_ATTACHED_CUSTOM_SRC)) {
			if (noti->typec_state.polarity == 0)
				usb3_switch_set(TYPEC_ORIENTATION_REVERSE);
			else
				usb3_switch_set(TYPEC_ORIENTATION_NORMAL);
		} else if (noti->typec_state.new_state == TYPEC_ATTACHED_SRC) {
			if (g_extcon->support_u3) {
				if (noti->typec_state.polarity == 0)
					usb3_switch_set(TYPEC_ORIENTATION_REVERSE);
				else
					usb3_switch_set(TYPEC_ORIENTATION_NORMAL);
			} else {
				if (noti->typec_state.polarity == 0)
					usb3_switch_set(TYPEC_ORIENTATION_NORMAL);
				else
					usb3_switch_set(TYPEC_ORIENTATION_REVERSE);
			}
		} else if (noti->typec_state.new_state == TYPEC_UNATTACHED) {
			usb3_switch_set(TYPEC_ORIENTATION_NONE);
		}
#endif
		if (noti->typec_state.old_state == TYPEC_UNATTACHED &&
			noti->typec_state.new_state == TYPEC_ATTACHED_SRC) {
			dev_info(dev, "Type-C SRC plug in\n");
			mtk_usb_extcon_set_role(extcon, DUAL_PROP_DR_HOST);
		} else if (!(extcon->bypss_typec_sink) &&
			noti->typec_state.old_state == TYPEC_UNATTACHED &&
			(noti->typec_state.new_state == TYPEC_ATTACHED_SNK ||
			noti->typec_state.new_state == TYPEC_ATTACHED_NORP_SRC ||
			noti->typec_state.new_state == TYPEC_ATTACHED_CUSTOM_SRC)) {
			dev_info(dev, "Type-C SINK plug in\n");
			mtk_usb_extcon_set_role(extcon, DUAL_PROP_DR_DEVICE);
		} else if ((noti->typec_state.old_state == TYPEC_ATTACHED_SRC ||
			noti->typec_state.old_state == TYPEC_ATTACHED_SNK ||
			noti->typec_state.old_state == TYPEC_ATTACHED_NORP_SRC ||
			noti->typec_state.old_state == TYPEC_ATTACHED_CUSTOM_SRC) &&
			noti->typec_state.new_state == TYPEC_UNATTACHED) {
			dev_info(dev, "Type-C plug out\n");
			mtk_usb_extcon_set_role(extcon, DUAL_PROP_DR_NONE);
#ifdef __CUST_TCPC_POLARITY_DETECT__
			update_tcpc_polarity(UP_TCPC_PLUGOUT);
#endif
		}
#ifdef __CUST_TCPC_POLARITY_DETECT__
		if(noti->typec_state.new_state == TYPEC_ATTACHED_SRC|| noti->typec_state.new_state == TYPEC_ATTACHED_SNK){
			update_tcpc_polarity(noti->typec_state.polarity);

		}
#endif
		break;
	case TCP_NOTIFY_DR_SWAP:
		dev_info(dev, "%s dr_swap, new role=%d\n",
				__func__, noti->swap_state.new_role);
		if (noti->swap_state.new_role == PD_ROLE_UFP &&
				extcon->c_role != DUAL_PROP_DR_DEVICE) {
			dev_info(dev, "switch role to device\n");
			mtk_usb_extcon_set_role(extcon, DUAL_PROP_DR_NONE);
			mtk_usb_extcon_set_role(extcon, DUAL_PROP_DR_DEVICE);
		} else if (noti->swap_state.new_role == PD_ROLE_DFP &&
				extcon->c_role != DUAL_PROP_DR_HOST) {
			dev_info(dev, "switch role to host\n");
			mtk_usb_extcon_set_role(extcon, DUAL_PROP_DR_NONE);
			mtk_usb_extcon_set_role(extcon, DUAL_PROP_DR_HOST);
		}
		break;
	}

	return NOTIFY_OK;
}

static int mtk_usb_extcon_tcpc_init(struct mtk_extcon_info *extcon)
{
	struct tcpc_device *tcpc_dev;
	int ret;

	tcpc_dev = tcpc_dev_get_by_name("type_c_port0");

	if (!tcpc_dev) {
		dev_err(extcon->dev, "get tcpc device fail\n");
		return -ENODEV;
	}

	extcon->tcpc_nb.notifier_call = mtk_extcon_tcpc_notifier;
	ret = register_tcp_dev_notifier(tcpc_dev, &extcon->tcpc_nb,
		TCP_NOTIFY_TYPE_USB | TCP_NOTIFY_TYPE_VBUS |
		TCP_NOTIFY_TYPE_MISC);
	if (ret < 0) {
		dev_err(extcon->dev, "register notifer fail\n");
		return -EINVAL;
	}

	extcon->tcpc_dev = tcpc_dev;

	return 0;
}
#endif

static void mtk_usb_extcon_detect_cable(struct work_struct *work)
{
	struct mtk_extcon_info *extcon = container_of(to_delayed_work(work),
						    struct mtk_extcon_info,
						    wq_detcable);
	int id;

	/* check ID and update cable state */
	id = extcon->id_gpiod ?
		gpiod_get_value_cansleep(extcon->id_gpiod) : 1;

	/* at first we clean states which are no longer active */
	if (id) {
#ifdef __CUST_UVC_VBUS_GPIO__
		gpio_set_value(up_uvc_vbus_gpio_num, 0);
		gpio_set_value(up_uvc_switch_gpio_num,0);
#else
		mtk_usb_extcon_set_vbus(extcon, false);
#endif
		mtk_usb_extcon_set_role(extcon, DUAL_PROP_DR_NONE);
	} else {
#ifdef __CUST_UVC_VBUS_GPIO__
		gpio_set_value(up_uvc_switch_gpio_num,1);
		gpio_set_value(up_uvc_vbus_gpio_num, 1);
#else
		mtk_usb_extcon_set_vbus(extcon, true);
#endif
		mtk_usb_extcon_set_role(extcon, DUAL_PROP_DR_HOST);
	}
}

static irqreturn_t mtk_usb_idpin_handle(int irq, void *dev_id)
{
	struct mtk_extcon_info *extcon = dev_id;

	/* issue detection work */
	queue_delayed_work(system_power_efficient_wq, &extcon->wq_detcable, 0);

	return IRQ_HANDLED;
}

static int mtk_usb_extcon_id_pin_init(struct mtk_extcon_info *extcon)
{
	int ret = 0;
	int id;

	extcon->id_gpiod = devm_gpiod_get(extcon->dev, "id", GPIOD_IN);

	if (!extcon->id_gpiod || IS_ERR(extcon->id_gpiod)) {
		dev_err(extcon->dev, "failed to get id gpio\n");
		extcon->id_gpiod = NULL;
		return -EINVAL;
	}

	extcon->id_irq = gpiod_to_irq(extcon->id_gpiod);
	if (extcon->id_irq < 0) {
		dev_err(extcon->dev, "failed to get ID IRQ\n");
		extcon->id_gpiod = NULL;
		return -EINVAL;
	}

	INIT_DELAYED_WORK(&extcon->wq_detcable, mtk_usb_extcon_detect_cable);

	ret = devm_request_threaded_irq(extcon->dev, extcon->id_irq, NULL,
			mtk_usb_idpin_handle, IRQF_TRIGGER_RISING |
			IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			dev_name(extcon->dev), extcon);

	if (ret < 0) {
		dev_err(extcon->dev, "failed to request handler for ID IRQ\n");
		extcon->id_gpiod = NULL;
		return ret;
	}
	enable_irq_wake(extcon->id_irq);

	// get id pin value when boot on
	id = extcon->id_gpiod ?	gpiod_get_value_cansleep(extcon->id_gpiod) : 1;
	dev_info(extcon->dev, "id value : %d\n", id);
	if (!id) {
		mtk_usb_extcon_set_vbus(extcon, true);
		mtk_usb_extcon_set_role(extcon, DUAL_PROP_DR_HOST);
	}

	return 0;
}

#if defined ADAPT_PSY_V1
static void issue_connection_work(unsigned int dr)
{
	if (!g_extcon) {
		pr_info("g_extcon = NULL\n");
		return;
	}

	/* issue connection work */
	mtk_usb_extcon_set_role(g_extcon, dr);
}

void mt_usb_connect_v1(void)
{
	pr_info("%s in mtk extcon\n", __func__);

#if (defined CONFIG_TCPC_CLASS) || (defined CONFIG_CHRDET_VBUS_DETECTION)
	/* check current role to avoid power role swap issue */
	if (g_extcon && g_extcon->c_role == DUAL_PROP_DR_NONE)
		issue_connection_work(DUAL_PROP_DR_DEVICE);
#else
	issue_connection_work(DUAL_PROP_DR_DEVICE);
#endif
}
EXPORT_SYMBOL_GPL(mt_usb_connect_v1);

void mt_usb_disconnect_v1(void)
{
	pr_info("%s  in mtk extcon\n", __func__);
#ifdef CONFIG_TCPC_CLASS
	/* disconnect by tcpc notifier */
#else
	issue_connection_work(DUAL_PROP_DR_NONE);
#endif
}
EXPORT_SYMBOL_GPL(mt_usb_disconnect_v1);
#endif //ADAPT_PSY_V1

#ifdef CONFIG_WIRELESS_POWER_MT5728
#include<linux/workqueue.h>
struct mtk_extcon_info *wlextcon;
static struct workqueue_struct *wl_wq;
static struct delayed_work dwork;
static int wl_ops;

static void do_vbus_work(struct work_struct *work)
{

	bool vbus_on = !!wl_ops;

	mtk_usb_extcon_set_vbus(wlextcon, vbus_on);


}

static void issue_vbus_work(int ops, int delay)
{

	INIT_DELAYED_WORK(&dwork, do_vbus_work);
	wl_ops = ops;
	/* issue vbus work */
	pr_info("issue work, ops<%d>, delay<%d>\n", ops, delay);

	queue_delayed_work(wl_wq,
				&dwork, msecs_to_jiffies(delay));
}


void mt_usb_vbus_on(int delay)
{
	pr_info("vbus_on\n");
	issue_vbus_work(true, delay);
}

void mt_usb_vbus_off(int delay)
{
	pr_info("vbus_off\n");
	issue_vbus_work(false, delay);
}
#endif



#ifdef CONFIG_CHRDET_VBUS_DETECTION
	extern bool mtk_mt_pmic_get_vcdt(void);
#endif



#ifdef __CUST_TCPC_POLARITY_DETECT__
static ssize_t up_tcpc_polarity_show(struct device *dev, struct device_attribute *attr, char *buf)
{


	return scnprintf(buf, PAGE_SIZE, "%d \n", up_tcpc_polarity_val);
}
DEVICE_ATTR(up_tcpc_polarity, 0664, up_tcpc_polarity_show, NULL);
#endif

static int mtk_usb_extcon_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_extcon_info *extcon;
	struct platform_device *conn_pdev;
	struct device_node *conn_np;
	int ret;
#ifdef CONFIG_CHRDET_VBUS_DETECTION
	bool is_vcdt_on;
#endif


	#ifdef __CUST_WIRELESS_CHARGE_SUPPORT__
		#if __CUST_WIRELESS_CHARGE_SUPPORT__
			if(0 > get_wireless_gpio())
				pr_info("get_wireless_gpio failed.\n");	
		#endif
	#endif
	#ifdef CONFIG_WIRELESS_POWER_MT5728			
			gpio_direction_input(up_wireless_charge_gpio_num); //mt5728 wireless charge pin need default output status		
	#endif

	extcon = devm_kzalloc(&pdev->dev, sizeof(*extcon), GFP_KERNEL);
	if (!extcon)
		return -ENOMEM;

#ifdef CONFIG_WIRELESS_POWER_MT5728
	wlextcon = extcon;
#endif
	extcon->dev = dev;

	/* extcon */
	extcon->edev = devm_extcon_dev_allocate(dev, usb_extcon_cable);
	if (IS_ERR(extcon->edev)) {
		dev_err(dev, "failed to allocate extcon device\n");
		return -ENOMEM;
	}

	ret = devm_extcon_dev_register(dev, extcon->edev);
	if (ret < 0) {
		dev_info(dev, "failed to register extcon device\n");
		return ret;
	}

	/* usb role switch */
	conn_np = of_parse_phandle(dev->of_node, "dev-conn", 0);
	if (!conn_np) {
		dev_info(dev, "failed to get dev-conn node\n");
		return -EINVAL;
	}

	conn_pdev = of_find_device_by_node(conn_np);
	if (!conn_pdev) {
		dev_info(dev, "failed to get dev-conn pdev\n");
		return -EINVAL;
	}

	extcon->dev_conn.endpoint[0] = kasprintf(GFP_KERNEL,
				"%s-role-switch", dev_name(&conn_pdev->dev));
	extcon->dev_conn.endpoint[1] = dev_name(extcon->dev);
	extcon->dev_conn.id = "usb-role-switch";
	device_connection_add(&extcon->dev_conn);

	extcon->role_sw = usb_role_switch_get(extcon->dev);
	if (IS_ERR(extcon->role_sw)) {
		dev_err(dev, "failed to get usb role\n");
		return PTR_ERR(extcon->role_sw);
	}

	/* vbus */
#if defined(CONFIG_CHARGER_PSC5415A) || defined(CONFIG_CHARGER_BCT24157)
	
#else
	extcon->vbus = devm_regulator_get(dev, "vbus");
	if (IS_ERR(extcon->vbus)) {
		dev_err(dev, "failed to get vbus\n");
		return PTR_ERR(extcon->vbus);
	}

#ifndef CONFIG_CHRDET_VBUS_DETECTION
	if (!of_property_read_u32(dev->of_node, "vbus-voltage",
					&extcon->vbus_vol))
		dev_info(dev, "vbus-voltage=%d", extcon->vbus_vol);

	if (!of_property_read_u32(dev->of_node, "vbus-current",
					&extcon->vbus_cur))
		dev_info(dev, "vbus-current=%d", extcon->vbus_cur);
#endif
#endif
	extcon->bypss_typec_sink =
		of_property_read_bool(dev->of_node,
			"mediatek,bypss-typec-sink");

	extcon->extcon_wq = create_singlethread_workqueue("extcon_usb");
	if (!extcon->extcon_wq)
		return -ENOMEM;

#ifdef CONFIG_WIRELESS_POWER_MT5728
	wl_wq = create_singlethread_workqueue("wl_workq");
#endif
	extcon->support_u3 = !of_property_read_bool(dev->of_node, "not_support_u3");
	if (!extcon->support_u3)
		dev_info(dev, "platform does not support U3\n");

	extcon->c_role = DUAL_PROP_DR_DEVICE;

	/* default initial role */
#ifdef CONFIG_CHRDET_VBUS_DETECTION
	is_vcdt_on = mtk_mt_pmic_get_vcdt();
	dev_info(dev, "extcon vcdt %d\n", is_vcdt_on);
	if (is_vcdt_on)
		mtk_usb_extcon_set_role(extcon, DUAL_PROP_DR_DEVICE);
	else
		mtk_usb_extcon_set_role(extcon, DUAL_PROP_DR_NONE);
#else
	mtk_usb_extcon_set_role(extcon, DUAL_PROP_DR_NONE);
#endif
	/* default turn off vbus */
	mtk_usb_extcon_set_vbus(extcon, false);

	/*get id resources*/
	ret = mtk_usb_extcon_id_pin_init(extcon);
	if (ret < 0)
		dev_info(dev, "failed to init id pin\n");

#ifdef __CUST_UVC_VBUS_GPIO__
		up_uvc_vbus_gpio_num = up_parse_dts(dev->of_node, "uvc_vbus_gpio");

		if (0 > up_uvc_vbus_gpio_num)
		{
			dev_info(dev,"%s: up_parse_dts fail. \n", __func__);
			
		}


		up_uvc_switch_gpio_num = up_parse_dts(dev->of_node, "uvc_switch_gpio");

		if (0 > up_uvc_switch_gpio_num)
		{
			dev_info(dev,"%s: up_parse_dts fail. \n", __func__);
			
		}

	
#endif
	/* power psy */
	ret = mtk_usb_extcon_psy_init(extcon);
	if (ret < 0)
		dev_err(dev, "failed to init psy\n");

#ifdef CONFIG_TCPC_CLASS
	/* tcpc */
	ret = mtk_usb_extcon_tcpc_init(extcon);
	if (ret < 0)
		dev_err(dev, "failed to init tcpc\n");
#endif

	g_extcon = extcon;

	platform_set_drvdata(pdev, extcon);

	#ifdef __CUST_OTG_GPIO_SELECT__
	   	#if __CUST_OTG_GPIO_SELECT__
			if(0 > get_otg_select_gpio()) {
				printk("UP_OTG_get_otg_select_gpio failed.\n");	
			}
			#ifdef CONFIG_WIRELESS_POWER_MT5728
				gpio_direction_input(up_otg_select_gpio_num);
			#endif
	  	#endif
	#endif

#ifdef __CUST_TCPC_POLARITY_DETECT__
		ret = device_create_file(dev, &dev_attr_up_tcpc_polarity);
		if (ret)
			dev_err(dev, "failed to create polarity attr\n");
#endif

	return 0;
}

static int mtk_usb_extcon_remove(struct platform_device *pdev)
{
	struct mtk_extcon_info *extcon = platform_get_drvdata(pdev);

	if (extcon->dev_conn.id)
		device_connection_remove(&extcon->dev_conn);

	return 0;
}

static void mtk_usb_extcon_shutdown(struct platform_device *pdev)
{
	struct mtk_extcon_info *extcon = platform_get_drvdata(pdev);

	if (extcon->c_role == DUAL_PROP_DR_HOST) {
		dev_info(extcon->dev, "set host vbus off when shutdown\n");
		mtk_usb_extcon_set_vbus(extcon, false);
	}
}

static const struct of_device_id mtk_usb_extcon_of_match[] = {
	{ .compatible = "mediatek,extcon-usb", },
	{ },
};
MODULE_DEVICE_TABLE(of, mtk_usb_extcon_of_match);

static struct platform_driver mtk_usb_extcon_driver = {
	.probe		= mtk_usb_extcon_probe,
	.remove		= mtk_usb_extcon_remove,
	.shutdown	= mtk_usb_extcon_shutdown,
	.driver		= {
		.name	= "mtk-extcon-usb",
		.of_match_table = mtk_usb_extcon_of_match,
	},
};

static int __init mtk_usb_extcon_init(void)
{
	return platform_driver_register(&mtk_usb_extcon_driver);
}
late_initcall_sync(mtk_usb_extcon_init);

static void __exit mtk_usb_extcon_exit(void)
{
	platform_driver_unregister(&mtk_usb_extcon_driver);
}
module_exit(mtk_usb_extcon_exit);

