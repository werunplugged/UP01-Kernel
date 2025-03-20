# SPDX-License-Identifier: GPL-2.0

ccflags-y += -I$(srctree)/drivers/usb/typec
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/typec/tcpc/inc

obj-y += mux_switch.o

ifeq ($(CONFIG_OCP96011_I2C), y)
obj-y += ocp96011-i2c.o
endif
ifeq ($(CONFIG_USBSWITCH_HL5280), y)
obj-y += hl5280/ 
endif
