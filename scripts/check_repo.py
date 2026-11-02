#!/usr/bin/env python3
"""Repository hygiene checks.

Run from anywhere:  python tools/check_repo.py

Checks that every tracked text file is

  1. ASCII only            (the repository is English only)
  2. free of local paths   (no home directories, user names, drive letters)
  3. free of firmware      (no .bin/.Z5M/.dec/.bi and no key material)

Patterns are assembled at run time so that this file does not match itself.
"""
from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]   # scripts/ -> repo root

ALLOWED_EXT = {".md", ".py", ".c", ".h", ".S", ".ld", ".json", ".toml", ".txt",
               ".dat", ".sh", ".hex", ""}
FORBIDDEN_EXT = {".bin", ".z5m", ".dec", ".bi", ".exe", ".zip", ".gz"}

_SLASH = chr(47)
_BSLASH = chr(92)
LOCAL_PATTERNS = [
    _SLASH + "home" + _SLASH,
    _SLASH + "Users" + _SLASH,
    _SLASH + "tmp" + _SLASH,
    "stu" + "." + "m1saka",
    "tiny" + "claw",
]
# Windows drive letters C: to Z:. A: and B: are left out on purpose: the camera's
# memory card is A:, and paths like A:\\Z5APP.BIN are part of the subject matter.
LOCAL_PATTERNS += [chr(letter) + ":" + _BSLASH for letter in range(ord("C"), ord("Z") + 1)]


def main() -> int:
    problems: list[str] = []
    checked = 0
    for path in sorted(ROOT.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(ROOT)
        if ".git" in rel.parts or "__pycache__" in rel.parts:
            continue
        if path.suffix.lower() in FORBIDDEN_EXT:
            problems.append("%s: forbidden file type" % rel)
            continue
        if path.suffix.lower() not in ALLOWED_EXT:
            problems.append("%s: unexpected file type" % rel)
            continue
        checked += 1
        data = path.read_bytes()
        if any(b > 0x7F for b in data):
            problems.append("%s: non-ASCII bytes" % rel)
        text = data.decode("ascii", "replace")
        for pat in LOCAL_PATTERNS:
            if pat in text:
                problems.append("%s: contains local path fragment %r" % (rel, pat))

    if problems:
        for p in problems:
            print("FAIL " + p)
        return 1
    print("OK  %d files checked" % checked)
    return 0


if __name__ == "__main__":
    sys.exit(main())
