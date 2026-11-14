#
# Samsung Galaxy A3 (2017), SM-A320F
#

$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)
$(call inherit-product, device/samsung/a3y17lte/device.mk)
$(call inherit-product, vendor/lineage/config/common_full_phone.mk)

PRODUCT_NAME := lineage_a3y17lte
PRODUCT_DEVICE := a3y17lte
PRODUCT_BRAND := samsung
PRODUCT_MODEL := SM-A320F
PRODUCT_MANUFACTURER := samsung

PRODUCT_GMS_CLIENTID_BASE := android-samsung
