#!/usr/bin/env python3
"""Build this example into a .Z5M module.

    python sdk/examples/hello/build.py [-o out.Z5M]
"""
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
SDK = HERE.parents[1]
out = (pathlib.Path(sys.argv[sys.argv.index('-o') + 1]) if '-o' in sys.argv
       else SDK / 'build' / 'hello.Z5M')

subprocess.run([sys.executable, str(SDK / 'tools' / 'z5build.py'),
                str(HERE / 'main.c'), '-o', str(out)], check=True)
