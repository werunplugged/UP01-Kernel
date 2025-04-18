/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
*/

#include <linux/clk.h>

#include "seninf_clk.h"

static struct SENINF_CLK_CTRL gseninf_mclk_name[SENINF_CLK_IDX_MAX_NUM] = {
	{"SCP_SYS_DIS"},
	{"SCP_SYS_CAM"},
	{"CAMSYS_SENINF_CGPDN"},
	{"TOP_MUX_SENINF"},
	{"TOP_MUX_SENINF1"},
	{"TOP_MUX_SENINF2"},
	{"TOP_MUX_CAMTG"},
	{"TOP_MUX_CAMTG2"},
	{"TOP_MUX_CAMTG3"},
	{"TOP_MUX_CAMTG4"},
	{"TOP_MUX_CAMTG5"},
	{"TOP_UNIVP_192M_D32"}, /*   6*/
	{"TOP_UNIVP_192M_D16"}, /*  12*/
	{"TOP_F26M_CK_D2"},     /*  13*/
	{"TOP_UNIVP_192M_D8"},  /*  24*/
	{"TOP_CLK26M"},         /*  26*/
	{"TOP_UNIVP_192M_D4"},  /*  48*/
	{"TOP_UNIVPLL_D3_D8"},  /*  52*/
};

static enum SENINF_CLK_MCLK_FREQ
gseninf_clk_freq[SENINF_CLK_IDX_FREQ_IDX_NUM] = {
	SENINF_CLK_MCLK_FREQ_6MHZ,
	SENINF_CLK_MCLK_FREQ_12MHZ,
	SENINF_CLK_MCLK_FREQ_13MHZ,
	SENINF_CLK_MCLK_FREQ_24MHZ,
	SENINF_CLK_MCLK_FREQ_26MHZ,
	SENINF_CLK_MCLK_FREQ_48MHZ,
	SENINF_CLK_MCLK_FREQ_52MHZ,
};

#ifdef IMGSENSOR_DFS_CTRL_ENABLE
struct mtk_pm_qos_request imgsensor_qos;

int imgsensor_dfs_ctrl(enum DFS_OPTION option, void *pbuff)
{
	int i4RetValue = 0;

	/*pr_info("%s\n", __func__);*/

	switch (option) {
	case DFS_CTRL_ENABLE:
		mtk_pm_qos_add_request(&imgsensor_qos, PM_QOS_CAM_FREQ, 0);
		pr_debug("seninf PMQoS turn on\n");
		break;
	case DFS_CTRL_DISABLE:
		mtk_pm_qos_remove_request(&imgsensor_qos);
		pr_debug("seninf PMQoS turn off\n");
		break;
	case DFS_UPDATE:
		pr_debug(
			"seninf Set isp clock level:%d\n",
			*(unsigned int *)pbuff);
		mtk_pm_qos_update_request(&imgsensor_qos,
			*(unsigned int *)pbuff);
		break;
	case DFS_RELEASE:
		pr_debug(
			"seninf release and set isp clk request to 0\n");
		mtk_pm_qos_update_request(&imgsensor_qos, 0);
		break;
	case DFS_SUPPORTED_ISP_CLOCKS:
	{
		int result = 0;
		uint64_t freq_steps[ISP_CLK_LEVEL_CNT] = {0};
		struct IMAGESENSOR_GET_SUPPORTED_ISP_CLK *pIspclks;
		unsigned int lv = 0;

		pIspclks = (struct IMAGESENSOR_GET_SUPPORTED_ISP_CLK *) pbuff;

		/* Call mmdvfs_qos_get_freq_steps
		 * to get supported frequency
		 */
		result = mmdvfs_qos_get_freq_steps(
			PM_QOS_CAM_FREQ,
			freq_steps, (u32 *)&pIspclks->clklevelcnt);

		if (result < 0) {
			pr_err(
				"ERR: get MMDVFS freq steps failed, result: %d\n",
				result);
			i4RetValue = -EFAULT;
			break;
		}

		if (pIspclks->clklevelcnt > ISP_CLK_LEVEL_CNT) {
			pr_err("ERR: clklevelcnt is exceeded");
			i4RetValue = -EFAULT;
			break;
		}

		for (lv = 0; lv < pIspclks->clklevelcnt; lv++) {
			/* Save clk from low to high */
			pIspclks->clklevel[lv] = freq_steps[lv];
			/*pr_debug("DFS Clk level[%d]:%d",
			 *	lv, pIspclks->clklevel[lv]);
			 */
		}
	}
		break;
	case DFS_CUR_ISP_CLOCK:
	{
		unsigned int *pGetIspclk;

		pGetIspclk = (unsigned int *) pbuff;
		*pGetIspclk = (u32)mmdvfs_qos_get_freq(PM_QOS_CAM_FREQ);
		/*pr_debug("current isp clock:%d", *pGetIspclk);*/
	}
		break;
	default:
		pr_info("None\n");
		break;
	}
	return i4RetValue;
}
#endif

static inline void seninf_clk_check(struct SENINF_CLK *pclk)
{
	int i;

	for (i = 0; i < SENINF_CLK_IDX_MAX_NUM; i++)
		WARN_ON(IS_ERR(pclk->mclk_sel[i]));
}

/**********************************************************
 *Common Clock Framework (CCF)
 **********************************************************/
enum SENINF_RETURN seninf_clk_init(struct SENINF_CLK *pclk)
{
	int i;

	if (pclk->pplatform_device == NULL) {
		pr_err("[%s] pdev is null\n", __func__);
		return SENINF_RETURN_ERROR;
	}
	/* get all possible using clocks */
	for (i = 0; i < SENINF_CLK_IDX_MAX_NUM; i++) {
		pclk->mclk_sel[i] = devm_clk_get(&pclk->pplatform_device->dev,
						gseninf_mclk_name[i].pctrl);
		atomic_set(&pclk->enable_cnt[i], 0);

		if (IS_ERR(pclk->mclk_sel[i])) {
			pr_err("cannot get %d clock\n", i);
			return SENINF_RETURN_ERROR;
		}
	}

#ifdef CONFIG_PM_SLEEP
	pclk->seninf_wake_lock = wakeup_source_register(
			NULL, "seninf_lock_wakelock");
	if (!pclk->seninf_wake_lock) {
		pr_info("failed to get seninf_wake_lock\n");
		return SENINF_RETURN_ERROR;
	}
#endif
	atomic_set(&pclk->wakelock_cnt, 0);

	return SENINF_RETURN_SUCCESS;
}

void seninf_clk_exit(struct SENINF_CLK *pclk)
{
#ifdef CONFIG_PM_SLEEP
	wakeup_source_unregister(pclk->seninf_wake_lock);
#endif
}

int seninf_clk_set(struct SENINF_CLK *pclk,
					struct ACDK_SENSOR_MCLK_STRUCT *pmclk)
{
	int i, ret = 0;
	unsigned int idx_tg, idx_freq;

	if (pmclk->TG >= SENINF_CLK_TG_MAX_NUM ||
	    pmclk->freq > SENINF_CLK_MCLK_FREQ_MAX ||
		pmclk->freq < SENINF_CLK_MCLK_FREQ_MIN) {
		pr_err(
	"[CAMERA SENSOR]kdSetSensorMclk out of range, tg = %d, freq = %d\n",
		  pmclk->TG, pmclk->freq);
		return -EFAULT;
	}

	PK_DBG("[CAMERA SENSOR] CCF kdSetSensorMclk on= %d, freq= %d, TG= %d\n",
	       pmclk->on, pmclk->freq, pmclk->TG);

	seninf_clk_check(pclk);

	for (i = 0; ((i < SENINF_CLK_IDX_FREQ_IDX_NUM) &&
			(pmclk->freq != gseninf_clk_freq[i])); i++)
		;

	if (i >= SENINF_CLK_IDX_FREQ_IDX_NUM)
		return -EFAULT;


	idx_tg = pmclk->TG + SENINF_CLK_IDX_TG_MIN_NUM;
	idx_freq = i + SENINF_CLK_IDX_FREQ_MIN_NUM;

	if (pmclk->on) {
		/* Workaround for timestamp: TG1 always ON */
		if (clk_prepare_enable(
			pclk->mclk_sel[SENINF_CLK_IDX_TG_TOP_MUX_CAMTG]))
			pr_err("[CAMERA SENSOR] failed tg=%d\n",
				  SENINF_CLK_IDX_TG_TOP_MUX_CAMTG);
		else
			atomic_inc(
			&pclk->enable_cnt[SENINF_CLK_IDX_TG_TOP_MUX_CAMTG]);

		if (clk_prepare_enable(pclk->mclk_sel[idx_tg]))
			pr_err("[CAMERA SENSOR] failed tg=%d\n", pmclk->TG);
		else
			atomic_inc(&pclk->enable_cnt[idx_tg]);

		if (clk_prepare_enable(pclk->mclk_sel[idx_freq]))
			pr_err("[CAMERA SENSOR] failed freq idx= %d\n", i);
		else
			atomic_inc(&pclk->enable_cnt[idx_freq]);

		ret = clk_set_parent(
			pclk->mclk_sel[idx_tg], pclk->mclk_sel[idx_freq]);
	} else {
		if (atomic_read(&pclk->enable_cnt[idx_freq]) > 0) {
			clk_disable_unprepare(pclk->mclk_sel[idx_freq]);
			atomic_dec(&pclk->enable_cnt[idx_freq]);
		}

		if (atomic_read(&pclk->enable_cnt[idx_tg]) > 0) {
			clk_disable_unprepare(pclk->mclk_sel[idx_tg]);
			atomic_dec(&pclk->enable_cnt[idx_tg]);
		}

		/* Workaround for timestamp: TG1 always ON */
		if (atomic_read(
			&pclk->enable_cnt[SENINF_CLK_IDX_TG_TOP_MUX_CAMTG])
			> 0) {
			clk_disable_unprepare(
			pclk->mclk_sel[SENINF_CLK_IDX_TG_TOP_MUX_CAMTG]);
			atomic_dec(
			&pclk->enable_cnt[SENINF_CLK_IDX_TG_TOP_MUX_CAMTG]);
		}
	}

	return ret;
}

void seninf_clk_open(struct SENINF_CLK *pclk)
{
	MINT32 i;

	PK_DBG("open\n");

	if (atomic_inc_return(&pclk->wakelock_cnt) == 1) {
#ifdef CONFIG_PM_SLEEP
		__pm_stay_awake(pclk->seninf_wake_lock);
#endif
	}

	for (i = SENINF_CLK_IDX_SYS_MIN_NUM;
		i < SENINF_CLK_IDX_SYS_MAX_NUM;
		i++) {
		if (clk_prepare_enable(pclk->mclk_sel[i]))
			pr_err("[CAMERA SENSOR] failed sys idx= %d\n", i);
		else
			atomic_inc(&pclk->enable_cnt[i]);
	}
}

void seninf_clk_release(struct SENINF_CLK *pclk)
{
	MINT32 i = SENINF_CLK_IDX_MAX_NUM;

	PK_DBG("release\n");

	do {
		i--;
		for (; atomic_read(&pclk->enable_cnt[i]) > 0;) {
			clk_disable_unprepare(pclk->mclk_sel[i]);
			atomic_dec(&pclk->enable_cnt[i]);
		}
	} while (i);

	if (atomic_dec_and_test(&pclk->wakelock_cnt)) {
#ifdef CONFIG_PM_SLEEP
		__pm_relax(pclk->seninf_wake_lock);
#endif
	}
}

unsigned int seninf_clk_get_meter(struct SENINF_CLK *pclk, unsigned int clk)
{
#if SENINF_CLK_CONTROL
	/* workaround */
	mt_get_ckgen_freq(1);

	if (clk == 4) {
		PK_DBG("CAMSYS_SENINF_CGPDN = %lu\n",
		clk_get_rate(
		pclk->mclk_sel[SENINF_CLK_IDX_SYS_CAMSYS_SENINF_CGPDN]));
		PK_DBG("TOP_MUX_SENINF = %lu\n",
			clk_get_rate(
			pclk->mclk_sel[SENINF_CLK_IDX_SYS_TOP_MUX_SENINF]));
	}
	return mt_get_ckgen_freq(clk);
#else
	return 0;
#endif
}

