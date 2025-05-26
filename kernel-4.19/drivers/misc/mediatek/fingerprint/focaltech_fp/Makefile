# Makefile for the focaltech fingerprint driver.

#=======================================================================
# Project configurations, should be configured based on your projects

# If your project is MTK platform, you should set it to y, otherwise set
# it to n(remarking it is also ok)
CONFIG_FINGERPRINT_FOCALTECH_ARCH_MTK := y

# If TEE is emulation which is called REE, you should set it to y
CONFIG_FINGERPRINT_FOCALTECH_TEE_REE := y


#==========================================================================
# source files and ccflags
obj-$(CONFIG_FINGERPRINT_FOCALTECH) := focaltech_fp.o
focaltech_fp-y := ff_core.o

#===For MTK platform
ifeq ($(CONFIG_ARCH_MEDIATEK),y)
CONFIG_FINGERPRINT_FOCALTECH_ARCH_MTK := y
endif

ifeq ($(CONFIG_FINGERPRINT_FOCALTECH_ARCH_MTK),y)
ccflags-$(CONFIG_DRM_MEDIATEK_V2) += -I$(srctree)/drivers/gpu/drm/mediatek/mediatek_v2/
ccflags-y += -DCONFIG_FINGERPRINT_FOCALTECH_ARCH_MTK
focaltech_fp-y += ff_chip.o

# trustkernel maybe can't register SPI and call spi_setup and spi sync functions.
ifeq ($(filter y m, $(CONFIG_TRUSTKERNEL_TEE_SUPPORT)),)
ccflags-y += -DCONFIG_FINGERPRINT_FOCALTECH_SPI_SUPPORT
ccflags-y += -DCONFIG_FINGERPRINT_FOCALTECH_SPICLK_SUPPORT
focaltech_fp-y += ff_spi.o
endif

ifeq ($(CONFIG_FINGERPRINT_FOCALTECH_TEE_REE),y)
ccflags-y += -DCONFIG_FINGERPRINT_FOCALTECH_TEE_REE
endif
endif

#===For other platforms
ifneq ($(CONFIG_FINGERPRINT_FOCALTECH_ARCH_MTK),y)
ifeq ($(CONFIG_FINGERPRINT_FOCALTECH_TEE_REE),y)
ccflags-y += -DCONFIG_FINGERPRINT_FOCALTECH_TEE_REE
ccflags-y += -DCONFIG_FINGERPRINT_FOCALTECH_SPI_SUPPORT
focaltech_fp-y += ff_spi.o ff_chip.o
endif
endif

#=== for microtrust tee
ifneq ($(filter y m, $(CONFIG_MICROTRUST_TEE_SUPPORT)),)
#include trustkernel driver header file, based on your platform
#for kernel 5.10, you should include $(srctree)/drivers/tee/teei/Makefile.include
#for kernel < 5.10, maybe you should #include $(srctree)/drivers/misc/mediatek/teei/Makefile.include
include $(srctree)/drivers/tee/teei/Makefile.include
endif

#=== for trustkernel tee
ifneq ($(filter y m, $(CONFIG_TRUSTKERNEL_TEE_SUPPORT)),)
ccflags-y += -I$(srctree)/drivers/misc/mediatek/tkcore/include/
endif

