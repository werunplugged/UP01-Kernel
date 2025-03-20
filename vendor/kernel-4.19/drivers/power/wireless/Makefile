MTK_PLATFORM := $(subst ",,$(CONFIG_MTK_PLATFORM))
subdir-ccflags-y += -Werror -I$(srctree)/drivers/misc/mediatek/typec/tcpc/inc
subdir-ccflags-y += -Werror -I$(srctree)/drivers/misc/mediatek/include/
subdir-ccflags-y += -Werror -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include
subdir-ccflags-y += -Werror -I$(srctree)/drivers/misc/mediatek/usb20/$(MTK_PLATFORM)
ifeq ($(CONFIG_MTK_GAUGE_VERSION),30)
subdir-ccflags-y += -Werror -I$(srctree)/drivers/power/supply/mediatek/charger
subdir-ccflags-y += -Werror -I$(srctree)/drivers/power/supply/mediatek/misc
endif


obj-$(CONFIG_WIRELESS_POWER_MT5728)	+= wireless_power_mt5728.o
