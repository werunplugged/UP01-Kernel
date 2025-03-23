// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 * Author: Michael Hsiao <michael.hsiao@mediatek.com>
 */

/*******************************************************************************
 *
 * Filename:
 * ---------
 *   mt_soc_pcm_dl3_I2S3.c
 *
 * Project:
 * --------
 *    Audio Driver Kernel Function
 *
 * Description:
 * ------------
 *   Audio I2S3dl3 and Dl3 playback
 *
 * Author:
 * -------
 * Chipeng Chang
 *
 *------------------------------------------------------------------------------
 *
 *
 ******************************************************************************
 */

#include <linux/dma-mapping.h>
#include <sound/pcm_params.h>

#include "mtk-auddrv-afe.h"
#include "mtk-auddrv-ana.h"
#include "mtk-auddrv-clk.h"
#include "mtk-auddrv-common.h"
#include "mtk-auddrv-def.h"
#include "mtk-auddrv-kernel.h"
#include "mtk-soc-afe-control.h"
#include "mtk-soc-pcm-common.h"
#include "mtk-soc-pcm-platform.h"

static struct afe_mem_control_t *pI2S3dl3MemControl;
static struct snd_dma_buffer Dl3I2S3_Playback_dma_buf;
static unsigned int mPlaybackDramState;

static bool vcore_dvfs_enable;

/*
 *    function implementation
 */

static int mtk_I2S3dl3_probe(struct platform_device *pdev);
static int mtk_pcm_I2S3dl3_close(struct snd_pcm_substream *substream);
static int mtk_afe_I2S3dl3_component_probe(struct snd_soc_component *component);

static int mI2S3dl3_hdoutput_control;
static bool mPrepareDone;

static struct device *mDev;

const char *const I2S3dl3_HD_output[] = {"Off", "On"};
static const char *const I2S0_I2S3_4pin_switch[] = {"Off", "On"};

static const struct soc_enum Audio_I2S3dl3_Enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(I2S3dl3_HD_output), I2S3dl3_HD_output),
};

static const struct soc_enum Audio_I2S0_I2S3_4pin_Enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(I2S0_I2S3_4pin_switch), I2S0_I2S3_4pin_switch),
};

static int Audio_I2S3dl3_hdoutput_Get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("Audio_AmpR_Get = %d\n", mI2S3dl3_hdoutput_control);
	ucontrol->value.integer.value[0] = mI2S3dl3_hdoutput_control;
	return 0;
}

static int Audio_I2S3dl3_hdoutput_Set(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	/* pr_debug("%s()\n", __func__); */
	if (ucontrol->value.enumerated.item[0] >
	    ARRAY_SIZE(I2S3dl3_HD_output)) {
		pr_debug("%s(), return -EINVAL\n", __func__);
		return -EINVAL;
	}

	mI2S3dl3_hdoutput_control = ucontrol->value.integer.value[0];

	if (GetMemoryPathEnable(Soc_Aud_Digital_Block_MEM_HDMI) == true) {
		pr_debug("return HDMI enabled\n");
		return 0;
	}

	return 0;
}

static int Audio_I2S0_I2S3_4pin_Get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s, I2S0_I2S3_4pin_ctrl = %d\n", __func__,  I2S0_I2S3_4pin_ctrl);
	ucontrol->value.integer.value[0] = I2S0_I2S3_4pin_ctrl;
	return 0;
}

static int Audio_I2S0_I2S3_4pin_Set(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s, set %ld\n", __func__, ucontrol->value.integer.value[0]);
	if (ucontrol->value.enumerated.item[0] > ARRAY_SIZE(I2S0_I2S3_4pin_switch)) {
		pr_debug("return -EINVAL\n");
		return -EINVAL;
	}

	I2S0_I2S3_4pin_ctrl = ucontrol->value.integer.value[0];
	return 0;
}

static const struct snd_kcontrol_new Audio_snd_I2S3dl3_controls[] = {
	SOC_ENUM_EXT("Audio_I2S3dl3_hd_Switch", Audio_I2S3dl3_Enum[0],
		     Audio_I2S3dl3_hdoutput_Get, Audio_I2S3dl3_hdoutput_Set),
	SOC_ENUM_EXT("Audio_I2S0_I2S3_4pin", Audio_I2S0_I2S3_4pin_Enum[0],
			Audio_I2S0_I2S3_4pin_Get, Audio_I2S0_I2S3_4pin_Set),
};

static struct snd_pcm_hardware mtk_I2S3dl3_hardware = {
	.info = (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_NO_PERIOD_WAKEUP |
		 SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_RESUME |
		 SNDRV_PCM_INFO_MMAP_VALID),
	.formats = SND_SOC_ADV_MT_FMTS,
	.rates = SOC_HIGH_USE_RATE,
	.rate_min = SOC_HIGH_USE_RATE_MIN,
	.rate_max = SOC_HIGH_USE_RATE_MAX,
	.channels_min = SOC_NORMAL_USE_CHANNELS_MIN,
	.channels_max = SOC_NORMAL_USE_CHANNELS_MAX,
	.buffer_bytes_max = SOC_HIFI_BUFFER_SIZE,
	.period_bytes_max = SOC_HIFI_BUFFER_SIZE,
	.periods_min = SOC_NORMAL_USE_PERIODS_MIN,
	.periods_max = SOC_NORMAL_USE_PERIODS_MAX,
	.fifo_size = 0,
};

static int mtk_pcm_I2S3dl3_stop(struct snd_pcm_substream *substream)
{
	/* struct afe_block_t *Afe_Block = &(pI2S3dl3MemControl->rBlock); */

	pr_debug("%s\n", __func__);

	irq_remove_substream_user(
		substream, irq_request_number(Soc_Aud_Digital_Block_MEM_DL3));

	SetMemoryPathEnable(Soc_Aud_Digital_Block_MEM_DL3, false);

	ClearMemBlock(Soc_Aud_Digital_Block_MEM_DL3);

	return 0;
}

static snd_pcm_uframes_t
mtk_pcm_I2S3dl3_pointer(struct snd_pcm_substream *substream)
{
	return get_mem_frame_index(substream, pI2S3dl3MemControl,
				   Soc_Aud_Digital_Block_MEM_DL3);
}

static int mtk_pcm_I2S3dl3_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *hw_params)
{
	int ret = 0;

	substream->runtime->dma_bytes = params_buffer_bytes(hw_params);
	if (substream->runtime->dma_bytes <= GetPLaybackSramFullSize() &&
	    !pI2S3dl3MemControl->mAssignDRAM &&
	    AllocateAudioSram(&substream->runtime->dma_addr,
			      &substream->runtime->dma_area,
			      substream->runtime->dma_bytes, substream,
			      params_format(hw_params), false) == 0) {
		SetHighAddr(Soc_Aud_Digital_Block_MEM_DL3, false,
			    substream->runtime->dma_addr);
	} else {
		pr_debug("%s(), use DRAM\n", __func__);
		substream->runtime->dma_area = Dl3I2S3_Playback_dma_buf.area;
		substream->runtime->dma_addr = Dl3I2S3_Playback_dma_buf.addr;
		SetHighAddr(Soc_Aud_Digital_Block_MEM_DL3, true,
			    substream->runtime->dma_addr);
		mPlaybackDramState = true;
		AudDrv_Emi_Clk_On();
	}

	set_mem_block(substream, hw_params, pI2S3dl3MemControl,
		      Soc_Aud_Digital_Block_MEM_DL3);

	pr_debug("dma_bytes = %zu dma_area = %p dma_addr = 0x%lx\n",
		substream->runtime->dma_bytes, substream->runtime->dma_area,
		(long)substream->runtime->dma_addr);

	return ret;
}

static int mtk_pcm_I2S3dl3_hw_free(struct snd_pcm_substream *substream)
{
	/* pr_debug("%s substream = %p\n", __func__, substream); */
	if (mPlaybackDramState == true) {
		AudDrv_Emi_Clk_Off();
		mPlaybackDramState = false;
	} else
		freeAudioSram((void *)substream);

	return 0;
}

static struct snd_pcm_hw_constraint_list constraints_sample_rates = {
	.count = ARRAY_SIZE(soc_high_supported_sample_rates),
	.list = soc_high_supported_sample_rates,
	/* TODO: KC: need check this!!!!!!!!!! */
	.mask = 0,
};

static int mtk_pcm_I2S3dl3_open(struct snd_pcm_substream *substream)
{
	int ret = 0;
	struct snd_pcm_runtime *runtime = substream->runtime;

	mPlaybackDramState = false;

	pr_debug("%s: mtk_I2S3dl3_hardware.buffer_bytes_max = %zu mPlaybackDramState = %d\n",
		__func__, mtk_I2S3dl3_hardware.buffer_bytes_max,
		mPlaybackDramState);
	runtime->hw = mtk_I2S3dl3_hardware;

	AudDrv_Clk_On();

	memcpy((void *)(&(runtime->hw)), (void *)&mtk_I2S3dl3_hardware,
	       sizeof(struct snd_pcm_hardware));
	pI2S3dl3MemControl = Get_Mem_ControlT(Soc_Aud_Digital_Block_MEM_DL3);

	ret = snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
					 &constraints_sample_rates);


	if (ret < 0) {
		pr_debug("ret < 0 mtk_pcm_I2S3dl3_close\n");
		mtk_pcm_I2S3dl3_close(substream);
		return ret;
	}

	return 0;
}

static int mtk_pcm_I2S3dl3_close(struct snd_pcm_substream *substream)
{
	pr_debug("%s\n", __func__);

	if (is_irq_from_ext_module()) {
		ext_sync_signal_lock();
		ext_sync_signal_unlock();
	}

	if (mPrepareDone == true) {
		SetIntfConnection(Soc_Aud_InterCon_DisConnect,
				  Soc_Aud_AFE_IO_Block_MEM_DL3,
				  Soc_Aud_AFE_IO_Block_I2S3);
#ifdef MT6771_SND_SOC
		/* stop I2S output */
		if (I2S0_I2S3_4pin_ctrl) {
			Disable4pin_I2S0_I2S3();
			if (!mtk_soc_always_hd) {
				DisableAPLLTunerbySampleRate(
						substream->runtime->rate);
				DisableALLbySampleRate(
						substream->runtime->rate);
			}
		} else
#endif
		{
			SetMemoryPathEnable(Soc_Aud_Digital_Block_I2S_OUT_2, false);

			if (GetMemoryPathEnable(Soc_Aud_Digital_Block_I2S_OUT_2) ==
					false)
				Afe_Set_Reg(AFE_I2S_CON3, 0x0, 0x1);

			RemoveMemifSubStream(Soc_Aud_Digital_Block_MEM_DL3, substream);

			if (mI2S3dl3_hdoutput_control == true) {
				pr_debug("%s(), mI2S3dl3_hdoutput_control = %d\n",
					__func__, mI2S3dl3_hdoutput_control);
				/* here to close APLL */
				if (!mtk_soc_always_hd) {
					DisableAPLLTunerbySampleRate(
						substream->runtime->rate);
					DisableALLbySampleRate(
						substream->runtime->rate);
				}

				EnableI2SCLKDiv(Soc_Aud_I2S1_MCKDIV, false);
				EnableI2SCLKDiv(Soc_Aud_I2S3_MCKDIV, false);
			}
		}
		EnableAfe(false);
		mPrepareDone = false;
	}

	AudDrv_Clk_Off();

	vcore_dvfs(&vcore_dvfs_enable, true);

	return 0;
}

static int mtk_pcm_I2S3dl3_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned int u32AudioI2S = 0;
	bool mI2SWLen;

	pr_debug("%s: mPrepareDone = %d, format = %d, sample rate = %d\n",
		__func__, mPrepareDone, runtime->format,
		substream->runtime->rate);

	if (mPrepareDone == false) {
		SetMemifSubStream(Soc_Aud_Digital_Block_MEM_DL3, substream);

		if (runtime->format == SNDRV_PCM_FORMAT_S32_LE ||
		    runtime->format == SNDRV_PCM_FORMAT_U32_LE) {
			SetMemIfFetchFormatPerSample(
				Soc_Aud_Digital_Block_MEM_DL3,
				AFE_WLEN_32_BIT_ALIGN_8BIT_0_24BIT_DATA);
			SetConnectionFormat(OUTPUT_DATA_FORMAT_24BIT,
					    Soc_Aud_AFE_IO_Block_I2S3);
			mI2SWLen = Soc_Aud_I2S_WLEN_WLEN_32BITS;
		} else {
			SetMemIfFetchFormatPerSample(
				Soc_Aud_Digital_Block_MEM_DL3, AFE_WLEN_16_BIT);
			SetConnectionFormat(OUTPUT_DATA_FORMAT_16BIT,
					    Soc_Aud_AFE_IO_Block_I2S3);
			mI2SWLen = Soc_Aud_I2S_WLEN_WLEN_16BITS;
		}
		SetIntfConnection(Soc_Aud_InterCon_Connection,
				  Soc_Aud_AFE_IO_Block_MEM_DL3,
				  Soc_Aud_AFE_IO_Block_I2S3);


		/* TODO: KC: use Set2ndI2SOut() to set i2s3 */
		/* I2S out Setting */
#ifdef MT6771_SND_SOC
		if (I2S0_I2S3_4pin_ctrl) {
			if (!mtk_soc_always_hd) {
				EnableALLbySampleRate(runtime->rate);
				EnableAPLLTunerbySampleRate(runtime->rate);
			}
			Enable4pin_I2S0_I2S3(runtime->rate, 1, mI2SWLen);
		} else
#endif
		{
			u32AudioI2S =
				SampleRateTransform(runtime->rate,
					    Soc_Aud_Digital_Block_I2S_OUT_2)
						<< 8;
			u32AudioI2S |= Soc_Aud_I2S_FORMAT_I2S << 3; /* us3 I2s format */
			u32AudioI2S |= mI2SWLen << 1;

			if (mI2S3dl3_hdoutput_control == true) {
				pr_debug("%s mI2S3dl3_hdoutput_control == %d\n",
					__func__, mI2S3dl3_hdoutput_control);

				/* here to open APLL */
				if (!mtk_soc_always_hd) {
					EnableALLbySampleRate(runtime->rate);
					EnableAPLLTunerbySampleRate(runtime->rate);
				}

				SetCLkMclk(Soc_Aud_I2S1,
					runtime->rate); /* select I2S */
				SetCLkMclk(Soc_Aud_I2S3, runtime->rate);

				EnableI2SCLKDiv(Soc_Aud_I2S1_MCKDIV, true);
				EnableI2SCLKDiv(Soc_Aud_I2S3_MCKDIV, true);

				u32AudioI2S |= Soc_Aud_LOW_JITTER_CLOCK
				       << 12; /* Low jitter mode */
			} else {
				u32AudioI2S &= ~(Soc_Aud_LOW_JITTER_CLOCK << 12);
			}

			if (GetMemoryPathEnable(Soc_Aud_Digital_Block_I2S_OUT_2) ==
				false) {
				SetMemoryPathEnable(Soc_Aud_Digital_Block_I2S_OUT_2,
						true);

				Afe_Set_Reg(AFE_I2S_CON3, u32AudioI2S | 1,
						AFE_MASK_ALL);
			} else {
				SetMemoryPathEnable(Soc_Aud_Digital_Block_I2S_OUT_2,
					    true);
			}
		}

		EnableAfe(true);
		mPrepareDone = true;
	}
	return 0;
}

static int mtk_pcm_I2S3dl3_start(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	pr_debug("%s, period_size =%d\n", __func__, substream->runtime->period_size);

	/* here to set interrupt */
	irq_add_substream_user(
		substream, irq_request_number(Soc_Aud_Digital_Block_MEM_DL3),
		substream->runtime->rate,
		substream->runtime->period_size);

	SetSampleRate(Soc_Aud_Digital_Block_MEM_DL3, runtime->rate);
	SetChannels(Soc_Aud_Digital_Block_MEM_DL3, runtime->channels);
	SetMemoryPathEnable(Soc_Aud_Digital_Block_MEM_DL3, true);

	EnableAfe(true);

	return 0;
}

static int mtk_pcm_I2S3dl3_trigger(struct snd_pcm_substream *substream, int cmd)
{

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		return mtk_pcm_I2S3dl3_start(substream);
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		return mtk_pcm_I2S3dl3_stop(substream);
	}
	return -EINVAL;
}

static int mtk_pcm_I2S3dl3_copy(struct snd_pcm_substream *substream,
				int channel, unsigned long pos,
				void __user *dst, unsigned long count)
{
	vcore_dvfs(&vcore_dvfs_enable, false);
	return mtk_memblk_copy(substream, channel, pos, dst, count,
			       pI2S3dl3MemControl,
			       Soc_Aud_Digital_Block_MEM_DL3);
}

static int mtk_pcm_I2S3dl3_silence(struct snd_pcm_substream *substream,
				   int channel,
				   unsigned long pos,
				   unsigned long bytes)
{
	return 0; /* do nothing */
}

static void *dummy_page[2];

static struct page *mtk_I2S3dl3_pcm_page(struct snd_pcm_substream *substream,
					 unsigned long offset)
{
	return virt_to_page(dummy_page[substream->stream]); /* the same page */
}

static struct snd_pcm_ops mtk_I2S3dl3_ops = {
	.open = mtk_pcm_I2S3dl3_open,
	.close = mtk_pcm_I2S3dl3_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = mtk_pcm_I2S3dl3_hw_params,
	.hw_free = mtk_pcm_I2S3dl3_hw_free,
	.prepare = mtk_pcm_I2S3dl3_prepare,
	.trigger = mtk_pcm_I2S3dl3_trigger,
	.pointer = mtk_pcm_I2S3dl3_pointer,
	.copy_user = mtk_pcm_I2S3dl3_copy,
	.fill_silence = mtk_pcm_I2S3dl3_silence,
	.page = mtk_I2S3dl3_pcm_page,
	.mmap = mtk_pcm_mmap,
};

static const struct snd_soc_component_driver mtk_I2S3dl3_soc_component = {
	.name = AFE_PCM_NAME,
	.ops = &mtk_I2S3dl3_ops,
	.probe = mtk_afe_I2S3dl3_component_probe,
};

static int mtk_I2S3dl3_probe(struct platform_device *pdev)
{
	pr_debug("%s\n", __func__);

	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	if (!pdev->dev.dma_mask)
		pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;

	if (pdev->dev.of_node)
		dev_set_name(&pdev->dev, "%s", MT_SOC_I2S3_DL3_PCM);
	pdev->name = pdev->dev.kobj.name;

	pr_debug("%s: dev name %s\n", __func__, dev_name(&pdev->dev));

	mDev = &pdev->dev;

	return snd_soc_register_component(&pdev->dev,
					  &mtk_I2S3dl3_soc_component,
					  NULL,
					  0);
}

static int mtk_afe_I2S3dl3_component_probe(struct snd_soc_component *component)
{
	pr_debug("%s\n", __func__);
	snd_soc_add_component_controls(component, Audio_snd_I2S3dl3_controls,
				      ARRAY_SIZE(Audio_snd_I2S3dl3_controls));
	/* allocate dram */
	Dl3I2S3_Playback_dma_buf.area = dma_alloc_coherent(
		component->dev, SOC_HIFI_BUFFER_SIZE,
		&Dl3I2S3_Playback_dma_buf.addr, GFP_KERNEL | GFP_DMA);
	if (!Dl3I2S3_Playback_dma_buf.area)
		return -ENOMEM;

	Dl3I2S3_Playback_dma_buf.bytes = SOC_HIFI_BUFFER_SIZE;
	pr_debug("area = %p\n", Dl3I2S3_Playback_dma_buf.area);

	return 0;
}

static int mtk_I2S3dl3_remove(struct platform_device *pdev)
{
	snd_soc_unregister_component(&pdev->dev);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id mt_soc_pcm_dl1_i2s3Dl3_of_ids[] = {
	{
		.compatible = "mediatek,mt_soc_pcm_i2s3_dl3",
	},
	{} };
#endif

static struct platform_driver mtk_I2S3dl3_driver = {
	.driver = {

			.name = MT_SOC_I2S3_DL3_PCM,
			.owner = THIS_MODULE,
#ifdef CONFIG_OF
			.of_match_table = mt_soc_pcm_dl1_i2s3Dl3_of_ids,
#endif
		},
	.probe = mtk_I2S3dl3_probe,
	.remove = mtk_I2S3dl3_remove,
};

#ifndef CONFIG_OF
static struct platform_device *soc_mtkI2S3dl3_dev;
#endif

static int __init mtk_I2S3dl3_soc_platform_init(void)
{
	int ret;

	pr_debug("%s\n", __func__);
#ifndef CONFIG_OF
	soc_mtkI2S3dl3_dev = platform_device_alloc(MT_SOC_I2S3_DL3_PCM, -1);
	if (!soc_mtkI2S3dl3_dev)
		return -ENOMEM;

	ret = platform_device_add(soc_mtkI2S3dl3_dev);
	if (ret != 0) {
		platform_device_put(soc_mtkI2S3dl3_dev);
		return ret;
	}
#endif

	ret = platform_driver_register(&mtk_I2S3dl3_driver);

	return ret;
}
module_init(mtk_I2S3dl3_soc_platform_init);

static void __exit mtk_I2S3dl3_soc_platform_exit(void)
{
	platform_driver_unregister(&mtk_I2S3dl3_driver);
}
module_exit(mtk_I2S3dl3_soc_platform_exit);

MODULE_DESCRIPTION("AFE PCM module platform driver");
MODULE_LICENSE("GPL");
