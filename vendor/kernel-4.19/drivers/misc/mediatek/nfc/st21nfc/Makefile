#
# Makefile for ST21NFC
#

ccflags-y += -I$(srctree)/drivers/misc/mediatek/base/power/include
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat
obj-$(CONFIG_ST21NFC_CHIP_SUPPORT) +=st21nfc.o
ccflags-$(CONFIG_NFC_DEBUG) := -DDEBUG
