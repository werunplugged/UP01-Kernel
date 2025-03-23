/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mt6768-mt6358-ref.h  --  Mediatek 6768 audio driver ref device definition
 *
 * Copyright (c) 2022 MediaTek Inc.
 */

#ifndef _MT6768_MT6358_REF_H_
#define _MT6768_MT6358_REF_H_

void ref_ext_amp_switch(bool enable);
void ref_parse_dts_node(void);

void ref_AudDrv_GPIO_Single_Speaker_Sel(bool enable, int mode);
void ref_AudDrv_GPIO_Dual_Speaker_Sel(bool enable, int mode);
int ref_MidTest_AudDrv_GPIO_Speaker_Get(struct snd_kcontrol *kcontrol,
             struct snd_ctl_elem_value *ucontrol);
int ref_MidTest_AudDrv_GPIO_Speaker_Set(struct snd_kcontrol *kcontrol,
             struct snd_ctl_elem_value *ucontrol);

int ref_2N1_Speaker_Get(struct snd_kcontrol *kcontrol,
             struct snd_ctl_elem_value *ucontrol);
int ref_2N1_Speaker_Set(struct snd_kcontrol *kcontrol,
             struct snd_ctl_elem_value *ucontrol);
int ref_LoopBack_Get(struct snd_kcontrol *kcontrol,
                   struct snd_ctl_elem_value *ucontrol);
int ref_LoopBack_Set(struct snd_kcontrol *kcontrol,
                   struct snd_ctl_elem_value *ucontrol);
int ref_Amp_FM_PA_MODE_Get(struct snd_kcontrol *kcontrol,
                            struct snd_ctl_elem_value *ucontrol);

int ref_Amp_FM_PA_MODE_Set(struct snd_kcontrol *kcontrol,
                            struct snd_ctl_elem_value *ucontrol);
int ref_Amp_PA_Type_Get(struct snd_kcontrol *kcontrol,
                            struct snd_ctl_elem_value *ucontrol);

int ref_Amp_PA_Type_Set(struct snd_kcontrol *kcontrol,
                            struct snd_ctl_elem_value *ucontrol);

#endif

