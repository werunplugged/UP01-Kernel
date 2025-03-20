# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2019 MediaTek Inc.

KERNEL_ENV_PATH := $(call my-dir)
KERNEL_ROOT_DIR := $(PWD)

define touch-kernel-image-timestamp
if [ -e $(1) ] && [ -e $(2) ] && cmp -s $(1) $(2); then \
 echo $(2) has no change;\
 mv -f $(1) $(2);\
else \
 rm -f $(1);\
fi
endef

define move-kernel-module-files
v=`cat $(2)/include/config/kernel.release`;\
for i in `grep -h '\.ko' /dev/null $(2)/.tmp_versions/*.mod`; do \
 o=`basename $$i`;\
 if [ -e $(1)/lib/modules/$$o ] && cmp -s $(1)/lib/modules/$$v/kernel/$$i $(1)/lib/modules/$$o; then \
  echo $(1)/lib/modules/$$o has no change;\
 else \
  echo Update $(1)/lib/modules/$$o;\
  mv -f $(1)/lib/modules/$$v/kernel/$$i $(1)/lib/modules/$$o;\
 fi;\
done
endef

###############################
define cp-up-share-to-kernel-config
touch $(UP_SHARE_CONFIG_FILE);\
echo some up_share_config_begin $(1) $(2);\
startLine=`grep -n "up_share_config_begin" $(2) | head -1 | cut -d ":" -f 1`;\
if [ $$startLine -ne 0 ];then \
echo some up_share_config_begin find,$$startLine;\
endLine=`cat $(2) | wc -l`;\
sed -i "$$startLine"','$$endLine'd' $(2);\
fi;\
startLine2=`grep -n "up_custom_config_begin" $(2) | head -1 | cut -d ":" -f 1`;\
if [ $$startLine2 -ne 0 ];then \
echo some up_custom_config_begin find,$$startLine2;\
endLine2=`cat $(2) | wc -l`;\
sed -i "$$startLine2"','$$endLine2'd' $(2);\
fi;\
cat $(UP_SHARE_CONFIG_FILE)>>$(KERNEL_CONFIG_FILE)
endef
#########################
###############################
define cp-up-config-to-kernel-config
echo some up_custom_config_begin $(1) $(2);\
startLine=`grep -n "up_custom_config_begin" $(2) | head -1 | cut -d ":" -f 1`;\
if [ $$startLine -ne 0 ];then \
echo some up_custom_config_begin find,$$startLine;\
endLine=`cat $(2) | wc -l`;\
sed -i "$$startLine"','$$endLine'd' $(2);\
fi;\
cat $(UP_KERNEL_CONFIG_FILE)>>$(KERNEL_CONFIG_FILE)
endef
#########################
define clean-kernel-module-dirs
rm -rf $(1)/lib/modules/$(if $(2),`cat $(2)/include/config/kernel.release`,*/)
endef

# '\\' in command is wrongly replaced to '\\\\' in kernel/out/arch/arm/boot/compressed/.piggy.xzkern.cmd
define fixup-kernel-cmd-file
if [ -e $(1) ]; then cp $(1) $(1).bak; sed -e 's/\\\\\\\\/\\\\/g' < $(1).bak > $(1); rm -f $(1).bak; fi
endef

ifneq ($(strip $(TARGET_NO_KERNEL)),true)
  KERNEL_DIR := $(KERNEL_ENV_PATH)
  mkfile_path := $(abspath $(lastword $(MAKEFILE_LIST)))
  current_dir := $(notdir $(patsubst %/,%,$(dir $(mkfile_path))))
  UP_SHARE_CONFIG_FILE:=$(KERNEL_DIR)/arch/$(TARGET_ARCH)/configs/up_share_config
  UP_KERNEL_CONFIG_FILE:=$(KERNEL_DIR)/arch/$(KERNEL_TARGET_ARCH)/configs/up_kernel_config
  kernel_build_config_suffix := .mtk
  ifeq ($(KERNEL_TARGET_ARCH),arm64)
    kernel_build_config_suffix := $(kernel_build_config_suffix).aarch64
    ifeq ($(strip $(TARGET_KERNEL_USE_CLANG)),true)
    else
      kernel_build_config_suffix := $(kernel_build_config_suffix).gcc
    endif
  else
    kernel_build_config_suffix := $(kernel_build_config_suffix).arm
    ifeq ($(strip $(TARGET_KERNEL_USE_CLANG)),true)
    else
      $(error TARGET_KERNEL_USE_CLANG is not set)
    endif
  endif
  ifeq ($(PLATFORM_VERSION),Tiramisu)
    kernel_build_config_suffix := $(kernel_build_config_suffix).tiramisu
  endif
  include $(current_dir)/build.config$(kernel_build_config_suffix)

  ARGS := CROSS_COMPILE=$(CROSS_COMPILE)
  ifneq ($(LLVM),)
    ARGS += LLVM=1
    ifneq ($(filter-out false,$(USE_CCACHE)),)
      CCACHE_EXEC ?= /usr/bin/ccache
      CCACHE_EXEC := $(abspath $(wildcard $(CCACHE_EXEC)))
    else
      CCACHE_EXEC :=
    endif
    ifneq ($(CCACHE_EXEC),)
      ARGS += CCACHE_CPP2=yes CC='$(CCACHE_EXEC) clang'
    else
      ARGS += CC=clang
    endif
    ifneq ($(LLVM_IAS),)
      ARGS += LLVM_IAS=$(LLVM_IAS)
    endif
    ifeq ($(HOSTCC),)
      ifneq ($(CC),)
        ARGS += HOSTCC=$(CC)
      endif
    else
      ARGS += HOSTCC=$(HOSTCC)
    endif
    ifneq ($(LD),)
      ARGS += LD=$(LD) HOSTLD=$(LD)
      ifneq ($(suffix $(LD)),)
        ARGS += HOSTLDFLAGS=-fuse-ld=$(subst .,,$(suffix $(LD)))
      endif
    endif
    ifneq ($(LD_LIBRARY_PATH),)
      ARGS += LD_LIBRARY_PATH=$(KERNEL_ROOT_DIR)/$(LD_LIBRARY_PATH)
    endif
  endif

  TARGET_KERNEL_CROSS_COMPILE := $(KERNEL_ROOT_DIR)/$(LINUX_GCC_CROSS_COMPILE_PREBUILTS_BIN)/$(CROSS_COMPILE)

  ifeq ($(wildcard $(TARGET_PREBUILT_KERNEL)),)
    KERNEL_OUT ?= $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ
    KERNEL_ROOT_OUT := $(if $(filter /% ~%,$(KERNEL_OUT)),,$(KERNEL_ROOT_DIR)/)$(KERNEL_OUT)
    ifeq ($(KERNEL_TARGET_ARCH), arm64)
        KERNEL_ZIMAGE_OUT := $(KERNEL_OUT)/arch/$(KERNEL_TARGET_ARCH)/boot/Image.gz
        KERNEL_DTB_TARGET := $(KERNEL_OUT)/arch/$(KERNEL_TARGET_ARCH)/boot/dts/mediatek/$(TARGET_BOARD_PLATFORM).dtb
    else
        KERNEL_ZIMAGE_OUT := $(KERNEL_OUT)/arch/$(KERNEL_TARGET_ARCH)/boot/zImage
        KERNEL_DTB_TARGET := $(KERNEL_OUT)/arch/$(KERNEL_TARGET_ARCH)/boot/dts/$(TARGET_BOARD_PLATFORM).dtb
    endif

    INSTALLED_MTK_DTB_TARGET := $(BOARD_PREBUILT_DTBIMAGE_DIR)/mtk_dtb
    BUILT_KERNEL_TARGET := $(KERNEL_ZIMAGE_OUT).bin
    INSTALLED_KERNEL_TARGET := $(PRODUCT_OUT)/kernel
    TARGET_KERNEL_CONFIG := $(KERNEL_OUT)/.config
    KERNEL_CONFIG_FILE := $(KERNEL_DIR)/arch/$(KERNEL_TARGET_ARCH)/configs/$(word 1,$(KERNEL_DEFCONFIG))
    KERNEL_MAKE_OPTION := O=$(KERNEL_ROOT_OUT) ARCH=$(KERNEL_TARGET_ARCH) $(ARGS) ROOTDIR=$(KERNEL_ROOT_DIR)
    KERNEL_MAKE_PATH_OPTION := /usr/bin:/bin
    KERNEL_MAKE_OPTION += PATH=$(KERNEL_ROOT_DIR)/$(CLANG_PREBUILT_BIN):$(KERNEL_ROOT_DIR)/$(LINUX_GCC_CROSS_COMPILE_PREBUILTS_BIN):$(KERNEL_MAKE_PATH_OPTION):$$PATH
  else
    BUILT_KERNEL_TARGET := $(TARGET_PREBUILT_KERNEL)
  endif #TARGET_PREBUILT_KERNEL is empty
    KERNEL_MAKE_OPTION += PROJECT_DTB_NAMES='$(PROJECT_DTB_NAMES)'
endif #TARGET_NO_KERNEL
