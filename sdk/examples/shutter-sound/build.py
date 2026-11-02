#!/usr/bin/env python3
"""Build this example into the file the camera loads.

    python sdk/examples/shutter-sound/build.py [-o out.bin]

The output is examples/shutter-sound/build/Z5APP.BIN. It plays A:\\SND.WAV, so the
card needs that file: 48 kHz stereo 16-bit, made with scripts/wav2z5.py.
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
