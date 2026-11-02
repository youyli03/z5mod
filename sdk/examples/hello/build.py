#!/usr/bin/env python3
"""Build this example into the file the camera loads.

    python sdk/examples/hello/build.py [-o out.bin]

The output is examples/hello/build/Z5APP.BIN: copy it to the card root and the
patched firmware runs it on the next redraw. The name matters, not the extension
- the loader opens A:\\Z5APP.BIN.
"""
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
SDK = HERE.parents[1]

out = HERE / 'build' / 'Z5APP.BIN'
if '-o' in sys.argv:
    out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])

out.parent.mkdir(parents=True, exist_ok=True)
subprocess.run([sys.executable, str(SDK / 'tools' / 'z5build.py'),
                str(HERE / 'main.c'), '-o', str(out)], check=True)
print('  copy %s to the card root' % out)
