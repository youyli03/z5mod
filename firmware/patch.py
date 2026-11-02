"""Run the patcher from a source checkout, without installing anything."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "src"))

from z5mod_fw.cli import main

if __name__ == "__main__":
    raise SystemExit(main())
