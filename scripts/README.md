# scripts/

Standalone utilities. Nothing here is part of a build contract: the firmware
patcher and the module SDK must both work without these scripts.

| Script | Purpose |
| --- | --- |
| `check_repo.py` | repository hygiene: ASCII only, no local paths, no firmware or build files |
| `wav2z5.py` | convert any WAV to the playback format (48 kHz stereo 16-bit), or emit a C array |

Run them from the repository root:

```sh
python scripts/check_repo.py
python scripts/wav2z5.py clip.wav -o build/SND.WAV --seconds 0.9 --trim --normalize
```

`wav2z5.py` needs `numpy`; `check_repo.py` uses the standard library only.

## Run the checker on every commit

Git does not install hooks automatically - cloning a repository must never run
code from it. Enable the hook once per clone:

```sh
git config core.hooksPath .githooks
```

From then on `.githooks/pre-commit` runs `check_repo.py` before every commit and
blocks the commit when a file is not ASCII, mentions a local path, or carries a
firmware or build-product extension.
