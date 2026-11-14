#!/usr/bin/python3 -I
"""mkbootimg-samsung: pack a Samsung Exynos (S-Boot) Android boot image, header v0 + DT table.

Samsung's S-Boot reads the classic AOSP v0 header, with the dt.img (DTBH/QCDT table,
from dtbTool-exynos) size stored in the slot AOSP later reused for header_version.

  mkbootimg_samsung.py --kernel Image --ramdisk ramdisk.img --dt dt.img \
      --bootconfig bootconfig.txt --seandroid -o boot.img
  mkbootimg_samsung.py --unpack BOOT.img --outdir dir/     # split an existing image

S-Boot ignores the load addresses; the defaults match stock A3 2017 images.
S-Boot replaces the cmdline, so parameters go into a bootconfig appended to the ramdisk
(kernel CONFIG_BOOT_CONFIG_FORCE): S-Boot passes the whole ramdisk as the initrd.
Copied from the a3y17lte tools repo (tools/mkbootimg-samsung), plus --bootconfig/--seandroid.
"""
import argparse, hashlib, os, struct, sys

HDR = struct.Struct("<8s10I16s512s32s1024s")   # magic .. extra_cmdline (v0 layout)
BOOTCONFIG_MAGIC = b"#BOOTCONFIG\n"


SBOOT_MAX_RAMDISK = 0xe00000
SBOOT_MAX_TOTAL = 0x3d7ff00

def pad(b, page):
    return b + b"\0" * ((-len(b)) % page)


def bootconfig(path):
    """Kernel bootconfig footer, as tools/bootconfig -a writes it."""
    lines = [l.split("#", 1)[0].strip() for l in open(path).read().splitlines()]
    data = "\n".join(l for l in lines if l).encode() + b"\n\0"
    total = len(data) + 8 + len(BOOTCONFIG_MAGIC)
    data += b"\0" * ((-total) % 4)
    return data + struct.pack("<II", len(data), sum(data) & 0xffffffff) + BOOTCONFIG_MAGIC


DTBH_PLATFORM = 0x50a6       # platform/subtype codes of the stock A320F dt.img
DTBH_SUBTYPE = 0x217584da
DTB_FREE_SPACE = 0x8000      # S-Boot edits /chosen and /memory in place: leave it room


def fdt_root_u32(dtb, name):
    """Read a one-cell property of the root node from a flattened device tree."""
    magic, total, off_struct, off_strings = struct.unpack_from(">IIII", dtb, 0)
    if magic != 0xd00dfeed:
        sys.exit("not a DTB")
    pos, depth = off_struct, 0
    while True:
        tok, = struct.unpack_from(">I", dtb, pos)
        pos += 4
        if tok == 1:                                   # BEGIN_NODE
            end = dtb.index(b"\0", pos)
            pos = (end + 4) & ~3
            depth += 1
        elif tok == 2:                                 # END_NODE
            depth -= 1
        elif tok == 3:                                 # PROP
            length, nameoff = struct.unpack_from(">II", dtb, pos)
            pos += 8
            pname = dtb[off_strings + nameoff:dtb.index(b"\0", off_strings + nameoff)]
            if depth == 1 and pname == name.encode():
                if length != 4:
                    sys.exit(f"{name}: expected one cell")
                return struct.unpack_from(">I", dtb, pos)[0]
            pos = (pos + length + 3) & ~3
        elif tok == 4:                                 # NOP
            continue
        else:                                          # END
            sys.exit(f"DTB has no root property {name} (needed by S-Boot)")


def fdt_add_free_space(dtb, extra):
    """Grow totalsize: libfdt (S-Boot) uses the space after the strings block."""
    total, = struct.unpack_from(">I", dtb, 4)
    off_strings, = struct.unpack_from(">I", dtb, 12)
    size_strings, = struct.unpack_from(">I", dtb, 32)
    if off_strings + size_strings != total or len(dtb) != total:
        sys.exit("DTB layout: strings block is not last")
    d = bytearray(dtb) + b"\0" * extra
    struct.pack_into(">I", d, 4, total + extra)
    return bytes(d)


def dtbh(dtbs, page):
    """Samsung DTBH table, as LineageOS's old dtbhtool (hardware/samsung, <= 18.1) wrote it:
    'DTBH', version 2, count; per DTB chip/platform/subtype/hw_rev/hw_rev_end/offset/size/0x20;
    header padded to a page, each DTB page-aligned."""
    blobs = []
    for path in dtbs:
        d = fdt_add_free_space(open(path, "rb").read(), DTB_FREE_SPACE)
        blobs.append((fdt_root_u32(d, "model_info-chip"), fdt_root_u32(d, "model_info-hw_rev"),
                      fdt_root_u32(d, "model_info-hw_rev_end"), d))
    blobs.sort(key=lambda b: (b[0], b[1]))
    hdr_len = 12 + 32 * len(blobs)
    offset = hdr_len + page - (hdr_len % page)
    hdr = struct.pack("<4sII", b"DTBH", 2, len(blobs))
    body = b""
    for chip, rev, rev_end, d in blobs:
        size = len(pad(d, page))
        hdr += struct.pack("<8I", chip, DTBH_PLATFORM, DTBH_SUBTYPE, rev, rev_end, offset, size, 0x20)
        body += pad(d, page)
        offset += size
    return hdr + b"\0" * (hdr_len + page - (hdr_len % page) - len(hdr)) + body


def sboot_image_header(kernel):
    """arm64 Image header in the layout S-Boot is known to boot: text_offset 0x80000,
    flags 0 (mainline writes 0 and LE/4K/anywhere flags). The kernel is relocatable and
    does not read these fields itself."""
    k = bytearray(kernel)
    if k[0x38:0x3c] != b"ARMd":
        sys.exit("kernel is not an arm64 Image")
    struct.pack_into("<Q", k, 0x08, 0x80000)
    struct.pack_into("<Q", k, 0x18, 0)
    return bytes(k)


def pack(a):
    kernel = open(a.kernel, "rb").read()
    if a.sboot_image_header:
        kernel = sboot_image_header(kernel)
    ramdisk = open(a.ramdisk, "rb").read() if a.ramdisk else b""
    if a.bootconfig:
        ramdisk += bootconfig(a.bootconfig)
    second = open(a.second, "rb").read() if a.second else b""
    dt = open(a.dt, "rb").read() if a.dt else b""
    if a.dtb:
        dt = dtbh(a.dtb, a.pagesize)
    # S-Boot's checks before it boots an image (from its code, A320FLXXS9CTK2), on the
    # page-aligned sizes: ramdisk <= 14 MiB ("Invalid RAMDISK LENGTH"), and
    # kernel + ramdisk + dt + 0x1000 <= 0x3d7ff00 ("Invalid KERNEL LENGTH" / "DT LENGTH").
    if len(pad(ramdisk, a.pagesize)) > SBOOT_MAX_RAMDISK:
        sys.exit(f"ramdisk is {len(ramdisk)} bytes: S-Boot refuses more than "
                 f"{SBOOT_MAX_RAMDISK} (use a stronger compression)")
    if sum(len(pad(x, a.pagesize)) for x in (kernel, ramdisk, dt)) + 0x1000 > SBOOT_MAX_TOTAL:
        sys.exit(f"kernel + ramdisk + dt exceed S-Boot's limit of {SBOOT_MAX_TOTAL} bytes")
    cmdline = a.cmdline.encode()
    if len(cmdline) > 512 + 1024 - 2:
        sys.exit("cmdline too long")
    main, extra = cmdline[:511], cmdline[511:]

    sha = hashlib.sha1()
    for blob in (kernel, ramdisk, second, dt):
        sha.update(blob)
        sha.update(struct.pack("<I", len(blob)))
    digest = sha.digest() + b"\0" * 12

    base = a.base
    hdr = HDR.pack(b"ANDROID!",
                   len(kernel), base + a.kernel_offset,
                   len(ramdisk), base + a.ramdisk_offset,
                   len(second), base + a.second_offset,
                   base + a.tags_offset, a.pagesize, len(dt), a.os_version,
                   a.board.encode()[:15], main, digest, extra)
    out = pad(hdr, a.pagesize) + pad(kernel, a.pagesize) + pad(ramdisk, a.pagesize)
    out += pad(second, a.pagesize) + pad(dt, a.pagesize)
    if a.seandroid:
        out += b"SEANDROIDENFORCE"
    with open(a.output, "wb") as f:
        f.write(out)
    print(f"{a.output}: {len(out)} bytes (kernel {len(kernel)}, ramdisk {len(ramdisk)}, dt {len(dt)})")


def unpack(path, outdir):
    d = open(path, "rb").read()
    f = HDR.unpack_from(d)
    if f[0] != b"ANDROID!":
        sys.exit("not an Android boot image")
    ks, ka, rs, ra, ss, sa, ta, ps, dts, osv = f[1:11]
    name, cmd, ident, extra = f[11:]
    os.makedirs(outdir, exist_ok=True)
    pg = lambda n: (n + ps - 1) // ps * ps
    off = ps
    for nm, size in (("kernel", ks), ("ramdisk", rs), ("second", ss), ("dt", dts)):
        if size:
            open(os.path.join(outdir, nm), "wb").write(d[off:off + size])
        off += pg(size)
    info = {"kernel_addr": hex(ka), "ramdisk_addr": hex(ra), "second_addr": hex(sa),
            "tags_addr": hex(ta), "pagesize": ps, "os_version": osv,
            "board": name.rstrip(b"\0").decode(errors="replace"),
            "cmdline": (cmd.rstrip(b"\0") + extra.rstrip(b"\0")).decode(errors="replace"),
            "id": ident.hex(), "image_bytes": off}
    for k, v in info.items():
        print(f"{k}: {v}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--unpack")
    ap.add_argument("--outdir")
    ap.add_argument("--kernel")
    ap.add_argument("--ramdisk")
    ap.add_argument("--second")
    ap.add_argument("--dt", help="a ready DTBH dt.img")
    ap.add_argument("--dtb", action="append", help="DTB(s) to pack into a DTBH table (instead of --dt)")
    ap.add_argument("--dump-dtbh", help="print the DTBH table of a dt.img and exit")
    ap.add_argument("--bootconfig", help="key = value file appended to the ramdisk")
    ap.add_argument("--seandroid", action="store_true", help="append the SEANDROIDENFORCE trailer")
    ap.add_argument("--sboot-image-header", action="store_true",
                    help="set the arm64 Image header to text_offset 0x80000, flags 0")
    ap.add_argument("--cmdline", default="")
    ap.add_argument("--board", default="")
    ap.add_argument("--base", type=lambda x: int(x, 0), default=0x10000000)
    ap.add_argument("--kernel_offset", type=lambda x: int(x, 0), default=0x00008000)
    ap.add_argument("--ramdisk_offset", type=lambda x: int(x, 0), default=0x01000000)
    ap.add_argument("--second_offset", type=lambda x: int(x, 0), default=0x00f00000)
    ap.add_argument("--tags_offset", type=lambda x: int(x, 0), default=0x00000100)
    ap.add_argument("--os_version", type=lambda x: int(x, 0), default=0)
    ap.add_argument("--pagesize", type=int, default=2048)
    ap.add_argument("-o", "--output")
    a = ap.parse_args()
    if a.dump_dtbh:
        d = open(a.dump_dtbh, "rb").read()
        magic, ver, n = struct.unpack_from("<4sII", d, 0)
        print(magic, ver, n, len(d))
        for i in range(n):
            print(" ".join("%#x" % x for x in struct.unpack_from("<8I", d, 12 + 32 * i)))
        return
    if a.unpack:
        unpack(a.unpack, a.outdir or "unpacked")
    else:
        if not (a.kernel and a.output):
            ap.error("--kernel and -o are required")
        pack(a)


if __name__ == "__main__":
    main()
