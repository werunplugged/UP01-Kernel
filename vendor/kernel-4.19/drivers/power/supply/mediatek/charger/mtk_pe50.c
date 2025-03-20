/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
*/

#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <mt-plat/v1/prop_chgalgo_class.h>
#include "mtk_intf.h"

static struct pe50 *pe5;

static int __pe50_notifier_call(struct notifier_block *nb, unsigned long event,
				void *data)
{
	chr_info("%s %s\n", __func__, prop_chgalgo_notify_evt_tostring(event));
	switch (event) {
	case PCA_NOTIEVT_ALGO_STOP:
		wake_up_charger();
		break;
	default:
		break;
	}
	return 0;
}

int pe50_stop(void)
{
	if (pe5 == NULL) {
		chr_err("%s, pe5 is NULL\n", __func__);
		return -ENODEV;
	}

	if (pe5->online == true) {
		chr_err("%s\n", __func__);
		enable_vbus_ovp(true);
		pe5->online = false;
		pe5->state = PE50_INIT;
	}

	return 0;
}

bool pe50_is_ready(void)
{
	if (pe5 == NULL) {
		chr_info("%s: pe5 is NULL\n", __func__);
		return false;
	}

	return prop_chgalgo_is_algo_ready(pe5->pca_algo);
}

int pe50_init(void)
{
	int ret = -EBUSY;
	struct pe50 *pe50 = NULL;

	chr_info("%s: start\n", __func__);
	pe50 = kzalloc(sizeof(struct pe50), GFP_KERNEL);
	if (pe50 == NULL) {
		chr_err("%s: [PE50] pe50 alloc fail\n", __func__);
		return -ENOMEM;
	}

	pe5 = pe50;
	pe5->state = PE50_INIT;
	pe50->pca_algo = prop_chgalgo_dev_get_by_name("pca_algo_dv2");
	if (!pe50->pca_algo) {
		chr_err("[PE50] Get pca_algo fail\n");
		ret = -EINVAL;
	} else {
		ret = prop_chgalgo_init_algo(pe50->pca_algo);
		if (ret < 0) {
			chr_err("[PE50] Init algo fail (%d)\n", ret);
			pe50->pca_algo = NULL;
			goto out;
		}
		pe50->nb.notifier_call = __pe50_notifier_call;
		ret = prop_chgalgo_notifier_register(pe50->pca_algo,
						     &pe50->nb);
		if (ret < 0)
			chr_err("[PE50] chgalgo_notifier_register fail(%d)\n", ret);
	}
	return ret;

out:
	return ret;
}

int pe50_run(void)
{
	int ret = 0;
	bool running;

	chr_info("%s: pe5 state = %d\n", __func__, pe5->state);
	switch (pe5->state) {
	case PE50_INIT:
		enable_vbus_ovp(false);
		pe5->online = true;
		ret = prop_chgalgo_start_algo(pe5->pca_algo);
		if (ret < 0) {
			enable_vbus_ovp(true);
			chr_err("%s: start_algo fail(%d)\n", __func__, ret);
			break;
		}

		pe5->state = PE50_RUNNING;
		break;

	case PE50_RUNNING:
		running = prop_chgalgo_is_algo_running(pe5->pca_algo);
		if (!running){
			enable_vbus_ovp(true);
			ret = 2;
		}
		chr_info("%s: running = %d\n", __func__, running);
		break;
	}

	return ret;
}
