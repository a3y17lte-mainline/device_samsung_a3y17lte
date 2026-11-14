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
    $(DEVICE_PATH)/rootdir/etc/ueventd.samsungexynos7870.rc:$(TARGET_COPY_OUT_VENDOR)/etc/ueventd.rc \
    $(DEVICE_PATH)/rootdir/etc/init.recovery.samsungexynos7870.rc:$(TARGET_COPY_OUT_RECOVERY)/root/init.recovery.samsungexynos7870.rc

# Overlays (RRO): hardware keys, lid, brightness, power profile, Wi-Fi bands
PRODUCT_PACKAGES += \
    FrameworksResA3y17lte \
    WifiResA3y17lte

# Feature permissions
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/handheld_core_hardware.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/handheld_core_hardware.xml \
    frameworks/native/data/etc/android.hardware.touchscreen.multitouch.jazzhand.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.touchscreen.multitouch.jazzhand.xml \
    frameworks/native/data/etc/android.hardware.usb.host.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.usb.host.xml \
    frameworks/native/data/etc/android.hardware.usb.accessory.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.usb.accessory.xml \
    frameworks/native/data/etc/android.software.midi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.midi.xml

# Media: software Codec2 codecs only (no mainline MFC driver for the 7870)
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/configs/media/media_codecs.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_c2_audio.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_c2_audio.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_c2_telephony.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_c2_telephony.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_c2_tv.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_c2_tv.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_c2_video.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_c2_video.xml

# Audio: stub AIDL HAL (r_submix, no speaker/mic) until the COD3026X driver exists;
# without any audio HAL audioserver crash-loops.
PRODUCT_PACKAGES += \
    android.hardware.audio.service-aidl.example \
    android.hardware.audio.effect.service-aidl.example

# Input
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/configs/keylayout/gpio-keys.kl:$(TARGET_COPY_OUT_VENDOR)/usr/keylayout/gpio-keys.kl \
    $(DEVICE_PATH)/configs/keylayout/tm2-touchkey.kl:$(TARGET_COPY_OUT_VENDOR)/usr/keylayout/tm2-touchkey.kl \
    $(DEVICE_PATH)/configs/idc/Imagis_capacitive_touchscreen.idc:$(TARGET_COPY_OUT_VENDOR)/usr/idc/Imagis_capacitive_touchscreen.idc

# Graphics: drm_hwcomposer (HWC3, in AOSP). Mesa Panfrost (GLES 3.1, no Vulkan on Midgard)
# and the gralloc allocator/mapper need our own integration: AOSP 17's external/mesa3d builds
# only gfxstream, and AOSP minigbm has no backend for Exynos DRM + Panfrost. TODO.
PRODUCT_PACKAGES += \
    android.hardware.composer.hwc3-service.drm
PRODUCT_VENDOR_PROPERTIES += \
    ro.hardware.egl=mesa \
    ro.opengles.version=196609 \
    debug.renderengine.backend=skiaglthreaded \
    debug.hwui.renderer=skiagl
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.opengles.aep.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.opengles.aep.xml

# Health (s2mu005-fuel-gauge / s2mu004-charger through the power_supply class), also the
# offline-charging UI (--charger, see "on charger" in the init rc)
PRODUCT_PACKAGES += \
    android.hardware.health-service.example \
    android.hardware.health-service.example_recovery \
    charger_res_images

# KeyMint / Gatekeeper: non-secure software implementations (no TEE driver; see the
# security model): the Rust KeyMint reference TA and the software Gatekeeper.
PRODUCT_PACKAGES += \
    android.hardware.security.keymint-service.nonsecure \
    android.hardware.gatekeeper-service.nonsecure

# Lights: LineageOS light HAL (scans /sys/class/backlight). USB: AOSP example HAL, a real
# implementation over /sys/class/typec (our TCPM port). Power: the example HAL is a no-op,
# fine for bring-up.
# No USB gadget HAL: the example one hard-codes its UDC (11110000.dwc3), so the framework
# uses the sys.usb.config path of AOSP's init.usb.configfs.rc instead.
# TODO, no usable HAL exists: vibrator (the AOSP example is a fake, LineageOS's is
# timed_output; regulator-haptic is an input force-feedback device), thermal (the example
# reports a constant 30.1 C; ours needs the soc-thermal zone).
PRODUCT_PACKAGES += \
    android.hardware.light-service.lineage \
    android.hardware.power-service.example \
    android.hardware.usb-service.example

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
    external/drm_hwcomposer
