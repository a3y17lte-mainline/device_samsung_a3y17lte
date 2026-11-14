#
# S-Boot boot and recovery images: AOSP header v0 with the DTBH dt.img size in the
# header_version slot, raw Image, ramdisk with an appended bootconfig.
# Replaces the AOSP packing (BOARD_CUSTOM_BOOTIMG_MK); LineageOS's dt_image.mk is skipped
# with a custom bootimg makefile, so the dt.img rule lives here too.
#

A3_MKBOOTIMG := $(DEVICE_PATH)/tools/mkbootimg_samsung.py
A3_DTBTOOL := $(HOST_OUT_EXECUTABLES)/$(TARGET_CUSTOM_DTBTOOL)$(HOST_EXECUTABLE_SUFFIX)
A3_DTB := $(KERNEL_OUT)/arch/$(KERNEL_ARCH)/boot/dts/exynos/exynos7870-a3y17lte.dtb

# DTBH table: only this board's DTB carries the model_info-* properties S-Boot matches on.
INSTALLED_DTIMAGE_TARGET := $(PRODUCT_OUT)/dt.img
$(INSTALLED_DTIMAGE_TARGET): $(A3_DTBTOOL) $(INSTALLED_KERNEL_TARGET)
	$(call pretty,"Target dt image: $@")
	$(hide) rm -rf $(PRODUCT_OUT)/dtbh && mkdir -p $(PRODUCT_OUT)/dtbh
	$(hide) cp $(A3_DTB) $(PRODUCT_OUT)/dtbh/
	$(hide) $(A3_DTBTOOL) -o $@ -s $(BOARD_KERNEL_PAGESIZE) -p $(KERNEL_OUT)/scripts/dtc/ $(PRODUCT_OUT)/dtbh/

A3_MKBOOTIMG_ARGS := \
	--base $(BOARD_KERNEL_BASE) \
	--pagesize $(BOARD_KERNEL_PAGESIZE) \
	--dt $(INSTALLED_DTIMAGE_TARGET) \
	--bootconfig $(BOARD_A3_BOOTCONFIG) \
	--sboot-image-header \
	--seandroid

$(INSTALLED_BOOTIMAGE_TARGET): $(A3_MKBOOTIMG) $(INSTALLED_KERNEL_TARGET) $(INSTALLED_RAMDISK_TARGET) \
		$(INSTALLED_DTIMAGE_TARGET) $(BOARD_A3_BOOTCONFIG)
	$(call pretty,"Target boot image: $@")
	$(hide) python3 $(A3_MKBOOTIMG) $(A3_MKBOOTIMG_ARGS) \
		--kernel $(INSTALLED_KERNEL_TARGET) --ramdisk $(INSTALLED_RAMDISK_TARGET) -o $@
	$(hide) $(call assert-max-image-size,$@,$(BOARD_BOOTIMAGE_PARTITION_SIZE),raw)
	@echo "Made boot image: $@"

$(INSTALLED_RECOVERYIMAGE_TARGET): $(A3_MKBOOTIMG) $(recoveryimage-deps) \
		$(INSTALLED_DTIMAGE_TARGET) $(BOARD_A3_BOOTCONFIG)
	$(call pretty,"Target recovery image: $@")
	$(hide) python3 $(A3_MKBOOTIMG) $(A3_MKBOOTIMG_ARGS) \
		--kernel $(recovery_kernel) --ramdisk $(recovery_ramdisk) -o $@
	$(hide) $(call assert-max-image-size,$@,$(BOARD_RECOVERYIMAGE_PARTITION_SIZE),raw)
	@echo "Made recovery image: $@"
