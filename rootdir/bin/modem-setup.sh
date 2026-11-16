#!/vendor/bin/sh
# Runs on every boot before the radio HAL (init: exec_start in post-fs-data), in two parts:
# "nv" as radio, "firmware" as root (each owns what it reads and writes; no DAC overrides).
#
# The kernel starts the modem itself when the radio HAL opens the IPC port: it
# loads the firmware image and the NV data through the links in
# /vendor/firmware/samsung/a3y17lte (the DT's firmware-name) to the files here.
#
# - On the first boot it copies the modem's NV data files from EFS (mounted
#   read-only with noload by init: not a block of EFS is written) to the
#   working copy in /data/vendor/modem/nv, which the modem then reads and
#   writes through the HAL's RFS service. EFS itself is never written.
# - On the first boot it copies the firmware image from the RADIO partition
#   (a read; 37 MiB covers the table of contents, the loader and the firmware).
# - The NV link goes to the working copy itself, so every modem start loads the
#   current NV data.
#
# The working copy holds the IMEI and the radio calibration. Delete
# /data/vendor/modem/nv to start again from the files on EFS.

NV=/data/vendor/modem/nv
FW=/data/vendor/modem/firmware/samsung/a3y17lte
EFS=/mnt/vendor/efs

say() {
    log -p i -t modem-setup "$*" 2>/dev/null
    return 0
}

case "$1" in
nv)
    # As radio, which owns the files on EFS and the working copy
    [ -e "$NV/nv_data.bin" ] && exit 0
    if [ ! -e "$EFS/nv_data.bin" ]; then
        say "no NV data on EFS"
        exit 1
    fi
    for f in nv_data.bin nv_data.bin.md5 .nv_data.bak .nv_data.bak.md5; do
        cp "$EFS/$f" "$NV/$f.tmp" || exit 1
        chmod 0600 "$NV/$f.tmp"
    done
    # nv_data.bin last: its presence means the copy is complete
    for f in nv_data.bin.md5 .nv_data.bak .nv_data.bak.md5 nv_data.bin; do
        mv "$NV/$f.tmp" "$NV/$f" || exit 1
    done
    say "NV working copy created"
    ;;
firmware)
    # As root, which owns the RADIO partition and the firmware directory
    if [ ! -s "$FW/modem.bin" ]; then
        dd if=/dev/block/by-name/RADIO of="$FW/modem.bin.tmp" bs=1048576 count=37 \
            2>/dev/null || exit 1
        chmod 0640 "$FW/modem.bin.tmp"
        mv "$FW/modem.bin.tmp" "$FW/modem.bin" || exit 1
        say "firmware image copied from RADIO"
    fi
    ;;
*)
    echo "usage: modem-setup.sh nv|firmware" >&2
    exit 2
    ;;
esac
exit 0
