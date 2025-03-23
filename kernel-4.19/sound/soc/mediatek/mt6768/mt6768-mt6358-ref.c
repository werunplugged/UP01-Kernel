// SPDX-License-Identifier: GPL-2.0
//
// mt6768-mt6358-ref.c  --  mt6768 mt6358 ref dev speaker
//
// Copyright (c) 2022 MediaTek Inc.

#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "mt6768-afe-common.h"
#include "mt6768-afe-clk.h"
#include "mt6768-afe-gpio.h"
#include "../../codecs/mt6358.h"
#include "../common/mtk-sp-spk-amp.h"
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include "mt6768-mt6358-ref.h"

int g_pa_type = 0;
int g_fm_pa_mode = 0;
#if defined(CONFIG_SND_SOC_FS1599)
extern fsm_speaker_onn(void);
extern fsm_speaker_off(void);
extern fsm_add_card_controls(struct snd_soc_card *card);
#endif
static bool ref_is_loopback;

struct ext_amp_data {
    int gpio_no;
    int g_dualspeaker_gpio_no;
    int normal_mode;
    int speech_mode;
    int receiver_mode;
    int pa_type_gpio;
    int is_fs1599_pa;
    int fm_mode;
};
struct ext_amp_data g_spk_amp_data = {
    .gpio_no = -1,
    .g_dualspeaker_gpio_no = -1,
    .normal_mode = -1,
    .speech_mode = -1,
    .receiver_mode = -1,
    .pa_type_gpio = -1,
    .is_fs1599_pa = -1,
    .fm_mode = -1,
};

#define SPK_WARM_UP_TIME    (40)/* unit is ms */
#define NORMAL_MODE 0
#define SPEECH_MODE 1
#define RECEIVER_MODE 2
#define FM_MODE 3
#define GAP (2)  /* unit: us */

#define FS_GAP_STA (300)  /* unit: us */
#define FS_GAP_HL (10)    /* unit: us */

static void ref_ext_amp_sel(bool enable, int mode)
{
    if (g_spk_amp_data.gpio_no >= 0 && g_spk_amp_data.normal_mode >= 0 && g_spk_amp_data.speech_mode >= 0){
        if(!ref_is_loopback){
            if (mode == FM_MODE) {
                ref_AudDrv_GPIO_Single_Speaker_Sel(enable, NORMAL_MODE);
            }
            else {
                ref_AudDrv_GPIO_Single_Speaker_Sel(enable, mode);
            }
        }
        if (g_spk_amp_data.g_dualspeaker_gpio_no >= 0){
            ref_AudDrv_GPIO_Dual_Speaker_Sel(enable, mode);
        }
    }
}

void ref_ext_amp_switch(bool enable)
{
    extern bool mtk_get_speech_status(void);
    if (enable) {
        pr_debug("ref_ext_amp_switch+++ ON\n");
        if (g_spk_amp_data.is_fs1599_pa > 0) {
#if defined(CONFIG_SND_SOC_FS1599)
            fsm_speaker_onn();
#endif
        } else {
            if(mtk_get_speech_status()){
                ref_ext_amp_sel(true, SPEECH_MODE);
            } else if (g_fm_pa_mode == 1) {
                ref_ext_amp_sel(true, FM_MODE);
                g_fm_pa_mode = 0;
            } else {
                ref_ext_amp_sel(true, NORMAL_MODE);
            }
            msleep(SPK_WARM_UP_TIME);
        }
    } else {
        pr_debug("ref_ext_amp_switch+++ OFF\n");
        if (g_spk_amp_data.is_fs1599_pa > 0) {
#if defined(CONFIG_SND_SOC_FS1599)
            fsm_speaker_off();
#endif
        } else {
            ref_ext_amp_sel(false, 0);
            udelay(500);
        }
    }
}

void ref_parse_dts_node(void)
{
    struct device_node *np = of_find_compatible_node(NULL, NULL, "ref_audio,audio");
    g_spk_amp_data.gpio_no = -1;
    g_spk_amp_data.speech_mode = -1;
    g_spk_amp_data.normal_mode = -1;
    g_spk_amp_data.receiver_mode = -1;
    g_spk_amp_data.pa_type_gpio = -1;
    g_spk_amp_data.is_fs1599_pa = -1;

    if (np){
		if (of_property_read_u32(np, "extamp_fm_mode", &g_spk_amp_data.fm_mode)) {
		    pr_err("Cannot find extamp_fm_mode!");
		}
		g_spk_amp_data.g_dualspeaker_gpio_no = of_get_named_gpio(np, "dual_speaker_gpio", 0);
		g_spk_amp_data.pa_type_gpio = of_get_named_gpio(np, "amp_type_gpio", 0);
		if(g_spk_amp_data.pa_type_gpio >= 0){
			if(gpio_get_value(g_spk_amp_data.pa_type_gpio) == 0){
				pr_debug("ref_parse_dts_node() use fs pa\n");
				g_pa_type = 1;
				g_spk_amp_data.gpio_no = of_get_named_gpio(np, "fs_extamp_gpio", 0);
			}else{
				pr_debug("ref_parse_dts_node() use aw pa\n");
				g_pa_type = 0;
				g_spk_amp_data.gpio_no = of_get_named_gpio(np, "extamp_gpio", 0);
			}
		} else {
			g_spk_amp_data.gpio_no = of_get_named_gpio(np, "extamp_gpio", 0);
			if(g_spk_amp_data.gpio_no >= 0){
				pr_debug("ref_parse_dts_node() use aw pa\n");
				g_pa_type = 0;
			} else {
				pr_debug("ref_parse_dts_node() use fs pa\n");
			g_spk_amp_data.gpio_no = of_get_named_gpio(np, "fs_extamp_gpio", 0);
			if(g_spk_amp_data.gpio_no >= 0){
				g_pa_type = 1;
			} else {
				if (of_property_read_u32(np, "fs1599_pa", &g_spk_amp_data.is_fs1599_pa)) {
					pr_err("Cannot find fs1599_pa!\n");
				}
			}
			}
		}
        if(g_pa_type == 1){
            if (of_property_read_u32(np, "fs_extamp_mode", &g_spk_amp_data.normal_mode)) {
                pr_err("Cannot find fs_extamp_mode!\n");
            }
            if (of_property_read_u32(np, "fs_extamp_speech_mode", &g_spk_amp_data.speech_mode)) {
                pr_err("Cannot find fs_extamp_speech_mode!\n");
            }
            if (of_property_read_u32(np, "fs_receiver_mode", &g_spk_amp_data.receiver_mode)) {
                pr_err("Cannot find fs_receiver_mode!\n");
            }
        } else {
            if (of_property_read_u32(np, "extamp_mode", &g_spk_amp_data.normal_mode)) {
                pr_err("Cannot find extamp_mode!\n");
            }

            if (of_property_read_u32(np, "extamp_speech_mode", &g_spk_amp_data.speech_mode)) {
                pr_err("Cannot find extamp_speech_mode!\n");
            }
            if (of_property_read_u32(np, "receiver_mode", &g_spk_amp_data.receiver_mode)) {
                pr_err("Cannot find receiver_mode!\n");
            }
		}

        pr_debug("ref_parse_dts_node gpio = %d dualspeaker_gpio %d speech_mode = %d, normal_mode = %d\n",
            g_spk_amp_data.gpio_no, g_spk_amp_data.g_dualspeaker_gpio_no, g_spk_amp_data.speech_mode, g_spk_amp_data.normal_mode);
    }
}

#define RECEIVER_WARM_UP_TIME    (40)   /* unit is ms */

static int ref_AudDrv_GPIO_Amp_Sel_GetMode(int mode)
{
    int ref_amp_mode = 0;
    switch (mode) {
    case NORMAL_MODE:
        ref_amp_mode = g_spk_amp_data.normal_mode;
        break;
    case SPEECH_MODE:
        ref_amp_mode = g_spk_amp_data.speech_mode;
        break;
    case RECEIVER_MODE:
        ref_amp_mode = g_spk_amp_data.receiver_mode;
        break;
    case FM_MODE:
        ref_amp_mode = g_spk_amp_data.fm_mode;
        break;
    }
    return ref_amp_mode;
}

static void ref_AudDrv_GPIO_Single_Speaker_TLTH_delay(void)
{
    if(g_pa_type == 1){
        udelay(FS_GAP_HL);
    } else {
        udelay(GAP);
    }
}

void ref_AudDrv_GPIO_Single_Speaker_Sel(bool enable, int mode)
{
    int i,j;

    j = ref_AudDrv_GPIO_Amp_Sel_GetMode(mode);
    if (enable) {
        if(g_pa_type == 1){
            gpio_set_value(g_spk_amp_data.gpio_no, 1);
            udelay(FS_GAP_STA);
        }
        for (i = 0; i < j; i++) {
                gpio_set_value(g_spk_amp_data.gpio_no, 0);
                ref_AudDrv_GPIO_Single_Speaker_TLTH_delay();
                gpio_set_value(g_spk_amp_data.gpio_no, 1);
                ref_AudDrv_GPIO_Single_Speaker_TLTH_delay();
        }
    } else {
        gpio_set_value(g_spk_amp_data.gpio_no, 0);
    }
}
void ref_AudDrv_GPIO_Dual_Speaker_Sel(bool enable, int mode)
{
    int i,j;
    j = ref_AudDrv_GPIO_Amp_Sel_GetMode(mode);
    if (enable) {
        for (i = 0; i < j; i++) {
                gpio_set_value(g_spk_amp_data.g_dualspeaker_gpio_no, 0);
                udelay(GAP);
                gpio_set_value(g_spk_amp_data.g_dualspeaker_gpio_no, 1);
                udelay(GAP);
        }
    } else {
        gpio_set_value(g_spk_amp_data.g_dualspeaker_gpio_no, 0);
    }
}
static void ref_2N1_Speaker_Change(bool enable)
{
    if (enable) {
        pr_debug("ref_2N1_Speaker_Change+++ ON\n");
        if(ref_is_loopback){
            ref_AudDrv_GPIO_Single_Speaker_Sel(true, NORMAL_MODE);
        } else {
            ref_AudDrv_GPIO_Single_Speaker_Sel(true, RECEIVER_MODE);
        }
        msleep(RECEIVER_WARM_UP_TIME);
    } else {
        pr_debug("ref_2N1_Speaker_Change+++ OFF\n");
        ref_AudDrv_GPIO_Single_Speaker_Sel(false, RECEIVER_MODE);
        udelay(500);
        }
}
int ref_2N1_Speaker_Get(struct snd_kcontrol *kcontrol,
             struct snd_ctl_elem_value *ucontrol)
{
    pr_debug("ref_2N1_Speaker_Get \n");
    return 0;
}
int ref_2N1_Speaker_Set(struct snd_kcontrol *kcontrol,
             struct snd_ctl_elem_value *ucontrol)
{
    if (ucontrol->value.integer.value[0]) {
        ref_2N1_Speaker_Change(true);
    } else {
        ref_2N1_Speaker_Change(false);
    }
    return 0;
}
int ref_MidTest_AudDrv_GPIO_Speaker_Get(struct snd_kcontrol *kcontrol,
             struct snd_ctl_elem_value *ucontrol)
{
    pr_debug("ref_MidTest_AudDrv_GPIO_Speaker_Get \n");
    return 0;
}
int ref_MidTest_AudDrv_GPIO_Speaker_Set(struct snd_kcontrol *kcontrol,
             struct snd_ctl_elem_value *ucontrol)
{
    if (ucontrol->value.integer.value[0]) {
        pr_debug("ref_MidTest_AudDrv_GPIO_Speaker_Sel+++ ON\n");
        if (gpio_get_value(g_spk_amp_data.gpio_no)) {
            ref_AudDrv_GPIO_Single_Speaker_Sel(false, NORMAL_MODE);
        }
        ref_is_loopback = true;
        mdelay(RECEIVER_WARM_UP_TIME);
    } else {
        pr_debug("ref_MidTest_AudDrv_GPIO_Speaker_Sel+++ OFF\n");
        if (!gpio_get_value(g_spk_amp_data.gpio_no)) {
            ref_AudDrv_GPIO_Single_Speaker_Sel(true, NORMAL_MODE);
        }
        ref_is_loopback = false;
        mdelay(RECEIVER_WARM_UP_TIME);
    }
    return 0;
}
int ref_LoopBack_Get(struct snd_kcontrol *kcontrol,
                   struct snd_ctl_elem_value *ucontrol)
{
    pr_debug("%s()\n", __func__);
    return 0;
}

int ref_LoopBack_Set(struct snd_kcontrol *kcontrol,
                   struct snd_ctl_elem_value *ucontrol)
{
    if (ucontrol->value.integer.value[0]) {
        ref_is_loopback = true;
        pr_debug("%s(), loopback true!\n", __func__);
    } else {
        ref_is_loopback = false;
        pr_debug("%s(), loopback false!\n", __func__);
    }
    return 0;
}

int ref_Amp_FM_PA_MODE_Get(struct snd_kcontrol *kcontrol,
                            struct snd_ctl_elem_value *ucontrol)
{
       pr_debug("%s()\n", __func__);
       return 0;
}

int ref_Amp_FM_PA_MODE_Set(struct snd_kcontrol *kcontrol,
                            struct snd_ctl_elem_value *ucontrol)
{
       pr_debug("%s() fm pa type = %d\n", __func__, g_fm_pa_mode);
       g_fm_pa_mode = ucontrol->value.integer.value[0];
       return 0;
}

int ref_Amp_PA_Type_Get(struct snd_kcontrol *kcontrol,
                            struct snd_ctl_elem_value *ucontrol)
{
    pr_debug("%s() amp pa type = %d\n", __func__, g_pa_type);
    ucontrol->value.integer.value[0] = g_pa_type;
    return 0;
}

int ref_Amp_PA_Type_Set(struct snd_kcontrol *kcontrol,
                            struct snd_ctl_elem_value *ucontrol)
{
    pr_debug("%s()\n", __func__);
    return 0;
}

