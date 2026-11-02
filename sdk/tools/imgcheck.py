#!/usr/bin/env python3
"""z5sdk image checker: prove that a Z5_IMAGE() function is copyable.

An image is copied to alloc() memory and executed there, with no PIC base in
r9 and without the rest of the module. It is therefore only safe when

  * every branch target stays inside the image,
  * no instruction touches r9 (the module's PIC base),
  * every PC-relative literal load stays inside the image,
  * nothing calls back into the module.

Images are found through their symbol pairs: <name> and <name>_end, which
Z5_IMAGE()/Z5_IMAGE_END() put in the same section (Round 563).

Usage:  imgcheck.py <module.elf> [-d]
Exit code 1 on any violation.
"""
import re
import subprocess
import sys

try:
    import capstone
except ImportError:
    sys.exit('this tool needs capstone: python -m pip install capstone')

OBJDUMP = 'arm-none-eabi-objdump'
OBJCOPY = 'arm-none-eabi-objcopy'
READELF = 'arm-none-eabi-readelf'


def symbols(elf):
    """Value -> name. readelf keeps bit0 set for Thumb functions (nm does
    not), which is how the instruction set of an image is detected."""
    out = subprocess.run([READELF, '-sW', elf],
                         capture_output=True, text=True).stdout
    syms = {}
    for line in out.splitlines():
        m = re.match(r'\s*\d+:\s+([0-9a-f]+)\s+\d+\s+(\w+)\s+\w+\s+\w+\s+\w+\s+(\S+)', line)
        if m:
            syms[m.group(3)] = int(m.group(1), 16)
    return syms


def text_blob(elf, tmp=None):
    out = subprocess.run([OBJDUMP, '-h', elf], capture_output=True, text=True).stdout
    addr = None
    for line in out.splitlines():
        m = re.match(r'^\s*\d+\s+\.text\s+([0-9a-f]+)\s+([0-9a-f]+)', line)
        if m:
            addr = int(m.group(2), 16)
    subprocess.run([OBJCOPY, '-O', 'binary', '--only-section=.text', elf, tmp],
                   check=True, capture_output=True)
    return addr, open(tmp, 'rb').read()


def images(syms, base, blob):
    res = []
    for name, addr in sorted(syms.items()):
        if not name.endswith('_end'):
            continue
        start = name[:-4]
        if start not in syms:
            continue
        lo, hi = syms[start] & ~1, addr & ~1
        if hi <= lo:
            continue
        off = lo - base
        # bit0 of the symbol value tells the instruction set
        thumb = bool(syms[start] & 1)
        res.append((start, syms[start], blob[off:off + (hi - lo)], thumb))
    return res


def check(name, thumb_addr, code, thumb=True, dump=False):
    mode = capstone.CS_MODE_THUMB if thumb else capstone.CS_MODE_ARM
    md = capstone.Cs(capstone.CS_ARCH_ARM, mode | capstone.CS_MODE_LITTLE_ENDIAN)
    lo, hi = 0, len(code)
    errs = []
    lines = []
    literals = set()          # addresses of ldr-literal words: real data, not code
    for ins in md.disasm(code, lo):
        if ins.address in literals:
            continue          # literal pool: decoding it as code gives false hits
        tag = ''
        ops = ins.op_str
        if ins.mnemonic in ('bl', 'b', 'b.w', 'bl.w'):
            m = re.match(r'^(0x[0-9a-f]+)', ops)
            if m:
                tgt = int(m.group(1), 16)
                if not (lo <= tgt < hi):
                    tag = '  <-- branch leaves the image'
        if re.search(r'\br9\b', ops) or re.search(r'\bsb\b', ops):
            tag = '  <-- touches the PIC base r9'
        if 'pc' in ops and ins.mnemonic.startswith('ldr'):
            m = re.search(r'#(-?0x[0-9a-f]+)', ops)
            if m:
                if thumb:
                    tgt = ((ins.address + 4) & ~3) + int(m.group(1), 16)
                else:
                    tgt = ((ins.address + 8) & ~3) + int(m.group(1), 16)
                if lo <= tgt < hi:
                    literals.add(tgt)
                if not (lo <= tgt < hi):
                    tag = '  <-- literal 0x%X is outside the image' % tgt
        if tag:
            errs.append('+0x%03X  %-8s %s%s' % (ins.address, ins.mnemonic, ops, tag))
        lines.append('    +0x%03X  %-8s %s' % (ins.address, ins.mnemonic, ops))
    return errs, lines


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('-')]
    dump = '-d' in sys.argv
    if not args:
        sys.exit(__doc__)
    elf = args[0]
    syms = symbols(elf)
    base, blob = text_blob(elf)
    imgs = images(syms, base, blob)
    if not imgs:
        print('no images (no <name>/<name>_end symbol pairs)')
        return 0
    bad = 0
    for name, thumb_addr, code, thumb in imgs:
        errs, lines = check(name, thumb_addr, code, thumb, dump)
        print('%-16s %5d B  %s  %s' %
              (name, len(code), 'thumb' if thumb else 'arm  ',
               'OK' if not errs else 'FAIL'))
        for e in errs:
            print('    ' + e)
        if dump:
            for l in lines:
                print(l)
        bad += len(errs)
    print('---')
    print('%d image(s), %d violation(s)' % (len(imgs), bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
