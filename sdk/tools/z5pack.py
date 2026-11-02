#!/usr/bin/env python3
"""z5pack - pack and inspect .Z5M containers.

Container layout (little endian, 32-byte header followed by the code):

    off  size  field
      0     4  magic      'Z5MD' (0x444D355A)
      4     2  version    currently 1
      6     2  flags      bit0: entry takes a context pointer in r0
      8     4  header_len always 32
     12     4  code_off   offset of the entry point from the start of the file
     16     4  code_len   number of code bytes
     20     4  image_len  total file length
     24     4  crc32      CRC-32 over bytes [0..24) plus the code section
     28     4  reserved   must be 0

The loader reads the 32-byte header, checks magic/version/crc32, copies the code to a RAM block in
848-byte chunks (the firmware's own read granularity), performs cache maintenance, and calls the
entry point with r0 = context pointer.
"""
import argparse, struct, sys, zlib

MAGIC = 0x444D355A
VERSION = 1
HDR = 32


def crc32_of(image_with_zero_crc: bytes) -> int:
    """CRC-32 over the whole image with the crc32 field itself set to zero.

    The on-camera loader does exactly this: it reads the file into RAM, remembers the stored CRC,
    zeroes that field in the copy, and CRCs the whole buffer. Keeping the host tool on the same
    definition means one bit of arithmetic on both sides."""
    return zlib.crc32(image_with_zero_crc) & 0xFFFFFFFF


def pack(code_path: str, out_path: str, entry_off: int = 0, flags: int = 1) -> None:
    code = open(code_path, 'rb').read()
    if not code:
        raise SystemExit('code file is empty')
    if len(code) % 4:
        code += b'\x00' * (4 - len(code) % 4)
    image_len = HDR + len(code)
    head = struct.pack('<IHHIII', MAGIC, VERSION, flags, HDR, HDR + entry_off, len(code))
    head += struct.pack('<I', image_len)
    head += struct.pack('<I', 0)                      # crc placeholder
    head += struct.pack('<I', 0)                      # reserved
    assert len(head) == HDR, len(head)
    image = bytearray(head + code)
    crc = crc32_of(bytes(image))
    struct.pack_into('<I', image, 24, crc)
    open(out_path, 'wb').write(bytes(image))
    print('  packed %s (%d bytes, code %d, entry +%d, crc 0x%08x)'
          % (out_path, image_len, len(code), entry_off, crc))


def check(path: str) -> int:
    data = open(path, 'rb').read()
    if len(data) < HDR:
        print('  too short'); return 1
    magic, ver, flags, hlen, coff, clen = struct.unpack_from('<IHHIII', data, 0)
    ilen = struct.unpack_from('<I', data, 20)[0]
    crc = struct.unpack_from('<I', data, 24)[0]
    ok = True
    if magic != MAGIC:
        print('  magic mismatch: 0x%08x' % magic); ok = False
    if ver != VERSION:
        print('  version %d (expected %d)' % (ver, VERSION)); ok = False
    if hlen != HDR:
        print('  header_len %d (expected %d)' % (hlen, HDR)); ok = False
    if ilen != len(data):
        print('  image_len field %d but file is %d bytes' % (ilen, len(data))); ok = False
    if coff < HDR or coff >= len(data):
        print('  code_off 0x%x out of range' % coff); ok = False
    if clen == 0 or HDR + clen > len(data):
        print('  code_len %d does not fit' % clen); ok = False
    zeroed = bytearray(data); zeroed[24:28] = b'\x00\x00\x00\x00'
    want = crc32_of(bytes(zeroed))
    if crc != want:
        print('  crc32 mismatch: file 0x%08x, computed 0x%08x' % (crc, want)); ok = False
    print('  %s  magic Z5MD  ver %d  flags 0x%x  entry +0x%x  code %d  image %d  crc 0x%08x'
          % ('OK ' if ok else 'BAD', ver, flags, coff - HDR, clen, ilen, crc))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description='pack and inspect .Z5M containers')
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('pack', help='wrap a raw Thumb binary into a .Z5M container')
    p.add_argument('code'); p.add_argument('out')
    p.add_argument('--entry', type=lambda s: int(s, 0), default=0, help='entry offset inside code')
    p.add_argument('--flags', type=lambda s: int(s, 0), default=1)
    c = sub.add_parser('check', help='validate a .Z5M container')
    c.add_argument('image')
    a = ap.parse_args()
    if a.cmd == 'pack':
        pack(a.code, a.out, a.entry, a.flags)
    else:
        sys.exit(check(a.image))


if __name__ == '__main__':
    main()
