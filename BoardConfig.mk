#
# Copyright (C) 2021 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

# Inherit from sm8250-common
include device/xiaomi/sm8250-common/BoardConfigCommon.mk

DEVICE_PATH := device/xiaomi/lmi

# Broken rules
BUILD_BROKEN_DUP_RULES := true
BUILD_BROKEN_ELF_PREBUILT_PRODUCT_COPY_FILES := true

# Display
TARGET_SCREEN_DENSITY := 440

# Kernel
TARGET_KERNEL_SOURCE := kernel/xiaomi/lmi
TARGET_KERNEL_VERSION := 4.19
TARGET_KERNEL_ADDITIONAL_FLAGS := LLVM=1 LLVM_IAS=1
TARGET_KERNEL_CONFIG := lmi_defconfig

# OTA assert
TARGET_OTA_ASSERT_DEVICE := lmi

# Properties
TARGET_VENDOR_PROP += $(DEVICE_PATH)/vendor.prop

# Inherit from the proprietary version
include vendor/xiaomi/lmi/BoardConfigVendor.mk
