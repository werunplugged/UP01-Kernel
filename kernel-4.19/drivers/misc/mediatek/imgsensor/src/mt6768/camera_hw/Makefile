# SPDX-License-Identifier: GPL-2.0

#
# Makefile for misc devices that really don't fit anywhere else.
#
ccflags-y += -I$(MTK_CUSTOM_PATH)/imgsensor/inc \

obj-y		+= regulator/
obj-y		+= gpio/
#obj-y		+= mt6306/
obj-y		+= mclk/
obj-y		+= imgsensor_cfg_table.o

ARCH_MTK_PROJECT := $(subst ",,$(CONFIG_ARCH_MTK_PROJECT))
ifneq ($(filter k69v1_64_g85_basic_ref_k419 k69v1_64_g85_basic_ref k69v1_2g_fhdp_ago_ref k69v1_2g_fhdp_ago_basic_ref, $(ARCH_MTK_PROJECT)),)
ccflags-y += -DG88_PROJECT
endif
