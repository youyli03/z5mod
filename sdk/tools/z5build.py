#!/usr/bin/env python3
"""z5build - compile C sources into a .Z5M module.

    python sdk/tools/z5build.py sdk/examples/hello/main.c -o hello.Z5M

The chain is arm-none-eabi-gcc -> ELF -> raw binary -> z5pack.

Position independence: the C side is compiled with
    -fpic -msingle-pic-base -mpic-register=r9 -mpic-data-is-text-relative
so every data access is an offset from r9, and src/start.S loads r9 from its
own PC plus a link-time constant delta. No relocation table, no runtime patch.

Requires arm-none-eabi-gcc and binutils on PATH.
"""
from __future__ import annotations

import argparse
import pathlib
import re
import struct
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
SDK = HERE.parent
SRC = SDK / 'src'
INCLUDE = SDK / 'include'
LDScript = SDK / 'z5mod.ld'

CC = 'arm-none-eabi-gcc'
OBJCOPY = 'arm-none-eabi-objcopy'
OBJDUMP = 'arm-none-eabi-objdump'
SIZE = 'arm-none-eabi-size'
PYTHON = sys.executable

CFLAGS = [
    '-mthumb', '-march=armv7-a', '-mtune=cortex-a7', '-Os',
    '-ffreestanding', '-nostdlib', '-fno-builtin', '-fno-common',
    '-fno-stack-protector', '-fno-asynchronous-unwind-tables',
    '-fpic', '-msingle-pic-base', '-mpic-register=r9',
    '-mpic-data-is-text-relative',
    '-Wall', '-Wextra', '-Wno-unused-parameter',
    '-I', str(INCLUDE),
    # keep Z5_IMAGE() functions in source order, so a Z5_IMAGE_END marker
    # always lands after the code it closes
    '-fno-toplevel-reorder', '-fno-reorder-functions',
    # drop everything the example does not reference, so a module's bytes do
    # not change when an unrelated area of the SDK grows
    '-ffunction-sections', '-fdata-sections',
]

ADDR_LOW, ADDR_HIGH = 0x10020000, 0x12200000


def run(cmd) -> str:
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write('FAILED: %s\n%s%s\n' % (' '.join(map(str, cmd)), r.stdout, r.stderr))
        raise SystemExit(1)
    return r.stdout


def flags_for(src: pathlib.Path) -> list:
    """Sources named *_arm.c are compiled as ARM: a T-Kernel service must be
    entered from ARM state, so a task that calls one in a loop has to be ARM
    code. Everything else stays Thumb."""
    if src.name.endswith('_arm.c'):
        return [('-marm' if f == '-mthumb' else f) for f in CFLAGS]
    return list(CFLAGS)


def build(main_c: pathlib.Path, out: pathlib.Path, keep: bool = False) -> None:
    main_c = main_c.resolve()
    stem = main_c.stem
    # Intermediates next to the output, not in a shared directory: two examples
    # both have a main.c, and a shared work dir let them overwrite each other.
    work = out.resolve().parent / 'obj'
    work.mkdir(parents=True, exist_ok=True)

    sources = sorted(SRC.glob('*.S')) + sorted(SRC.glob('*.c')) + [main_c]
    objs = []
    for src in sources:
        obj = work / (src.stem + '.o')
        run([CC] + flags_for(src) + ['-c', str(src), '-o', str(obj)])
        objs.append(obj)

    elf = work / (stem + '.elf')
    run([CC] + CFLAGS + ['-T', str(LDScript), '-o', str(elf)] + objs
        + ['-Wl,--gc-sections', '-Wl,-Map=' + str(work / (stem + '.map'))])

    raw = work / (stem + '.bin')
    run([OBJCOPY, '-O', 'binary', str(elf), str(raw)])
    code = raw.read_bytes()

    print('  %s (%d bytes)' % (stem, len(code)))
    for line in run([SIZE, str(elf)]).splitlines():
        print('    ' + line.strip())

    hits = []
    for off in range(0, len(code) - 4, 4):
        value = struct.unpack_from('<I', code, off)[0]
        if ADDR_LOW <= (value & ~1) < ADDR_HIGH:
            hits.append((off, value))
    print('  firmware addresses in the blob: %d' % len(hits))
    for off, value in hits[:8]:
        print('    +0x%04X  0x%08X' % (off, value))
    if len(hits) > 8:
        print('    ... %d more' % (len(hits) - 8))

    print('  entry stub:')
    for line in run([OBJDUMP, '-d', '--start-address=0', '--stop-address=0x20', str(elf)]).splitlines():
        if re.match(r'^\s+[0-9a-f]+:', line):
            print('    ' + line.strip())

    run([PYTHON, str(HERE / 'z5pack.py'), 'pack', str(raw), str(out)])
    if not keep:
        raw.unlink(missing_ok=True)
    print('  output %s (%d bytes)' % (out, out.stat().st_size))


def main() -> int:
    ap = argparse.ArgumentParser(description='build a .Z5M module from C sources')
    ap.add_argument('source', type=pathlib.Path, help='the example main.c')
    ap.add_argument('-o', '--out', type=pathlib.Path, required=True)
    ap.add_argument('--keep', action='store_true', help='keep the intermediate raw binary')
    args = ap.parse_args()
    build(args.source, args.out, args.keep)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
