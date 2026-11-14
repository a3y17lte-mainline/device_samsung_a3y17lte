#
# Samsung Galaxy A3 (2017), SM-A320F, Exynos 7870
# Mainline-based kernel (kernel_samsung_exynos7870, android17-6.18), no proprietary HALs.
#
# Preliminary: written before the first build, values marked VERIFY are not tested yet.
#

DEVICE_PATH := device/samsung/a3y17lte

# Architecture: arm64 only (8x Cortex-A53)
TARGET_ARCH := arm64
TARGET_ARCH_VARIANT := armv8-a
TARGET_CPU_ABI := arm64-v8a
TARGET_CPU_VARIANT := cortex-a53
TARGET_SUPPORTS_64_BIT_APPS := true

TARGET_BOARD_PLATFORM := exynos7870
TARGET_BOOTLOADER_BOARD_NAME := universal7870
TARGET_NO_BOOTLOADER := true
TARGET_NO_RADIOIMAGE := true

# Kernel: built from source (kernel_samsung_exynos7870: android17-6.18 + the mainline
# a3y17lte series). Raw Image: S-Boot cannot decompress. Modules go to /vendor/lib/modules
# and are loaded by modalias (ueventd), like mdev does on the Alpine test system.
TARGET_KERNEL_SOURCE := kernel/samsung/exynos7870
TARGET_KERNEL_CONFIG := a3y17lte_defconfig
TARGET_KERNEL_ARCH := arm64
BOARD_KERNEL_IMAGE_NAME := Image

# Boot image: S-Boot loads it directly. Header v0, page 2048, raw Image (S-Boot cannot
# decompress), gzip ramdisk with an appended bootconfig, DTBH dt.img. S-Boot ignores the
# load addresses; these are the stock values.
BOARD_KERNEL_BASE := 0x10000000
BOARD_KERNEL_PAGESIZE := 2048
BOARD_BOOT_HEADER_VERSION := 0
BOARD_RAMDISK_USE_LZ4 := false
BOARD_KERNEL_SEPARATED_DT := true
BOARD_CUSTOM_BOOTIMG_MK := $(DEVICE_PATH)/bootimg.mk
# DTBH table from the kernel-built DTB, by hardware/samsung's dtbhtool (model_info-* select it)
TARGET_CUSTOM_DTBTOOL := dtbhtoolExynos
BOARD_A3_BOOTCONFIG := $(DEVICE_PATH)/bootconfig.txt
# S-Boot replaces the cmdline with its own (androidboot.hardware=samsungexynos7870 etc.);
# our parameters travel in the bootconfig instead.
BOARD_KERNEL_CMDLINE :=

# Partitions (PIT, unchanged: no repartitioning). Sizes in bytes.
BOARD_FLASH_BLOCK_SIZE := 131072
BOARD_BOOTIMAGE_PARTITION_SIZE := 33554432
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 39845888
BOARD_CACHEIMAGE_PARTITION_SIZE := 209715200
BOARD_USERDATAIMAGE_PARTITION_SIZE := 11077156864
BOARD_CACHEIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_USERDATAIMAGE_FILE_SYSTEM_TYPE := f2fs
TARGET_USERIMAGES_USE_EXT4 := true
TARGET_USERIMAGES_USE_F2FS := true

# Dynamic partitions: super lives on the PIT partition SYSTEM (mmcblk0p19).
# Non-A/B, no virtual A/B. androidboot.super_partition=SYSTEM comes from the bootconfig.
# VERIFY with lpdump after the first build: block device name and super name.
BOARD_SUPER_PARTITION_SIZE := 4194304000
BOARD_SUPER_PARTITION_BLOCK_DEVICES := SYSTEM
BOARD_SUPER_PARTITION_SYSTEM_DEVICE_SIZE := 4194304000
BOARD_SUPER_PARTITION_METADATA_DEVICE := SYSTEM
BOARD_SUPER_PARTITION_GROUPS := a3_dynamic_partitions
# super size minus 4 MiB for metadata
BOARD_A3_DYNAMIC_PARTITIONS_SIZE := 4190109696
BOARD_A3_DYNAMIC_PARTITIONS_PARTITION_LIST := system system_ext product vendor

BOARD_SYSTEMIMAGE_FILE_SYSTEM_TYPE := erofs
BOARD_SYSTEM_EXTIMAGE_FILE_SYSTEM_TYPE := erofs
BOARD_PRODUCTIMAGE_FILE_SYSTEM_TYPE := erofs
BOARD_VENDORIMAGE_FILE_SYSTEM_TYPE := erofs
BOARD_EROFS_COMPRESSOR := lz4

TARGET_COPY_OUT_SYSTEM_EXT := system_ext
TARGET_COPY_OUT_PRODUCT := product
TARGET_COPY_OUT_VENDOR := vendor
TARGET_COPY_OUT_ODM := vendor/odm

# /metadata on the PIT partition HIDDEN (60 MiB): needs the user's yes before it is formatted.
BOARD_USES_METADATA_PARTITION := true

# No AVB: S-Boot does not know vbmeta.
BOARD_AVB_ENABLE := false

# Recovery (Lineage recovery comes last; RECOVERY holds our U-Boot during bring-up)
TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/rootdir/etc/fstab.samsungexynos7870
TARGET_RECOVERY_PIXEL_FORMAT := RGBX_8888

# Display / graphics: Exynos DRM + Mesa Panfrost (GLES 3.1, no Vulkan on Midgard),
# minigbm, drm_hwcomposer.
TARGET_SCREEN_DENSITY := 320
BOARD_GPU_DRIVERS := panfrost
BOARD_MESA3D_USES_MESON_BUILD := true
BOARD_MESA3D_GALLIUM_DRIVERS := panfrost
BOARD_MESA3D_VULKAN_DRIVERS :=
TARGET_USES_HWC2 := true

# Wi-Fi: QCA9377 on mainline ath10k_sdio (nl80211)
BOARD_WLAN_DEVICE := default
BOARD_WPA_SUPPLICANT_DRIVER := NL80211
BOARD_HOSTAPD_DRIVER := NL80211
BOARD_WPA_SUPPLICANT_PRIVATE_LIB := lib_driver_cmd_default
BOARD_HOSTAPD_PRIVATE_LIB := lib_driver_cmd_default
WPA_SUPPLICANT_VERSION := VER_0_8_X
WIFI_HIDL_FEATURE_DUAL_INTERFACE := false

# Bluetooth: hci_qca in the kernel, HCI user channel from userspace
BOARD_HAVE_BLUETOOTH := true

# Properties
TARGET_VENDOR_PROP += $(DEVICE_PATH)/vendor.prop

# SELinux: permissive during bring-up (bootconfig androidboot.selinux)
BOARD_VENDOR_SEPOLICY_DIRS += $(DEVICE_PATH)/sepolicy/vendor

# VINTF
DEVICE_MANIFEST_FILE := $(DEVICE_PATH)/manifest.xml

# Security patch level: follows the platform
VENDOR_SECURITY_PATCH := $(PLATFORM_SECURITY_PATCH)
