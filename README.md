# device_samsung_a3y17lte

LineageOS 24.0 (Android 17) device tree for the Samsung Galaxy A3 (2017), SM-A320F (Exynos 7870),
on a mainline-based kernel ([kernel_samsung_exynos7870](https://github.com/a3y17lte-mainline/kernel_samsung_exynos7870),
android17-6.18) with open-source HALs only: Mesa Panfrost, minigbm, drm_hwcomposer.

**Preliminary**: written before the first build and the first boot.

- Boot: S-Boot loads `boot.img` directly (header v0, raw Image, DTBH dt.img, bootconfig
  appended to the ramdisk): see `bootimg.mk`, `tools/mkbootimg_samsung.py`, `bootconfig.txt`.
- Storage, no repartitioning: super on the PIT partition SYSTEM (4000 MiB: system, system_ext,
  product, vendor as EROFS), /cache on CACHE, /metadata on HIDDEN, /data (f2fs, FBE) on USERDATA.
- Kernel: `prebuilt/` (not in git) holds Image, dt.img and modules from the kernel repo's build.
- Firmware: `./extract-files.sh FIRMWARE_DIR` fills vendor/samsung/a3y17lte (4 files, sha256-pinned).
