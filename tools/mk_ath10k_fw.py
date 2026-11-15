#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""mk_ath10k_fw.py FW_IMAGE OTP_IMAGE OUT: wrap the stock QCA9377 Wi-Fi firmware for ath10k.

The stock ROM's qwlan30.bin (firmware) and otp30.bin are the raw "SGMT" images that the
vendor driver downloads. ath10k wants them in its "QCA-ATH10K" container
(firmware-sdio-5.bin): a magic string followed by 4-byte-aligned {id, length, data}
elements. Nothing in the images changes.

Why not linux-firmware's firmware-sdio-5.bin: this firmware reports the WOW service
(wake-on-WLAN), and the container says so in its feature bits. Without it mac80211 stops
the chip for every suspend and reboots its firmware in the resume path (5 s).

The output is deterministic: extract-files.sh pins its sha256.
"""
import struct
import sys

IE_FW_VERSION, IE_TIMESTAMP, IE_FEATURES, IE_FW_IMAGE, IE_OTP_IMAGE = 0, 1, 2, 3, 4
IE_WMI_OP_VERSION, IE_HTT_OP_VERSION = 5, 6

# enum ath10k_fw_features bits, LSB first in the first byte
FEATURE_WOWLAN_SUPPORT = 1 << 6
FEATURE_IGNORE_OTP_RESULT = 1 << 7

WMI_OP_VERSION_TLV = 4
HTT_OP_VERSION_TLV = 3

VERSION = b"qwlan30-A320FLXXS9CTK2"
TIMESTAMP = 0x589c5000  # as in linux-firmware's QCA9377 SDIO container


def ie(ie_id, data):
    return struct.pack("<II", ie_id, len(data)) + data + b"\0" * (-len(data) % 4)


def main():
    fw_path, otp_path, out_path = sys.argv[1:4]
    with open(fw_path, "rb") as f:
        fw = f.read()
    with open(otp_path, "rb") as f:
        otp = f.read()
    for name, image in (("firmware", fw), ("otp", otp)):
        if image[:4] != b"SGMT":
            sys.exit(f"{name} image is not an SGMT image")

    magic = b"QCA-ATH10K\0"
    out = magic + b"\0" * (-len(magic) % 4)
    out += ie(IE_FW_VERSION, VERSION)
    out += ie(IE_TIMESTAMP, struct.pack("<I", TIMESTAMP))
    out += ie(IE_OTP_IMAGE, otp)
    out += ie(IE_FW_IMAGE, fw)
    out += ie(IE_FEATURES, bytes([FEATURE_WOWLAN_SUPPORT | FEATURE_IGNORE_OTP_RESULT]))
    out += ie(IE_WMI_OP_VERSION, struct.pack("<I", WMI_OP_VERSION_TLV))
    out += ie(IE_HTT_OP_VERSION, struct.pack("<I", HTT_OP_VERSION_TLV))
    with open(out_path, "wb") as f:
        f.write(out)


if __name__ == "__main__":
    main()
