#!/vendor/bin/sh
# Runs once per boot before the radio HAL (init: exec_start in post-fs-data).
#
# The kernel starts the modem itself when the radio HAL opens the IPC port: it
# loads the firmware image and the NV data from /data/vendor/modem/firmware
# (kernel.firmware_class.path in bootconfig.txt; the DT's firmware-name).
#
# - On the first boot it copies the modem's NV data files from EFS (mounted
#   read-only with noload by init: not a block of EFS is written) to the
#   working copy in /data/vendor/modem/nv, which the modem then reads and
#   writes through the HAL's RFS service. EFS itself is never written.
# - On the first boot it copies the firmware image from the RADIO partition
#   (a read; 37 MiB covers the table of contents, the loader and the firmware).
# - It links modem-nv.bin to the working copy, so every modem start loads the
#   current NV data.
#
# The working copy holds the IMEI and the radio calibration. Delete
# /data/vendor/modem/nv to start again from the files on EFS.

NV=/data/vendor/modem/nv
FW=/data/vendor/modem/firmware/samsung/a3y17lte
EFS=/mnt/vendor/efs

log() {
    echo "modem-setup: $*" > /dev/kmsg
}

if [ ! -e "$NV/nv_data.bin" ]; then
    if [ ! -e "$EFS/nv_data.bin" ]; then
        log "no NV data on EFS"
        exit 1
    fi
    for f in nv_data.bin nv_data.bin.md5 .nv_data.bak .nv_data.bak.md5; do
        cp "$EFS/$f" "$NV/$f.tmp" || exit 1
    done
    for f in nv_data.bin.md5 .nv_data.bak .nv_data.bak.md5 nv_data.bin; do
        mv "$NV/$f.tmp" "$NV/$f"
    done
    chown radio:radio "$NV"/*
    chmod 0600 "$NV"/*
    log "NV working copy created"
fi

if [ ! -s "$FW/modem.bin" ]; then
    dd if=/dev/block/by-name/RADIO of="$FW/modem.bin.tmp" bs=1048576 count=37 2>/dev/null || exit 1
    mv "$FW/modem.bin.tmp" "$FW/modem.bin"
    chmod 0640 "$FW/modem.bin"
    log "firmware image copied from RADIO"
fi

ln -sf "$NV/nv_data.bin" "$FW/modem-nv.bin"
exit 0
