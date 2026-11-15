#!/usr/bin/env python3
"""
The Galaxy S20's boot image with HakiOS's U-Boot in it (devices/x1s/u-boot), for the owner to flash to `boot` in TWRP.

S-BOOT starts the kernel of boot.img. Here that kernel is U-Boot; everything else S-BOOT reads is taken from the phone's
own stock boot image (its backup), as it was: the header's addresses and versions, the command line, and the phone's
device tree (the dtb section), which S-BOOT checks and patches before it hands it over. The device tree is Samsung's
file: it is read from the backup on this computer at packing time and never enters the repository.

  kernel   = u-boot.bin (its Linux header's text_offset set to the stock kernel's 0x80000, so S-BOOT puts it where
             it puts Linux; U-Boot is position independent)
  ramdisk  = an HKUB container with the HakiOS loader and its payload (tools/mkubootimg.py -c), or nothing
  dtb      = the stock image's, byte for byte

usage: devices/x1s/mkbootimg.py --stock E:\\s20-g981b\\backup-20261110\\boot.img -u u-boot.bin [-r x.hkub | -l loader.efi [-p payload.bin]] -o hakios-x1s-boot.img

Undo on the phone: flash the stock boot.img back in TWRP.
"""
import argparse, gzip, hashlib, os, struct, sys

PAGE = 2048
HDR_V2_SIZE = 1660
BOOT_PARTITION = 0x3B00000            # 59 MiB: the phone's boot partition (the stock image is exactly this long)


def pad(b, page=PAGE):
    return b + b'\0' * (-len(b) % page)


def empty_ramdisk():
    """a gzip'd cpio (newc) archive with nothing in it but its trailer"""
    name = b'TRAILER!!!\0'
    head = b'070701' + b''.join(b'%08X' % v for v in (0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, len(name), 0))
    cpio = head + name
    cpio += b'\0' * (-len(cpio) % 4)
    cpio += b'\0' * (-len(cpio) % 512)
    return gzip.compress(cpio, mtime=0)


def read_stock(path):
    d = open(path, 'rb').read()
    if d[:8] != b'ANDROID!':
        sys.exit('mkbootimg: %s is not an Android boot image' % path)
    (ks, ka, rs, ra, ss, sa, ta, ps, hv, osv) = struct.unpack_from('<10I', d, 8)
    if hv != 2 or ps != PAGE:
        sys.exit('mkbootimg: expected header version 2, page 2048 (got %d, %d)' % (hv, ps))
    name = d[48:64]
    cmdline = d[64:64 + 512]
    extra = d[64 + 512 + 32:64 + 512 + 32 + 1024]
    o = 64 + 512 + 32 + 1024
    rdtbo_size, rdtbo_off, hsize, dtb_size, dtb_addr = struct.unpack_from('<IQIIQ', d, o)
    off = PAGE + len(pad(b'\0' * ks)) + len(pad(b'\0' * rs)) + len(pad(b'\0' * ss)) + len(pad(b'\0' * rdtbo_size))
    dtb = d[off:off + dtb_size]
    if dtb[:4] not in (b'\xd7\xb7\xab\x1e', b'\xd0\x0d\xfe\xed'):
        sys.exit('mkbootimg: no device tree where the header says (magic %s)' % dtb[:4].hex())
    return dict(kernel_addr=ka, ramdisk_addr=ra, second_addr=sa, tags_addr=ta, os_version=osv, name=name,
                cmdline=cmdline, extra=extra, dtb=dtb, dtb_addr=dtb_addr)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--stock', required=True, help="the phone's own boot.img (the backup)")
    ap.add_argument('-u', '--u-boot', required=True, help='u-boot.bin (the x1s build)')
    ap.add_argument('-r', '--ramdisk', help='an HKUB container (tools/mkubootimg.py -c), optional')
    ap.add_argument('--logo', help='the menu logo (HKLG, private: never on GitHub), an HKUB entry "logo" with -l')
    ap.add_argument('-l', '--loader', help='loader.efi: an HKUB container with it (and -p) becomes the ramdisk')
    ap.add_argument('-p', '--payload', help='an XNUP payload (xnuloader/mkpayload.py), with -l')
    ap.add_argument('-o', '--output', default='hakios-x1s-boot.img')
    a = ap.parse_args()

    st = read_stock(a.stock)
    kernel = bytearray(open(a.u_boot, 'rb').read())
    if kernel[0x38:0x3c] != b'ARMd':
        sys.exit('mkbootimg: %s has no arm64 Linux header (CONFIG_LINUX_KERNEL_IMAGE_HEADER)' % a.u_boot)
    struct.pack_into('<Q', kernel, 8, 0x80000)            # text_offset: where S-BOOT puts the stock kernel
    kernel = bytes(kernel)
    # Without a container: an empty gzip'd cpio, the kind the stock image has, rather than no ramdisk at all
    # (S-BOOT's handling of a zero-sized ramdisk is unknown). The hakios command takes it for "nothing here".
    ramdisk = open(a.ramdisk, 'rb').read() if a.ramdisk else empty_ramdisk()
    if a.loader:
        # the same container upstream's U-Boot images carry (tools/mkubootimg.py): the hakios command takes it
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools'))
        from mkubootimg import container
        efi = open(a.loader, 'rb').read()
        if efi[:2] != b'MZ':
            sys.exit('mkbootimg: %s is not a UEFI application' % a.loader)
        entries = []
        if a.payload:
            entries.append(('payload', open(a.payload, 'rb').read()))
        entries.append(('loader', efi))
        if a.logo:
            logo = open(a.logo, 'rb').read()
            if logo[:4] != b'HKLG':
                sys.exit('mkbootimg: %s is not an HKLG logo' % a.logo)
            entries.append(('logo', logo))
        ramdisk = container(entries)
    dtb = st['dtb']

    sha = hashlib.sha1()
    for blob in (kernel, ramdisk, b'', b'', dtb):         # kernel, ramdisk, second, recovery dtbo, dtb (AOSP mkbootimg)
        sha.update(blob)
        sha.update(struct.pack('<I', len(blob)))
    hdr = struct.pack('<8s10I', b'ANDROID!', len(kernel), st['kernel_addr'], len(ramdisk), st['ramdisk_addr'],
                      0, st['second_addr'], st['tags_addr'], PAGE, 2, st['os_version'])
    hdr += st['name'] + st['cmdline'] + sha.digest().ljust(32, b'\0') + st['extra']
    hdr += struct.pack('<IQIIQ', 0, 0, HDR_V2_SIZE, len(dtb), st['dtb_addr'])
    assert len(hdr) == HDR_V2_SIZE
    img = pad(hdr) + pad(kernel) + pad(ramdisk) + pad(dtb)
    if len(img) > BOOT_PARTITION:
        sys.exit('mkbootimg: %d bytes, the boot partition holds %d' % (len(img), BOOT_PARTITION))
    open(a.output, 'wb').write(img)
    print('%s: %d bytes (U-Boot %d, ramdisk %d, stock dtb %d), sha256 %s' % (
        a.output, len(img), len(kernel), len(ramdisk), len(dtb), hashlib.sha256(img).hexdigest()))


if __name__ == '__main__':
    main()
