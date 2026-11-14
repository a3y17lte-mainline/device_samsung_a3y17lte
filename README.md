# device_samsung_a3y17lte

LineageOS 24.0 (Android 17) device tree for the Samsung Galaxy A3 (2017), SM-A320F (Exynos 7870),
on a mainline-based kernel ([kernel_samsung_exynos7870](https://github.com/a3y17lte-mainline/kernel_samsung_exynos7870),
android17-6.18) with open-source HALs only: our Mesa Panfrost (external_mesa), minigbm with an Exynos DRM backend
(external_minigbm), drm_hwcomposer, and our own vibrator and thermal HALs (`aidl/`).

**Preliminary**: written before the first build and the first boot.

- Boot: S-Boot loads `boot.img` directly (header v0, raw Image, DTBH dt.img, bootconfig
  appended to the ramdisk, Image header as S-Boot expects): see `bootimg.mk`, `tools/mkbootimg_samsung.py`, `bootconfig.txt`.
- Storage, no repartitioning: super on the PIT partition SYSTEM (4000 MiB: system, system_ext,
  product, vendor as EROFS), /cache on CACHE, /metadata on HIDDEN, /data (f2fs, FBE) on USERDATA.
- Kernel: built from source by the LineageOS build (`TARGET_KERNEL_SOURCE` kernel/samsung/exynos7870,
  `a3y17lte_defconfig`); the DTBH dt.img is built from its DTB by
  `tools/mkbootimg_samsung.py` (format of LineageOS's old dtbhtool, codes of the stock dt.img).
  Modules load by modalias (ueventd).
- Firmware: `./extract-files.sh FIRMWARE_DIR` fills vendor/samsung/a3y17lte (4 files, sha256-pinned).
- Graphics: `external/mesa` (a3y17lte-mainline/external_mesa: the Mesa release with our Midgard
  fixes, built by its meson-based Android.mk; Panfrost only) and `external/minigbm`
  (a3y17lte-mainline/external_minigbm replaces AOSP's: dumb buffers on the Exynos DRM node).
  Both come from android-host-arm64's local manifest, whose setup.sh also builds Mesa's host
  tools (mesa_clc, panfrost_compile).
- `aidl/vibrator`: IVibrator V4 on the regulator-haptic input device (FF_RUMBLE, on/off only:
  the motor runs at one voltage). Effects are fixed lengths: TICK 10, CLICK 20, HEAVY_CLICK 30 ms,
  DOUBLE_CLICK 20 + 100 gap + 20 ms.
- `aidl/thermal`: IThermal V3 over the kernel thermal zones (found by type). soc-thermal is
  reported as CPU, GPU and the SKIN sensor ThermalManagerService uses; battery-thermal as BATTERY.
  The kernel throttles by itself (soc-thermal trips); the HAL reports and notifies (5 s polling,
  1 s once a sensor is above NONE).
