#
# Samsung Galaxy A3 (2017): mainline kernel, open-source HALs only.
#
# Preliminary: HAL module names are from AOSP/LineageOS conventions and get checked
# against the LineageOS 24.0 tree at the first build.
#

DEVICE_PATH := device/samsung/a3y17lte

$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit_only.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/emulated_storage.mk)
$(call inherit-product, vendor/samsung/a3y17lte/a3y17lte-vendor.mk)

# 2 GB RAM
$(call inherit-product, frameworks/native/build/phone-xhdpi-2048-dalvik-heap.mk)

# Dynamic partitions on SYSTEM (non-A/B)
PRODUCT_USE_DYNAMIC_PARTITIONS := true
PRODUCT_BUILD_SUPER_PARTITION := true
PRODUCT_SHIPPING_API_LEVEL := 37

# Screen: 720x1280, 4.7"
TARGET_SCREEN_HEIGHT := 1280
TARGET_SCREEN_WIDTH := 720
PRODUCT_AAPT_CONFIG := normal
PRODUCT_AAPT_PREF_CONFIG := xhdpi

# Init / fstab
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/rootdir/etc/fstab.samsungexynos7870:$(TARGET_COPY_OUT_VENDOR)/etc/fstab.samsungexynos7870 \
    $(DEVICE_PATH)/rootdir/etc/fstab.samsungexynos7870:$(TARGET_COPY_OUT_RAMDISK)/fstab.samsungexynos7870 \
    $(DEVICE_PATH)/rootdir/etc/init.samsungexynos7870.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/hw/init.samsungexynos7870.rc \
    $(DEVICE_PATH)/rootdir/etc/init.samsungexynos7870.usb.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/hw/init.samsungexynos7870.usb.rc \
    $(DEVICE_PATH)/rootdir/etc/ueventd.samsungexynos7870.rc:$(TARGET_COPY_OUT_VENDOR)/etc/ueventd.rc

# Input
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/configs/keylayout/gpio-keys.kl:$(TARGET_COPY_OUT_VENDOR)/usr/keylayout/gpio-keys.kl \
    $(DEVICE_PATH)/configs/keylayout/tm2-touchkey.kl:$(TARGET_COPY_OUT_VENDOR)/usr/keylayout/tm2-touchkey.kl \
    $(DEVICE_PATH)/configs/idc/Imagis_capacitive_touchscreen.idc:$(TARGET_COPY_OUT_VENDOR)/usr/idc/Imagis_capacitive_touchscreen.idc

# Graphics: Mesa Panfrost (GLES 3.1), minigbm (gbm_mesa), drm_hwcomposer. No Vulkan on Midgard.
PRODUCT_PACKAGES += \
    libEGL_mesa \
    libGLESv1_CM_mesa \
    libGLESv2_mesa \
    libgallium_dri \
    libglapi \
    hwcomposer.drm \
    android.hardware.composer.hwc3-service.drm \
    android.hardware.graphics.allocator-service.minigbm_gbm_mesa \
    mapper.minigbm_gbm_mesa
PRODUCT_VENDOR_PROPERTIES += \
    ro.hardware.egl=mesa \
    ro.hardware.hwcomposer=drm \
    ro.hardware.gralloc=minigbm_gbm_mesa \
    ro.opengles.version=196609 \
    debug.renderengine.backend=skiaglthreaded \
    debug.hwui.renderer=skiagl
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.opengles.aep.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.opengles.aep.xml

# Health (s2mu005-fuel-gauge / s2mu004-charger through the power_supply class)
PRODUCT_PACKAGES += \
    android.hardware.health-service.example \
    android.hardware.health-service.example_recovery

# KeyMint / Gatekeeper: software (no TEE driver; see the security model)
PRODUCT_PACKAGES += \
    android.hardware.security.keymint-service \
    android.hardware.gatekeeper-service.software

# Lights (panel backlight, touch-key LEDs), vibrator (FF), power, thermal, USB
PRODUCT_PACKAGES += \
    android.hardware.light-service.lineage \
    android.hardware.vibrator-service.example \
    android.hardware.power-service.lineage-libperfmgr \
    android.hardware.thermal-service.example \
    android.hardware.usb-service.example \
    android.hardware.usb.gadget-service.example

# Wi-Fi (ath10k_sdio, nl80211)
PRODUCT_PACKAGES += \
    android.hardware.wifi-service \
    hostapd \
    wpa_supplicant \
    wpa_supplicant.conf
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.wifi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.xml \
    frameworks/native/data/etc/android.hardware.wifi.direct.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.direct.xml

# Bluetooth: Linux HCI user channel (hci_qca in the kernel)
PRODUCT_PACKAGES += \
    android.hardware.bluetooth-service.default
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.bluetooth.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth.xml \
    frameworks/native/data/etc/android.hardware.bluetooth_le.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth_le.xml

# Later (phase 4 HAL order): sensors (IIO), audio (after COD3026X), NFC, camera flash.
# Out of scope for now: RIL/modem, camera, GNSS, fingerprint.

# Soong namespaces
PRODUCT_SOONG_NAMESPACES += \
    $(DEVICE_PATH) \
    external/mesa3d \
    external/minigbm \
    external/drm_hwcomposer
