/*
 * Copyright (C) 2020 Richtek Technology Corp.
 *
 * Richtek Fast Charge TA for Proprietary Charging Algorithm
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <mt-plat/v1/prop_chgalgo_class.h>
#include <mt-plat/v1/prop_ta_class.h>
#include <mt-plat/v1/charger_class.h>
#include <tcpm.h>

#define PCA_RFC_TA_VERSION	"2.0.0_G"
#define RFC_STAT_MODE	BIT(0)
#define RFC_STAT_PSRDY	BIT(3)
#define RFC_STAT_OVP	BIT(16)
#define RFC_STAT_OTP1	BIT(17)
#define RFC_STAT_OTP2	BIT(18)
#define RFC_STAT_UVP	BIT(19)
#define RFC_STAT_OCP	BIT(20)
#define RFC_RECV_HARDRESET_MAX	5

struct pca_rfc_info {
	struct device *dev;
	struct prop_chgalgo_device *pca;
	struct prop_ta_classdev *rfc;
	struct tcpc_device *tcpc;
	struct notifier_block tcp_nb;
	struct charger_device *chg;
	int hrst_cnt;
};

static int pca_rfc_enable_charging(struct prop_chgalgo_device *pca, bool en,
				   u32 mV, u32 mA)
{
	int ret;
	struct pca_rfc_info *info = prop_chgalgo_get_drvdata(pca);

	ret = prop_ta_device_set_voltage_current(info->rfc, mV, mA);
	if (ret < 0) {
		PCA_ERR("set voltage current fail(%d)\n", ret);
		return ret;
	}
	PCA_DBG("en = %d, %dmV, %dmA\n", en, mV, mA);
	return 0;
}

static int pca_rfc_set_cap(struct prop_chgalgo_device *pca, u32 mV, u32 mA)
{
	int ret;
	struct pca_rfc_info *info = prop_chgalgo_get_drvdata(pca);

	ret = prop_ta_device_set_voltage_current(info->rfc, mV, mA);
	if (ret < 0) {
		PCA_ERR("set voltage current fail(%d)\n", ret);
		return ret;
	}
	PCA_DBG("%dmV, %dmA\n", mV, mA);
	return 0;
}

static int pca_rfc_get_measure_cap(struct prop_chgalgo_device *pca, u32 *mV,
				   u32 *mA)
{
	int ret;
	struct pca_rfc_info *info = prop_chgalgo_get_drvdata(pca);

	ret = prop_ta_device_get_output_voltage_current(info->rfc, mV, mA);
	if (ret < 0) {
		PCA_ERR("get output voltage current fail(%d)\n", ret);
		return ret;
	}
	PCA_DBG("%dmV, %dmA\n", *mV, *mA);
	return 0;
}

static int pca_rfc_get_temperature(struct prop_chgalgo_device *pca, int *temp)
{
	int ret;
	struct pca_rfc_info *info = prop_chgalgo_get_drvdata(pca);

	ret = prop_ta_device_get_temp1(info->rfc, temp);
	if (ret < 0) {
		PCA_ERR("get temp fail(%d)\n", ret);
		return ret;
	}
	PCA_DBG("%d degree\n", *temp);
	return 0;
}

static int pca_rfc_get_status(struct prop_chgalgo_device *pca,
			      struct prop_chgalgo_ta_status *ta_status)
{
	int ret;
	struct pca_rfc_info *info = prop_chgalgo_get_drvdata(pca);
	u32 status;

	ret = prop_ta_device_get_status(info->rfc, &status);
	if (ret < 0) {
		PCA_ERR("get status fail(%d)\n", ret);
		return ret;
	}
	ret = prop_ta_device_get_temp1(info->rfc, &ta_status->temp1);
	if (ret < 0) {
		PCA_ERR("get temp1 fail(%d)\n", ret);
		return ret;
	}
	ret = prop_ta_device_get_temp2(info->rfc, &ta_status->temp2);
	if (ret < 0) {
		PCA_ERR("get temp2 fail(%d)\n", ret);
		return ret;
	}

	ta_status->present_input = status & RFC_STAT_PSRDY;
	ta_status->ocp = (status & RFC_STAT_OCP) ? true : false;
	ta_status->otp = (status & (RFC_STAT_OTP1 | RFC_STAT_OTP2)) ? true
								    : false;
	ta_status->ovp = (status & RFC_STAT_OVP) ? true : false;
	ta_status->temp_level =
		(status & (RFC_STAT_OTP1 | RFC_STAT_OTP2)) >> 17;
	return 0;
}

static int pca_rfc_is_cc(struct prop_chgalgo_device *pca, bool *cc)
{
	int ret;
	struct pca_rfc_info *info = prop_chgalgo_get_drvdata(pca);
	u32 status;

	ret = prop_ta_device_get_status(info->rfc, &status);
	if (ret < 0) {
		PCA_ERR("get status fail(%d)\n", ret);
		return ret;
	}
	*cc = (status & RFC_STAT_MODE) ? true : false;
	return 0;
}

static int pca_rfc_send_hardreset(struct prop_chgalgo_device *pca)
{
	struct pca_rfc_info *info = prop_chgalgo_get_drvdata(pca);

	PCA_INFO("++\n");
	return prop_ta_device_set_voltage_current(info->rfc, 5000, 3000);
}

static int pca_rfc_authenticate_ta(struct prop_chgalgo_device *pca,
				   struct prop_chgalgo_ta_auth_data *data)
{
	int ret;
	struct pca_rfc_info *info = prop_chgalgo_get_drvdata(pca);
	u32 vmin, vmax, imax;

	PCA_INFO("++\n");
	if (info->hrst_cnt >= RFC_RECV_HARDRESET_MAX)
		return -EINVAL;

	if (!info->rfc) {
		info->rfc = prop_ta_device_get_by_name("rfc_ta");
		if (!info->rfc) {
			PCA_ERR("get rfc ta fail\n");
			ret = -ENODEV;
			goto out;
		}
	}

	ret = prop_ta_device_authentication(info->rfc);
	if (ret < 0) {
		PCA_ERR("authentication fail(%d)\n", ret);
		goto out;
	}
	ret = prop_ta_device_get_min_voltage(info->rfc, &vmin);
	if (ret < 0) {
		PCA_ERR("get vmin fail(%d)\n", ret);
		goto out;
	}
	ret = prop_ta_device_get_max_voltage(info->rfc, &vmax);
	if (ret < 0) {
		PCA_ERR("get vmax fail(%d)\n", ret);
		goto out;
	}
	ret = prop_ta_device_get_max_current(info->rfc, &imax);
	if (ret < 0) {
		PCA_ERR("get imax fail(%d)\n", ret);
		goto out;
	}

	PCA_INFO("rfc boundary, %d mv ~ %d mv, %d ma\n", vmin, vmax, imax);
	if (vmin <= data->vcap_min && vmax >= data->vcap_max &&
	    imax >= data->icap_min) {
		data->vta_max = vmax;
		data->vta_min = vmin;
		data->ita_max = imax;
		data->ita_min = 0;
		data->pwr_lmt = 0; /* 0 stands for no power limit */
		data->support_cc = true;
		data->support_meas_cap = true;
		data->support_status = true;
		data->vta_step = 20;
		data->ita_step = 50;
	} else {
		PCA_ERR("rfc cap cannot support pca algo\n", __func__);
		ret = -EINVAL;
	}
out:
	return ret;
}

static int pca_rfc_enable_wdt(struct prop_chgalgo_device *pca, bool en)
{
	struct pca_rfc_info *info = prop_chgalgo_get_drvdata(pca);

	return prop_ta_device_enable_wdt(info->rfc, en);
}

static int pca_rfc_set_wdt(struct prop_chgalgo_device *pca, u32 ms)
{
	struct pca_rfc_info *info = prop_chgalgo_get_drvdata(pca);

	return prop_ta_device_set_wdt(info->rfc, ms);
}

static struct prop_chgalgo_ta_ops pca_rfc_ops = {
	.enable_charging = pca_rfc_enable_charging,
	.set_cap = pca_rfc_set_cap,
	.get_measure_cap = pca_rfc_get_measure_cap,
	.get_temperature = pca_rfc_get_temperature,
	.get_status = pca_rfc_get_status,
	.is_cc = pca_rfc_is_cc,
	.send_hardreset = pca_rfc_send_hardreset,
	.authenticate_ta = pca_rfc_authenticate_ta,
	.enable_wdt = pca_rfc_enable_wdt,
	.set_wdt = pca_rfc_set_wdt,
};
static SIMPLE_PCA_TA_DESC(pca_ta_rfc, pca_rfc_ops);

static int pca_rfc_tcp_notifier_call(struct notifier_block *nb,
				     unsigned long event, void *data)
{
	struct pca_rfc_info *info =
		container_of(nb, struct pca_rfc_info, tcp_nb);
	struct tcp_notify *noti = data;

	switch (event) {
	case TCP_NOTIFY_PD_STATE:
		switch (noti->pd_state.connected) {
		case PD_CONNECT_NONE:
			dev_info(info->dev, "detached\n");
			info->hrst_cnt = 0;
			break;
		case PD_CONNECT_HARD_RESET:
			info->hrst_cnt++;
			dev_info(info->dev, "pd hardreset, cnt = %d\n",
				 info->hrst_cnt);
			break;
		default:
			break;
		}
	default:
		break;
	}
	return NOTIFY_OK;
}

static int pca_rfc_probe(struct platform_device *pdev)
{
	int ret;
	struct pca_rfc_info *info;

	dev_info(&pdev->dev, "%s(%s)\n", __func__, PCA_RFC_TA_VERSION);

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);

	info->rfc = prop_ta_device_get_by_name("ta_rfc");
	if (!info->rfc) {
		dev_err(info->dev, "%s get rfc dev fail\n", __func__);
		return -ENODEV;
	}

	info->chg = get_charger_by_name("primary_chg");
	if (!info->chg) {
		dev_err(info->dev, "%s get chg dev fail\n", __func__);
		return -ENODEV;
	}

	info->tcpc = tcpc_dev_get_by_name("type_c_port0");
	if (!info->tcpc) {
		dev_err(info->dev, "%s get tcpc dev fail\n", __func__);
		return -ENODEV;
	}

	/* register tcp notifier callback */
	info->tcp_nb.notifier_call = pca_rfc_tcp_notifier_call;
	ret = register_tcp_dev_notifier(info->tcpc, &info->tcp_nb,
					TCP_NOTIFY_TYPE_USB);
	if (ret < 0) {
		dev_err(info->dev, "register tcpc notifier fail\n");
		return ret;
	}

	info->pca = prop_chgalgo_device_register(info->dev, &pca_ta_rfc_desc,
						 info);
	if (!info->pca) {
		dev_err(&pdev->dev, "register pca_rfc_ta fail\n");
		return -EINVAL;
	}

	dev_info(info->dev, "%s successfully\n", __func__);
	return 0;
}

static int pca_rfc_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_device pca_rfc_platdev = {
	.name = "pca_rfc",
	.id = PLATFORM_DEVID_NONE,
};

static struct platform_driver pca_rfc_platdrv = {
	.probe = pca_rfc_probe,
	.remove = pca_rfc_remove,
	.driver = {
		.name = "pca_rfc",
		.owner = THIS_MODULE,
	},
};

static int __init pca_rfc_init(void)
{
	platform_device_register(&pca_rfc_platdev);
	return platform_driver_register(&pca_rfc_platdrv);
}

static void __exit pca_rfc_exit(void)
{
	platform_driver_unregister(&pca_rfc_platdrv);
	platform_device_unregister(&pca_rfc_platdev);
}
module_init(pca_rfc_init);
module_exit(pca_rfc_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Richtek Fast Charge TA For PCA");
MODULE_AUTHOR("ShuFan Lee<shufan_lee@richtek.com>");
MODULE_VERSION(PCA_RFC_TA_VERSION);

/*
 * 2.0.0_G
 * (1) Adapt to prop_chgalgo_class V2.0.0
 *
 * 1.0.2_G
 * (1) Add is_cc ops
 * (2) Correct variable name of authentication data
 *
 * 1.0.1_G
 * (1) Modify authentication data, provide information of power limit,
 *     step of voltage and current
 *
 * 1.0.0_G
 * Initial release
 */
