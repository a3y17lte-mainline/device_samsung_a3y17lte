#
# Samsung Galaxy A3 (2017), SM-A320F
#

# Shipping API level 23 pulls in the legacy HIDL services of base_vendor.mk. The HIDL CAS HAL
# is deprecated at FCM 202604 (check_vintf fails), and OMX has no 64-bit-only build. These
# switches are read when base_vendor.mk is included, so they must come before the inherits.
TARGET_REQUIRES_HIDL_CAS_HAL := false
TARGET_SUPPORTS_OMX_SERVICE := false

$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)
$(call inherit-product, device/samsung/a3y17lte/device.mk)
$(call inherit-product, vendor/lineage/config/common_full_phone.mk)

PRODUCT_NAME := lineage_a3y17lte
PRODUCT_DEVICE := a3y17lte
PRODUCT_BRAND := samsung
PRODUCT_MODEL := SM-A320F
PRODUCT_MANUFACTURER := samsung

PRODUCT_GMS_CLIENTID_BASE := android-samsung
