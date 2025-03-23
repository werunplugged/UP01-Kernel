// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 * Author: Jia.Zeng <Jia.Zeng@mediatek.com>
 */

/*******************************************************************************
 *
 * Filename:
 * ---------
 *   mtk-soc-i2s0-vul2-capture.c
 *
 * Project:
 * --------
 *   Audio Driver Kernel Function
 *
 * Description:
 * ------------
 *   Audio i2s0 to Ul2  capture
 *
 * Author:
 * -------
 *
 *
 *------------------------------------------------------------------------------
 *
 *
 *******************************************************************************/

#include "mtk-auddrv-common.h"
#include "mtk-soc-pcm-common.h"
#include "mtk-auddrv-def.h"
#include "mtk-auddrv-afe.h"
#include "mtk-auddrv-ana.h"
#include "mtk-auddrv-clk.h"
#include "mtk-auddrv-kernel.h"
#include "mtk-soc-afe-control.h"
#include "mtk-soc-pcm-platform.h"
#include "mtk-soc-analog-type.h"
#include "mtk-soc-digital-type.h"
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/jack.h>

/* information about */
struct afe_mem_control_t  *I2S0_VUL2_Control_context;
static struct snd_dma_buffer *I2s0_vul2_dma_buf;
static struct audio_digital_i2s *mAudioDigitalI2S;
static bool mCaptureUseSram;
static int i2s0_vul2_hdoutput;
const char * const i2s0_vul2_HD_output[] = {"Off", "On"};

static void StartAudioI2s0InVUL2Hardware(struct snd_pcm_substream *substream);
static void StopAudioI2s0InVUL2Hardware(struct snd_pcm_substream *substream);
static int mtk_i2s0_vul2_probe(struct platform_device *pdev);
static int mtk_i2s0_vul2_pcm_close(struct snd_pcm_substream *substream);
static int mtk_afe_i2s0_vul2_probe(struct snd_soc_component *component);

static const struct soc_enum Audio_i2s0_vul2_Enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(i2s0_vul2_HD_output), i2s0_vul2_HD_output),
};

static int Audio_i2s0_vul2_hdoutput_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s() = %d\n", __func__, i2s0_vul2_hdoutput);
	ucontrol->value.integer.value[0] = i2s0_vul2_hdoutput;
	return 0;
}

static int Audio_i2s0_vul2_hdoutput_set(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s()\n", __func__);
	if (ucontrol->value.enumerated.item[0] >
	    ARRAY_SIZE(i2s0_vul2_HD_output)) {
		pr_info("return -EINVAL\n");
		return -EINVAL;
	}
	i2s0_vul2_hdoutput = ucontrol->value.integer.value[0];
	return 0;
}

static const struct snd_kcontrol_new Audio_i2s0_vul2_controls[] = {
	SOC_ENUM_EXT("Audio_i2s0_vul2_hd_Switch", Audio_i2s0_vul2_Enum[0],
		     Audio_i2s0_vul2_hdoutput_get, Audio_i2s0_vul2_hdoutput_set),
};

static struct snd_pcm_hardware mtk_i2s0_vul2_hardware = {
	.info = (SNDRV_PCM_INFO_MMAP |
	SNDRV_PCM_INFO_INTERLEAVED |
	SNDRV_PCM_INFO_RESUME |
	SNDRV_PCM_INFO_MMAP_VALID),
	.formats =      SND_SOC_ADV_MT_FMTS,
	.rates =        SOC_NORMAL_USE_RATE,
	.rate_min =     SOC_NORMAL_USE_RATE_MIN,
	.rate_max =     SOC_NORMAL_USE_RATE_MAX,
	.channels_min =     SOC_NORMAL_USE_CHANNELS_MIN,
	.channels_max =     SOC_NORMAL_USE_CHANNELS_MAX,
	.buffer_bytes_max = UL1_MAX_BUFFER_SIZE,
	.period_bytes_max = UL1_MAX_BUFFER_SIZE,
	.periods_min =      UL1_MIN_PERIOD_SIZE,
	.periods_max =      UL1_MAX_PERIOD_SIZE,
	.fifo_size =        0,
};

static void StopAudioI2s0InVUL2Hardware(struct snd_pcm_substream *substream)
{
	pr_debug("%s\n", __func__);

#ifdef MT6771_SND_SOC
	if (I2S0_I2S3_4pin_ctrl) {
		Disable4pin_I2S0_I2S3();
	} else
#endif
	{
		SetMemoryPathEnable(Soc_Aud_Digital_Block_I2S_IN_2, false);
		if (GetMemoryPathEnable(Soc_Aud_Digital_Block_I2S_IN_2) == false)
			Set2ndI2SInEnable(false);
	}

	SetMemoryPathEnable(Soc_Aud_Digital_Block_MEM_VUL2, false);
	/* here to set interrupt */
	irq_remove_user(substream, irq_request_number(Soc_Aud_Digital_Block_MEM_VUL2));
	/* here to turn off digital part */
	SetIntfConnection(Soc_Aud_InterCon_DisConnect,
			Soc_Aud_AFE_IO_Block_I2S0, Soc_Aud_AFE_IO_Block_MEM_VUL2);

	if (i2s0_vul2_hdoutput == true) {
		pr_debug("%s i2s0_vul2_hdoutput == %d\n", __func__, i2s0_vul2_hdoutput);
		/* here to close APLL */
		if (!mtk_soc_always_hd) {
			DisableAPLLTunerbySampleRate(substream->runtime->rate);
			DisableALLbySampleRate(substream->runtime->rate);
		}
	}

	EnableAfe(false);
}

static void StartAudioI2s0InVUL2Hardware(struct snd_pcm_substream *substream)
{
	struct audio_digital_i2s m2ndI2SInAttribute;

	pr_debug("%s\n", __func__);
	memset_io((void *)&m2ndI2SInAttribute, 0, sizeof(m2ndI2SInAttribute));
	if (substream->runtime->format == SNDRV_PCM_FORMAT_S32_LE ||
		substream->runtime->format == SNDRV_PCM_FORMAT_U32_LE) {
		SetMemIfFetchFormatPerSample(Soc_Aud_Digital_Block_MEM_VUL2,
					AFE_WLEN_32_BIT_ALIGN_8BIT_0_24BIT_DATA);
		SetConnectionFormat(OUTPUT_DATA_FORMAT_24BIT, Soc_Aud_AFE_IO_Block_MEM_VUL2);
		m2ndI2SInAttribute.mI2S_WLEN = Soc_Aud_I2S_WLEN_WLEN_32BITS;
	} else {
		SetMemIfFetchFormatPerSample(Soc_Aud_Digital_Block_MEM_VUL2, AFE_WLEN_16_BIT);
		SetConnectionFormat(OUTPUT_DATA_FORMAT_16BIT, Soc_Aud_AFE_IO_Block_MEM_VUL2);
		m2ndI2SInAttribute.mI2S_WLEN = Soc_Aud_I2S_WLEN_WLEN_16BITS;
	}

	if (i2s0_vul2_hdoutput == true) {
		pr_debug("%s deep_buffer_dl_hdoutput == %d\n", __func__, i2s0_vul2_hdoutput);
		/* here to open APLL */
		if (!mtk_soc_always_hd) {
			EnableALLbySampleRate(substream->runtime->rate);
			EnableAPLLTunerbySampleRate(substream->runtime->rate);
		}
	}
#ifdef MT6771_SND_SOC
	if (I2S0_I2S3_4pin_ctrl)
		Enable4pin_I2S0_I2S3(substream->runtime->rate, i2s0_vul2_hdoutput,
								m2ndI2SInAttribute.mI2S_WLEN);
	else
#endif
	{
		m2ndI2SInAttribute.mLR_SWAP = Soc_Aud_LR_SWAP_NO_SWAP;
		m2ndI2SInAttribute.mI2S_IN_PAD_SEL = true; /* I2S_IN_FROM_IO_MUX */
		m2ndI2SInAttribute.mI2S_SLAVE = Soc_Aud_I2S_SRC_MASTER_MODE;
		m2ndI2SInAttribute.mI2S_SAMPLERATE = substream->runtime->rate;
		m2ndI2SInAttribute.mINV_LRCK = Soc_Aud_INV_LRCK_NO_INVERSE;
		m2ndI2SInAttribute.mI2S_FMT = Soc_Aud_I2S_FORMAT_I2S;
		m2ndI2SInAttribute.mI2S_HDEN = (i2s0_vul2_hdoutput ? Soc_Aud_LOW_JITTER_CLOCK
					: Soc_Aud_NORMAL_CLOCK);
		Set2ndI2SIn(&m2ndI2SInAttribute);

		if (GetMemoryPathEnable(Soc_Aud_Digital_Block_I2S_IN_2) == false) {
			SetMemoryPathEnable(Soc_Aud_Digital_Block_I2S_IN_2, true);
			Set2ndI2SInEnable(true);
		} else
			SetMemoryPathEnable(Soc_Aud_Digital_Block_I2S_IN_2, true);
	}

	/* here to set interrupt */
	irq_add_user(substream,
			irq_request_number(Soc_Aud_Digital_Block_MEM_VUL2),
			substream->runtime->rate,
			substream->runtime->period_size);

	SetSampleRate(Soc_Aud_Digital_Block_MEM_VUL2, substream->runtime->rate);
	SetChannels(Soc_Aud_Digital_Block_MEM_VUL2, substream->runtime->channels);
	SetMemoryPathEnable(Soc_Aud_Digital_Block_MEM_VUL2, true);
	SetIntfConnection(Soc_Aud_InterCon_Connection,
			Soc_Aud_AFE_IO_Block_I2S0, Soc_Aud_AFE_IO_Block_MEM_VUL2);

	EnableAfe(true);
}

static int mtk_i2s0_vul2_pcm_prepare(struct snd_pcm_substream *substream)
{
	pr_debug("%s substream->rate = %d  substream->channels = %d\n", __func__,
				substream->runtime->rate, substream->runtime->channels);
	return 0;
}

static int mtk_i2s0_vul2_alsa_stop(struct snd_pcm_substream *substream)
{
	struct afe_block_t *Vul_Block = &(I2S0_VUL2_Control_context->rBlock);

	pr_debug("%s\n", __func__);
	StopAudioI2s0InVUL2Hardware(substream);
	Vul_Block->u4DMAReadIdx  = 0;
	Vul_Block->u4WriteIdx  = 0;
	Vul_Block->u4DataRemained = 0;
	RemoveMemifSubStream(Soc_Aud_Digital_Block_MEM_VUL2, substream);

	return 0;
}

static snd_pcm_uframes_t mtk_i2s0_vul2_pcm_pointer(struct snd_pcm_substream
						 *substream)
{
	return get_mem_frame_index(substream,
		I2S0_VUL2_Control_context, Soc_Aud_Digital_Block_MEM_VUL2);
}

static int mtk_i2s0_vul2_pcm_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *hw_params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_dma_buffer *dma_buf = &substream->dma_buffer;
	int ret = 0;

	dma_buf->dev.type = SNDRV_DMA_TYPE_DEV;
	dma_buf->dev.dev = substream->pcm->card->dev;
	dma_buf->private_data = NULL;
	runtime->dma_bytes = params_buffer_bytes(hw_params);

	if (AllocateAudioSram(&substream->runtime->dma_addr,
			      &substream->runtime->dma_area,
			      substream->runtime->dma_bytes, substream,
			      params_format(hw_params), false) == 0) {
		pr_debug("AllocateAudioSram success\n");
		SetHighAddr(Soc_Aud_Digital_Block_MEM_VUL2, false, substream->runtime->dma_addr);
	} else if (I2s0_vul2_dma_buf->area) {
		runtime->dma_area = I2s0_vul2_dma_buf->area;
		runtime->dma_addr = I2s0_vul2_dma_buf->addr;
		SetHighAddr(Soc_Aud_Digital_Block_MEM_VUL2, true, runtime->dma_addr);
		mCaptureUseSram = true;
		AudDrv_Emi_Clk_On();
	} else {
		ret =  snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
	}

	set_mem_block(substream, hw_params, I2S0_VUL2_Control_context,
					Soc_Aud_Digital_Block_MEM_VUL2);
	pr_debug("%s dma_bytes = %zu dma_area = %p dma_addr = 0x%lx\n", __func__,
				substream->runtime->dma_bytes,
				substream->runtime->dma_area,
				(long)substream->runtime->dma_addr);

	return ret;
}

static int mtk_i2s0_vul2_pcm_hw_free(struct snd_pcm_substream *substream)
{
	pr_debug("%s\n", __func__);

	if (I2s0_vul2_dma_buf->area) {
		if (mCaptureUseSram == true) {
			AudDrv_Emi_Clk_Off();
			mCaptureUseSram = false;
		} else
			freeAudioSram((void *)substream);
		return 0;
	} else
		return snd_pcm_lib_free_pages(substream);
}

/* Conventional and unconventional sample rate supported */
static unsigned int Vul1_supported_sample_rates[] = {
	8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000, 88200, 96000, 192000
};

static struct snd_pcm_hw_constraint_list constraints_sample_rates = {
	.count = ARRAY_SIZE(Vul1_supported_sample_rates),
	.list = Vul1_supported_sample_rates,
};

static int mtk_i2s0_vul2_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret = 0;

	AudDrv_Clk_On();
	pr_debug("%s\n", __func__);

	I2S0_VUL2_Control_context = Get_Mem_ControlT(Soc_Aud_Digital_Block_MEM_VUL2);
	runtime->hw = mtk_i2s0_vul2_hardware;
	memcpy((void *)(&(runtime->hw)), (void *)&mtk_i2s0_vul2_hardware,
			sizeof(struct snd_pcm_hardware));

	pr_debug("runtime->hw->rates = 0x%x\n ", runtime->hw.rates);
	ret = snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
					&constraints_sample_rates);

	ret = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		pr_info("snd_pcm_hw_constraint_integer failed\n");

	pr_debug("%s runtime rate = %d channels = %d\n", __func__, runtime->rate,
				runtime->channels);
	runtime->hw.info |= SNDRV_PCM_INFO_INTERLEAVED;
	runtime->hw.info |= SNDRV_PCM_INFO_NONINTERLEAVED;
	runtime->hw.info |= SNDRV_PCM_INFO_MMAP_VALID;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		pr_debug("SNDRV_PCM_STREAM_CAPTURE mtkalsa_i2s0_vul2_constraints\n");

	if (ret < 0) {
		mtk_i2s0_vul2_pcm_close(substream);
		return ret;
	}

	return 0;
}
static int mtk_i2s0_vul2_pcm_close(struct snd_pcm_substream *substream)
{
	AudDrv_Clk_Off();
	return 0;
}

static int mtk_i2s0_vul2_alsa_start(struct snd_pcm_substream *substream)
{
	pr_debug("%s\n", __func__);

	SetMemifSubStream(Soc_Aud_Digital_Block_MEM_VUL2, substream);
	StartAudioI2s0InVUL2Hardware(substream);
	return 0;
}

static int mtk_i2s0_vul2_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	pr_debug("%s, cmd = %d\n", __func__, cmd);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		return mtk_i2s0_vul2_alsa_start(substream);
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		return mtk_i2s0_vul2_alsa_stop(substream);
	}

	return -EINVAL;
}

static int mtk_i2s0_vul2_pcm_copy(struct snd_pcm_substream *substream,
				int channel, snd_pcm_uframes_t pos,
				void __user *dst, snd_pcm_uframes_t count)
{
	return mtk_memblk_copy(substream, channel, pos, dst, count,
		I2S0_VUL2_Control_context, Soc_Aud_Digital_Block_MEM_VUL2);
}

static int mtk_i2s0_vul2_pcm_silence(struct snd_pcm_substream *substream,
				   int channel, snd_pcm_uframes_t pos,
				   snd_pcm_uframes_t count)
{
	pr_debug("%s\n", __func__);
	return 0; /* do nothing */
}

static void *dummy_page[2];

static struct page *mtk_i2s0_vul2_pcm_page(struct snd_pcm_substream *substream,
					 unsigned long offset)
{
	pr_debug("%s\n", __func__);
	return virt_to_page(dummy_page[substream->stream]); /* the same page */
}

static struct snd_pcm_ops mtk_afe_i2s0_vul2_ops = {
	.open =     mtk_i2s0_vul2_pcm_open,
	.close =    mtk_i2s0_vul2_pcm_close,
	.ioctl =    snd_pcm_lib_ioctl,
	.hw_params =    mtk_i2s0_vul2_pcm_hw_params,
	.hw_free =  mtk_i2s0_vul2_pcm_hw_free,
	.prepare =  mtk_i2s0_vul2_pcm_prepare,
	.trigger =  mtk_i2s0_vul2_pcm_trigger,
	.pointer =  mtk_i2s0_vul2_pcm_pointer,
	.copy_user =     mtk_i2s0_vul2_pcm_copy,
	.fill_silence =  mtk_i2s0_vul2_pcm_silence,
	.page =     mtk_i2s0_vul2_pcm_page,
};

static const struct snd_soc_component_driver mtk_soc_component = {
	.ops        = &mtk_afe_i2s0_vul2_ops,
	.probe      = mtk_afe_i2s0_vul2_probe,
};

static int mtk_i2s0_vul2_probe(struct platform_device *pdev)
{
	pr_debug("%s\n", __func__);

	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	if (!pdev->dev.dma_mask)
		pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;

	if (pdev->dev.of_node)
		dev_set_name(&pdev->dev, "%s", MT_SOC_I2S0_VUL2_PCM);
	pdev->name = pdev->dev.kobj.name;

	pr_debug("%s: dev name %s\n", __func__, dev_name(&pdev->dev));

	return snd_soc_register_component(&pdev->dev,
					 &mtk_soc_component,
					 NULL,
					 0);
}

static int mtk_afe_i2s0_vul2_probe(struct snd_soc_component *component)
{
	pr_debug("%s\n", __func__);

	snd_soc_add_component_controls(component, Audio_i2s0_vul2_controls,
				      ARRAY_SIZE(Audio_i2s0_vul2_controls));
	AudDrv_Allocate_mem_Buffer(component->dev, Soc_Aud_Digital_Block_MEM_VUL2,
				   UL1_MAX_BUFFER_SIZE);
	I2s0_vul2_dma_buf =  Get_Mem_Buffer(Soc_Aud_Digital_Block_MEM_VUL2);
	mAudioDigitalI2S =  kzalloc(sizeof(struct audio_digital_i2s), GFP_KERNEL);

	return 0;
}

static int mtk_i2s0_vul2_remove(struct platform_device *pdev)
{
	pr_debug("%s\n", __func__);

	snd_soc_unregister_component(&pdev->dev);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id mt_soc_i2s0_vul2_of_ids[] = {
	{ .compatible = "mediatek,mt_soc_i2s0_vul2_capture", },
	{}
};
#endif

static struct platform_driver mtk_afe_i2s0_vul2_driver = {
	.driver = {
		.name = MT_SOC_I2S0_VUL2_PCM,
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = mt_soc_i2s0_vul2_of_ids,
#endif
	},
	.probe = mtk_i2s0_vul2_probe,
	.remove = mtk_i2s0_vul2_remove,
};

#ifndef CONFIG_OF
static struct platform_device *soc_mtkafe_i2s0_vul2_dev;
#endif

static int __init mtk_soc_i2s0_vul2_platform_init(void)
{
	int ret = 0;

	pr_debug("%s\n", __func__);
#ifndef CONFIG_OF
	soc_mtkafe_i2s0_vul2_dev = platform_device_alloc(MT_SOC_I2S0_VUL2_PCM, -1);
	if (!soc_mtkafe_i2s0_vul2_dev)
		return -ENOMEM;
	ret = platform_device_add(soc_mtkafe_i2s0_vul2_dev);
	if (ret != 0) {
		platform_device_put(soc_mtkafe_i2s0_vul2_dev);
		return ret;
	}
#endif
	ret = platform_driver_register(&mtk_afe_i2s0_vul2_driver);

	return ret;
}
module_init(mtk_soc_i2s0_vul2_platform_init);

static void __exit mtk_soc_platform_exit(void)
{
	pr_debug("%s\n", __func__);
	platform_driver_unregister(&mtk_afe_i2s0_vul2_driver);
}
module_exit(mtk_soc_platform_exit);

MODULE_DESCRIPTION("AFE PCM module platform driver");
MODULE_LICENSE("GPL");
