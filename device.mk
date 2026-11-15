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
# The A320F launched with Android 6.0. A launch level >= 29 would also make check_vintf enforce
# Android 17's kernel config requirements at build time; four are unmet on purpose (see the
# kernel's a3y17lte_defconfig notes: ASHMEM, NF_CT_PROTO_DCCP, SCHED_DEBUG, CFI_CLANG).
PRODUCT_SHIPPING_API_LEVEL := 23

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

# Audio: our AIDL core service (aidl/audio: AOSP's example implementation with a module on
# real ALSA streams and libaudioroute routing) for the a3y17lte card: loudspeaker (TFA9896),
# earpiece, headphones and three mics (COD3026X). Plus r_submix and USB audio. Effects:
# AOSP's effects service and software effects, built here outside its APEX (aidl/audio-effect).
PRODUCT_PACKAGES += \
    android.hardware.audio.service-aidl.a3y17lte \
    android.hardware.audio.effect.service-aidl.a3y17lte \
    libbundleaidl \
    libdownmixaidl \
    libdynamicsprocessingaidl \
    libloudnessenhanceraidl \
    libpreprocessingaidl \
    libreverbaidl \
    libvisualizeraidl \
    audio_policy_configuration_a3y17lte.xml \
    mixer_paths_a3y17lte.xml
# (audio output and microphone features come from handheld_core_hardware.xml)

# Input
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/configs/keylayout/gpio-keys.kl:$(TARGET_COPY_OUT_VENDOR)/usr/keylayout/gpio-keys.kl \
    $(DEVICE_PATH)/configs/keylayout/tm2-touchkey.kl:$(TARGET_COPY_OUT_VENDOR)/usr/keylayout/tm2-touchkey.kl \
    $(DEVICE_PATH)/configs/idc/Imagis_capacitive_touchscreen.idc:$(TARGET_COPY_OUT_VENDOR)/usr/idc/Imagis_capacitive_touchscreen.idc

# Graphics: our Mesa (external/mesa: Panfrost with our Midgard fixes, GLES 3.1, no Vulkan on
# Midgard), minigbm (external/minigbm: our fork, dumb buffers on the Exynos DRM render node,
# imported by Panfrost through dma-buf) and drm_hwcomposer (HWC3).
PRODUCT_PACKAGES += \
    libEGL_mesa \
    libGLESv1_CM_mesa \
    libGLESv2_mesa \
    libgallium_dri \
    android.hardware.graphics.allocator-service.minigbm \
    mapper.minigbm \
    android.hardware.composer.hwc3-service.drm
PRODUCT_VENDOR_PROPERTIES += \
    ro.hardware.egl=mesa \
    drm.gpu.vendor_name=panfrost \
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
# security model): the Rust KeyMint reference TA and the software Gatekeeper. Gatekeeper's
# service only installs through its vendor APEX (the binary is installable: false).
PRODUCT_PACKAGES += \
    android.hardware.security.keymint-service.nonsecure \
    com.android.hardware.gatekeeper.nonsecure

# Lights: LineageOS light HAL (scans /sys/class/backlight). USB: AOSP example HAL, a real
# implementation over /sys/class/typec (our TCPM port).
# No USB gadget HAL: the example one hard-codes its UDC (11110000.dwc3), so the framework
# uses the sys.usb.config path of AOSP's init.usb.configfs.rc instead.
# Vibrator, thermal and power: our own (aidl/): regulator-haptic through input force feedback,
# the kernel thermal zones (soc-thermal, battery-thermal), and CPU boosts through the cpufreq
# policy limits (interaction, launch, battery saver).
PRODUCT_PACKAGES += \
    android.hardware.light-service.lineage \
    android.hardware.power-service.a3y17lte \
    android.hardware.thermal-service.a3y17lte \
    android.hardware.usb-service.example \
    android.hardware.vibrator-service.a3y17lte

# Wi-Fi (ath10k_sdio, nl80211): HAL-less, see BoardConfig.mk
PRODUCT_PACKAGES += \
    hostapd \
    wpa_supplicant \
    wpa_supplicant_a3y17lte.conf
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.wifi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.xml \
    frameworks/native/data/etc/android.hardware.wifi.direct.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.direct.xml

# Bluetooth: Linux HCI user channel (hci_qca in the kernel)
PRODUCT_PACKAGES += \
    android.hardware.bluetooth-service.default
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.bluetooth.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth.xml \
    frameworks/native/data/etc/android.hardware.bluetooth_le.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth_le.xml

# Sensors: our HAL (aidl/sensors) on the sensor hub's IIO devices: accelerometer, gyroscope,
# magnetometer, barometer, light, proximity (wake-up). Rotation vector, gravity and linear
# acceleration are fused by the framework.
PRODUCT_PACKAGES += \
    android.hardware.sensors-service.a3y17lte
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.sensor.accelerometer.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.accelerometer.xml \
    frameworks/native/data/etc/android.hardware.sensor.gyroscope.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.gyroscope.xml \
    frameworks/native/data/etc/android.hardware.sensor.compass.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.compass.xml \
    frameworks/native/data/etc/android.hardware.sensor.barometer.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.barometer.xml \
    frameworks/native/data/etc/android.hardware.sensor.light.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.light.xml \
    frameworks/native/data/etc/android.hardware.sensor.proximity.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.proximity.xml

# NFC (Samsung S3NRN80): our HAL (aidl/nfc) passes NCI packets between Android's NFC stack and
# the kernel's raw NCI device (/dev/s3fwrn5). Reader mode and host card emulation; no eSE (it
# is behind the secure world), no UICC. The chip's firmware is never written.
PRODUCT_PACKAGES += \
    android.hardware.nfc-service.a3y17lte
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.nfc.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.xml \
    frameworks/native/data/etc/android.hardware.nfc.hce.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.hce.xml \
    frameworks/native/data/etc/com.nxp.mifare.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/com.nxp.mifare.xml

# Later: camera flash.
# Out of scope for now: RIL/modem, camera, GNSS, fingerprint.

# Soong namespaces
PRODUCT_SOONG_NAMESPACES += \
    $(DEVICE_PATH) \
    external/drm_hwcomposer
