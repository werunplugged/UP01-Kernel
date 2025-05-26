###
# Copyright (C) Fourier Semiconductor Inc. 2016-2020. All rights reserved.
#

subdir-ccflags-y += -I$(srctree)/sound/soc/mediatek/common
EXTRA_CFLAGS += -Wall -Werror
MODFLAGS = -fno-pic
CFLAGS_MODULE = $(MODFLAGS)
AFLAGS_MODULE = $(MODFLAGS)

snd-soc-awinic-objs := aw882xx.o
snd-soc-awinic-objs += aw_bin_parse.o
snd-soc-awinic-objs += aw_calib.o
snd-soc-awinic-objs += aw_device.o
snd-soc-awinic-objs += aw_dsp.o
snd-soc-awinic-objs += aw_init.o
snd-soc-awinic-objs += aw_monitor.o
obj-$(CONFIG_SND_SOC_AW882XX_2PA) += snd-soc-awinic.o
