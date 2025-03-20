// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2018-2020, The Linux Foundation. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include "hl5280-i2c.h"
#include <tcpm.h>	
#include <linux/of.h>
#include <linux/usb/typec.h>
#include <linux/usb/typec_mux.h>

#define HL5280_I2C_NAME	"hl5280-driver"

#define HL5280_SWITCH_SETTINGS 0x04
#define HL5280_SWITCH_CONTROL  0x05
#define HL5280_SWITCH_STATUS1  0x07
#define HL5280_SLOW_L          0x08
#define HL5280_SLOW_R          0x09
#define HL5280_SLOW_MIC        0x0A
#define HL5280_SLOW_SENSE      0x0B
#define HL5280_SLOW_GND        0x0C
#define HL5280_DELAY_L_R       0x0D
#define HL5280_DELAY_L_MIC     0x0E
#define HL5280_DELAY_L_SENSE   0x0F
#define HL5280_DELAY_L_AGND    0x10
#define HL5280_RESET           0x1E


extern struct typec_switch *mtk_typec_switch_register(struct device *dev,
			const struct typec_switch_desc *desc);
extern void mtk_typec_switch_unregister(struct typec_switch *sw);

struct tag_bootmode {
	u32 size;
	u32 tag;
	u32 bootmode;
	u32 boottype;
};

static struct i2c_client *hl_client;

struct hl5280 {
	struct regmap *regmap;
	struct device *dev;
	struct typec_switch *sw;
	struct power_supply *usb_psy;
	struct notifier_block psy_nb;
	atomic_t usbc_mode;
	struct work_struct usbc_analog_work;
	struct blocking_notifier_head hl5280_notifier;
	struct mutex notification_lock;
	struct tcpc_device *tcpc;
	struct notifier_block pd_nb;
};

struct hl5280_reg_val {
	u16 reg;
	u8 val;
};

static const struct regmap_config hl5280_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = HL5280_RESET,
};

static const struct hl5280_reg_val fsa_reg_i2c_defaults[] = {
	{HL5280_SLOW_L, 0x00},
	{HL5280_SLOW_R, 0x00},
	{HL5280_SLOW_MIC, 0x00},
	{HL5280_SLOW_SENSE, 0x00},
	{HL5280_SLOW_GND, 0x00},
	{HL5280_DELAY_L_R, 0x00},
	{HL5280_DELAY_L_MIC, 0x00},
	{HL5280_DELAY_L_SENSE, 0x00},
	{HL5280_DELAY_L_AGND, 0x09},
	{HL5280_SWITCH_SETTINGS, 0x98},
};

static void hl5280_usbc_update_settings(struct hl5280 *fsa_priv,
		u32 switch_control, u32 switch_enable)
{
#if 0
	int i;
	int value[32];
#endif
	if (!fsa_priv->regmap) {
		dev_err(fsa_priv->dev, "%s: regmap invalid\n", __func__);
		return;
	}

	regmap_write(fsa_priv->regmap, HL5280_SWITCH_SETTINGS, 0x80);
	regmap_write(fsa_priv->regmap, HL5280_SWITCH_CONTROL, switch_control);
	/* HL5280 chip hardware requirement */
	usleep_range(50, 55);
	regmap_write(fsa_priv->regmap, HL5280_SWITCH_SETTINGS, switch_enable);
/*
	for(i=0;i<0x1e;i++){
		regmap_read(fsa_priv->regmap, i, &value[i]);
		pr_info("reg[%x]=0x%x\n",i,value[i]);
	}
*/

}

static int accdet_tcp_notifier_call(struct notifier_block *pnb,
				unsigned long event, void *data)
{
	struct tcp_notify *noti = data;
	uint8_t old_state = TYPEC_UNATTACHED, new_state = TYPEC_UNATTACHED;
	struct hl5280 *fsa_priv =
			container_of(pnb, struct hl5280, pd_nb);


	switch (event) {
	case TCP_NOTIFY_TYPEC_STATE:
		old_state = noti->typec_state.old_state;
		new_state = noti->typec_state.new_state;
		if (old_state == TYPEC_UNATTACHED &&
			   new_state == TYPEC_ATTACHED_AUDIO) {
			atomic_set(&(fsa_priv->usbc_mode), 1);
			pm_stay_awake(fsa_priv->dev);
			queue_work(system_freezable_wq, &fsa_priv->usbc_analog_work);
			pr_info("%s Audio plug in111\n", __func__);
		} else if (old_state == TYPEC_ATTACHED_AUDIO &&
			   new_state == TYPEC_UNATTACHED) {
			atomic_set(&(fsa_priv->usbc_mode), 0);
			pm_stay_awake(fsa_priv->dev);
			queue_work(system_freezable_wq, &fsa_priv->usbc_analog_work);
			pr_info("%s Audio plug out111\n", __func__);
		}
		//break;

	}
	return NOTIFY_OK;
}

static int hl5280_usbc_analog_setup_switches(struct hl5280 *fsa_priv)
{
	int rc = 0;
	
	struct device *dev;

	if (!fsa_priv)
		return -EINVAL;
	dev = fsa_priv->dev;
	if (!dev)
		return -EINVAL;

	mutex_lock(&fsa_priv->notification_lock);
	/* get latest mode again within locked context */

	pr_info("%s!!\n",__func__);

	switch (atomic_read(&(fsa_priv->usbc_mode))) {
	/* add all modes FSA should notify for in here */
	case 1:
		pr_info("%s!!!\n",__func__);
		/* activate switches */
		hl5280_usbc_update_settings(fsa_priv, 0x00, 0x9F);



		break;
	case 0:
		pr_info("%s!!!!\n",__func__);



		/* deactivate switches */
		hl5280_usbc_update_settings(fsa_priv, 0x18, 0x98);
		break;
	default:
		/* ignore other usb connection modes */
		break;
	}


	mutex_unlock(&fsa_priv->notification_lock);
	return rc;
}




/*
 * hl5280_reg_notifier - register notifier block with fsa driver
 *
 * @nb - notifier block of hl5280
 *
 * Returns 0 on success, or error code
 */
int hl5280_reg_notifier(struct notifier_block *nb)
{
	int rc = 0;
	struct i2c_client *client = hl_client;
	struct hl5280 *fsa_priv;

	if (!client)
		return -EINVAL;

	fsa_priv = (struct hl5280 *)i2c_get_clientdata(client);
	if (!fsa_priv)
		return -EINVAL;

	rc = blocking_notifier_chain_register
				(&fsa_priv->hl5280_notifier, nb);
	if (rc)
		return rc;

	/*
	 * as part of the init sequence check if there is a connected
	 * USB C analog adapter
	 */
	dev_dbg(fsa_priv->dev, "%s: verify if USB adapter is already inserted\n",
		__func__);
	rc = hl5280_usbc_analog_setup_switches(fsa_priv);

	return rc;
}
EXPORT_SYMBOL(hl5280_reg_notifier);

/*
 * hl5280_unreg_notifier - unregister notifier block with fsa driver
 *
 * @nb - notifier block of hl5280
 *
 * Returns 0 on pass, or error code
 */
int hl5280_unreg_notifier(struct notifier_block *nb)
{
	int rc = 0;
	struct i2c_client *client = hl_client;
	struct hl5280 *fsa_priv;
	struct device *dev;
	union power_supply_propval mode;

	if (!client)
		return -EINVAL;

	fsa_priv = (struct hl5280 *)i2c_get_clientdata(client);
	if (!fsa_priv)
		return -EINVAL;
	dev = fsa_priv->dev;
	if (!dev)
		return -EINVAL;

	mutex_lock(&fsa_priv->notification_lock);
	/* get latest mode within locked context */
	rc = power_supply_get_property(fsa_priv->usb_psy,
			POWER_SUPPLY_PROP_TYPEC_MODE, &mode);
	if (rc) {
		dev_dbg(dev, "%s: Unable to read USB TYPEC_MODE: %d\n",
			__func__, rc);
		goto done;
	}
	/* Do not reset switch settings for usb digital hs */
	if (mode.intval == POWER_SUPPLY_TYPEC_SINK_AUDIO_ADAPTER)
		hl5280_usbc_update_settings(fsa_priv, 0x18, 0x98);
	rc = blocking_notifier_chain_unregister
					(&fsa_priv->hl5280_notifier, nb);
done:
	mutex_unlock(&fsa_priv->notification_lock);
	return rc;
}
EXPORT_SYMBOL(hl5280_unreg_notifier);

static int hl5280_validate_display_port_settings(struct hl5280 *fsa_priv)
{
	u32 switch_status = 0;

	regmap_read(fsa_priv->regmap, HL5280_SWITCH_STATUS1, &switch_status);

	if ((switch_status != 0x23) && (switch_status != 0x1C)) {
		pr_err("AUX SBU1/2 switch status is invalid = %u\n",
				switch_status);
		return -EIO;
	}

	return 0;
}
/*
 * hl5280_switch_event - configure FSA switch position based on event
 *
 * @event - fsa_function enum
 *
 * Returns int on whether the switch happened or not
 */
int hl5280_switch_event(enum fsa_function event)
{
	int switch_control = 0;
	struct i2c_client *client = hl_client;
	struct hl5280 *fsa_priv;

	if (!client)
		return -EINVAL;

	fsa_priv = (struct hl5280 *)i2c_get_clientdata(client);
	if (!fsa_priv)
		return -EINVAL;
	if (!fsa_priv->regmap)
		return -EINVAL;

	switch (event) {
	case FSA_MIC_GND_SWAP:
		regmap_read(fsa_priv->regmap, HL5280_SWITCH_CONTROL,
				&switch_control);
		if ((switch_control & 0x07) == 0x07)
			switch_control = 0x0;
		else
			switch_control = 0x7;
		hl5280_usbc_update_settings(fsa_priv, switch_control, 0x9F);
		break;
	case FSA_USBC_ORIENTATION_CC1:
		hl5280_usbc_update_settings(fsa_priv, 0x18, 0xF8);
		return hl5280_validate_display_port_settings(fsa_priv);
	case FSA_USBC_ORIENTATION_CC2:
		hl5280_usbc_update_settings(fsa_priv, 0x78, 0xF8);
		return hl5280_validate_display_port_settings(fsa_priv);
	case FSA_USBC_DISPLAYPORT_DISCONNECTED:
		hl5280_usbc_update_settings(fsa_priv, 0x18, 0x98);
		break;
	default:
		break;
	}

	return 0;
}
EXPORT_SYMBOL(hl5280_switch_event);

void hl5280_switch_mic(void)
{
	hl5280_switch_event(FSA_MIC_GND_SWAP);

}
EXPORT_SYMBOL(hl5280_switch_mic);

static int hl5280_switch_set(struct typec_switch *sw,
			      enum typec_orientation orientation)
{
	struct hl5280 *fsa_priv = typec_switch_get_drvdata(sw);

	dev_info(fsa_priv->dev, "%s %d\n", __func__, orientation);

	switch (orientation) {
	case TYPEC_ORIENTATION_NONE:
		/* switch off */
		break;
	case TYPEC_ORIENTATION_NORMAL:
		/* switch cc1 side */
		break;
	case TYPEC_ORIENTATION_REVERSE:
		/* auto switch cc side */
		hl5280_switch_mic();
		break;
	default:
		break;
	}

	return 0;
}


static void hl5280_usbc_analog_work_fn(struct work_struct *work)
{
	struct hl5280 *fsa_priv =
		container_of(work, struct hl5280, usbc_analog_work);
	pr_info("%s!!\n",__func__);
	if (!fsa_priv) {
		pr_err("%s: fsa container invalid\n", __func__);
		return;
	}
	pr_info("%s!!!\n",__func__);
	hl5280_usbc_analog_setup_switches(fsa_priv);
	pm_relax(fsa_priv->dev);
}

static void hl5280_update_reg_defaults(struct regmap *regmap)
{
	u8 i;

	for (i = 0; i < ARRAY_SIZE(fsa_reg_i2c_defaults); i++)
		regmap_write(regmap, fsa_reg_i2c_defaults[i].reg,
				   fsa_reg_i2c_defaults[i].val);
}



static int hl5280_probe(struct i2c_client *i2c,
			 const struct i2c_device_id *id)
{
	struct hl5280 *fsa_priv;
	int rc = 0;
	struct typec_switch_desc sw_desc;
	sw_desc.drvdata = fsa_priv;
	sw_desc.fwnode = i2c->dev.fwnode;
	sw_desc.set = hl5280_switch_set;


	printk("%s\n",__func__);	
	fsa_priv = devm_kzalloc(&i2c->dev, sizeof(*fsa_priv),
				GFP_KERNEL);
	if (!fsa_priv)
		return -ENOMEM;
	hl_client = i2c;
	fsa_priv->dev = &i2c->dev;

	fsa_priv->regmap = devm_regmap_init_i2c(i2c, &hl5280_regmap_config);
	if (IS_ERR_OR_NULL(fsa_priv->regmap)) {
		dev_err(fsa_priv->dev, "%s: Failed to initialize regmap: %d\n",
			__func__, rc);
		if (!fsa_priv->regmap) {
			rc = -EINVAL;
			goto err_supply;
		}
		rc = PTR_ERR(fsa_priv->regmap);
		goto err_supply;
	}

	hl5280_update_reg_defaults(fsa_priv->regmap);


	fsa_priv->tcpc = tcpc_dev_get_by_name("type_c_port0");
	if (fsa_priv->tcpc == NULL) {
		pr_notice("%s: get tcpc dev fail\n", __func__);
		rc = -ENODEV;
		goto err_supply;
	}
	fsa_priv->pd_nb.notifier_call = accdet_tcp_notifier_call;
	rc = register_tcp_dev_notifier(fsa_priv->tcpc,
		&fsa_priv->pd_nb, TCP_NOTIFY_TYPE_ALL);
	if (rc < 0) {
		pr_notice("%s: register tcpc notifier fail(%d)\n",
			  __func__, rc);
		goto err_supply;
	}


	mutex_init(&fsa_priv->notification_lock);
	i2c_set_clientdata(i2c, fsa_priv);

	INIT_WORK(&fsa_priv->usbc_analog_work,
		  hl5280_usbc_analog_work_fn);

	fsa_priv->hl5280_notifier.rwsem =
		(struct rw_semaphore)__RWSEM_INITIALIZER
		((fsa_priv->hl5280_notifier).rwsem);
	fsa_priv->hl5280_notifier.head = NULL;

	fsa_priv->sw = mtk_typec_switch_register(&i2c->dev, &sw_desc);
	if (IS_ERR(fsa_priv->sw)) {
		dev_info(&i2c->dev, "error registering typec switch: %ld\n",
			PTR_ERR(fsa_priv->sw));
		return PTR_ERR(fsa_priv->sw);
	}
	typec_switch_set_drvdata(fsa_priv->sw, fsa_priv);
	return 0;

err_supply:

	devm_kfree(&i2c->dev, fsa_priv);
	return rc;
}

static int hl5280_remove(struct i2c_client *i2c)
{
	struct hl5280 *fsa_priv =
			(struct hl5280 *)i2c_get_clientdata(i2c);

	if (!fsa_priv)
		return -EINVAL;
	mtk_typec_switch_unregister(fsa_priv->sw);
	hl5280_usbc_update_settings(fsa_priv, 0x18, 0x98);
	cancel_work_sync(&fsa_priv->usbc_analog_work);
	pm_relax(fsa_priv->dev);
	/* deregister from PMI */
	power_supply_unreg_notifier(&fsa_priv->psy_nb);

	mutex_destroy(&fsa_priv->notification_lock);
	dev_set_drvdata(&i2c->dev, NULL);

	return 0;
}

static const struct of_device_id hl5280_i2c_dt_match[] = {
	{
		.compatible = "mediatek,hl5280-i2c",
	},
	{}
};

static struct i2c_driver hl5280_i2c_driver = {
	.driver = {
		.name = HL5280_I2C_NAME,
		.of_match_table = hl5280_i2c_dt_match,
	},
	.probe = hl5280_probe,
	.remove = hl5280_remove,
};

static int __init hl5280_init(void)
{
	int rc;
	struct device_node *boot_node = NULL;
	struct tag_bootmode *tag = NULL;
	int boot_mode = 11;//UNKNOWN_BOOT
	boot_node = of_find_node_by_name(NULL, "chosen");
	if (!boot_node){
		pr_info("%s: failed to get boot mode phandle\n", __func__);
	}
	else {
		tag = (struct tag_bootmode *)of_get_property(boot_node,
							"atag,boot", NULL);
		if (!tag){
			pr_info("%s: failed to get atag,boot\n", __func__);
		}
		else
			boot_mode = tag->bootmode;
	}
	pr_info("%s: bootmode %d\n", __func__,boot_mode);
	if(boot_mode == 1 || boot_mode == 5)
		return 0;

	pr_err("hl5280: init I2C driver: %d\n", rc);
	rc = i2c_add_driver(&hl5280_i2c_driver);
	if (rc)
		pr_err("hl5280: Failed to register I2C driver: %d\n", rc);

	return rc;
}
late_initcall_sync(hl5280_init);

static void __exit hl5280_exit(void)
{
	i2c_del_driver(&hl5280_i2c_driver);
}
module_exit(hl5280_exit);

MODULE_DESCRIPTION("HL5280 I2C driver");
MODULE_LICENSE("GPL v2");
