"""Command line for the Z 5 patcher: read only, never writes to hardware."""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

from . import container
from .platform import z5_150 as z5


def cmd_verify(args) -> int:
    plain, mods = container.load(Path(args.package))
    print("%s %s: %s decoded (%d byte body)" % (z5.CAMERA, z5.FIRMWARE,
                                               args.package, len(plain)))
    print("%d module(s):" % len(mods))
    for mod in mods:
        mark = " <- main" if mod.name == z5.MODULE_MAIN else ""
        print("  %-18s off 0x%08X  %10d bytes  sha256 %s%s"
              % (mod.name, mod.offset, mod.length, mod.sha256[:16], mark))
    main = container.main_module(mods)
    print("main module verified: %s (%d bytes)" % (main.name, main.length))
    return 0


def cmd_extract(args) -> int:
    _, mods = container.load(Path(args.package))
    wanted = args.name or z5.MODULE_MAIN
    for mod in mods:
        if mod.name != wanted:
            continue
        if mod.name == z5.MODULE_MAIN:
            container.main_module(mods)
        if args.out.exists():
            raise SystemExit("%s exists; refusing to overwrite" % args.out)
        args.out.write_bytes(mod.data)
        print("wrote %s (%d bytes, sha256 %s)" % (args.out, mod.length, mod.sha256))
        return 0
    raise SystemExit("%s is not in this package" % wanted)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="z5mod-fw", description=__doc__)
    sub = ap.add_subparsers(dest="command", required=True)

    p = sub.add_parser("verify", help="decode a stock package and check every checksum")
    p.add_argument("package", type=Path)
    p.set_defaults(func=cmd_verify)

    p = sub.add_parser("extract", help="write one module out of the package")
    p.add_argument("package", type=Path)
    p.add_argument("--name", default=None, help="module to extract (default: the main one)")
    p.add_argument("--out", type=Path, required=True)
    p.set_defaults(func=cmd_extract)

    args = ap.parse_args(argv)
    try:
        return args.func(args)
    except container.FormatError as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 4
