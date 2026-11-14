#
# S-Boot boot and recovery images: AOSP header v0 with the DTBH dt.img size in the
# header_version slot, raw Image, ramdisk with an appended bootconfig.
# Replaces the AOSP packing (BOARD_CUSTOM_BOOTIMG_MK).
#

A3_MKBOOTIMG := $(DEVICE_PATH)/tools/mkbootimg_samsung.py
A3_DTB := $(KERNEL_OUT)/arch/$(KERNEL_ARCH)/boot/dts/exynos/exynos7870-a3y17lte.dtb

# The packer builds the DTBH table itself from the kernel-built DTB (S-Boot selects it by
# the model_info-* properties) and leaves free space in it for S-Boot's in-place edits.
A3_MKBOOTIMG_ARGS := \
	--base $(BOARD_KERNEL_BASE) \
	--pagesize $(BOARD_KERNEL_PAGESIZE) \
	--dtb $(A3_DTB) \
	--bootconfig $(BOARD_A3_BOOTCONFIG) \
	--sboot-image-header \
	--seandroid

$(INSTALLED_BOOTIMAGE_TARGET): $(A3_MKBOOTIMG) $(INSTALLED_KERNEL_TARGET) $(INSTALLED_RAMDISK_TARGET) \
		$(BOARD_A3_BOOTCONFIG)
	$(call pretty,"Target boot image: $@")
	$(hide) python3 $(A3_MKBOOTIMG) $(A3_MKBOOTIMG_ARGS) \
		--kernel $(INSTALLED_KERNEL_TARGET) --ramdisk $(INSTALLED_RAMDISK_TARGET) -o $@
	$(hide) $(call assert-max-image-size,$@,$(BOARD_BOOTIMAGE_PARTITION_SIZE),raw)
	@echo "Made boot image: $@"

$(INSTALLED_RECOVERYIMAGE_TARGET): $(A3_MKBOOTIMG) $(recoveryimage-deps) $(BOARD_A3_BOOTCONFIG)
	$(call pretty,"Target recovery image: $@")
	$(hide) python3 $(A3_MKBOOTIMG) $(A3_MKBOOTIMG_ARGS) \
		--kernel $(recovery_kernel) --ramdisk $(recovery_ramdisk) -o $@
	$(hide) $(call assert-max-image-size,$@,$(BOARD_RECOVERYIMAGE_PARTITION_SIZE),raw)
	@echo "Made recovery image: $@"
