#
# Copyright (C) 2016 MediaTek Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#

ccflags-y += -I$(srctree)/drivers/misc/mediatek/base/power/$(MTK_PLATFORM)/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/base/power/include/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/base/power/include/clkbuf_v1/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/base/power/include/clkbuf_v1/$(MTK_PLATFORM)/
ccflags-y += -I$(srctree)/drivers/scsi/ufs/

ARCH_MTK_PROJECT := $(subst ",,$(CONFIG_ARCH_MTK_PROJECT))
ifneq ($(filter k69v1_64_g85_basic_ref_k419 k69v1_2g_fhdp_ago_ref k69v1_2g_fhdp_ago_basic_ref, $(ARCH_MTK_PROJECT)),)
	ccflags-y += -DMT6768_REF_DEV
endif

obj-y += mtk_clkbuf_hw.o

