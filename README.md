# z5mod

Experimental **SD-card module SDK** and **firmware patcher** for the Nikon Z 5
(firmware C 1.50).

Not affiliated with, endorsed by, or supported by Nikon. This is experimental
firmware tooling: a mistake can corrupt a card or leave the camera unusable.
Read [NOTICE.md](NOTICE.md) before building anything.

## What this repository contains - and what it does not

| Contains | Does not contain |
| --- | --- |
| Python patcher source | Nikon firmware, in any form |
| C module SDK source | firmware keys or derived dumps |
| SHA-256 manifests of accepted inputs and expected outputs | repackaged update files |

You obtain the stock firmware yourself from Nikon and run the patcher against
your own copy. Every stage is verified against `firmware/known/`; nothing is
downloaded and nothing touches a camera or a card automatically.

## Quick start

```sh
# 1. patched firmware from your own stock package
python firmware/patch.py build Z_5_0150.bin --out EG165050.BIN
#    sha256 must match firmware/known/1.5.0.json
#    copy it to the card's firmware folder and use the camera's update menu

# 2. a module
python sdk/examples/hello/build.py
#    copy examples/hello/build/Z5APP.BIN to the card root
```

Requires Python 3.10+ for the patcher and `arm-none-eabi-gcc` for the SDK.

## Using it on the camera

1. **Card in slot 1.** The loader reads `A:\`, which is that slot.
2. **Open MY MENU.** The module is attached to a row there. Nothing runs until
   that row is drawn: the patched camera calls the module from the row's draw
   callback, so entering MY MENU is what activates it.
3. **Cold boot after swapping files.** Power the camera off and take the battery
   out for a moment; then fit the card and start again. A warm restart can keep
   the previous module image cached, which makes a swapped file look inert.

## Why this is hard to brick

Two independent reasons, and they apply to the two different files:

* **A module cannot brick the camera.** The loader validates the container, the
  size and the CRC before running anything, and every failure path frees what it
  allocated and calls the original callback. A missing, truncated or corrupt
  `Z5APP.BIN` simply draws the row as if nothing were there - the menu keeps
  working.
* **The firmware image is reproducible and hash-checked.** It is built from your
  own stock package by a patcher that refuses to run unless the input matches the
  recorded SHA-256 byte for byte, and the output is compared with the hash of the
  build that was tested. What gets flashed is the firmware's own update path, the
  same one Nikon uses, so a normal failed update can still be retried from the
  card.

Neither is a guarantee: an interrupted firmware write is still an interrupted
firmware write. Keep a card with the stock firmware at hand.

## Two layers

```
firmware/    stock firmware + our patch   -> the camera can load modules from SD
sdk/         your C module                -> Z5APP.BIN on the card
```

The module container is called `.Z5M` while it is a file in this repository; on
the card it is `Z5APP.BIN`, which is the name the loader opens. The extension the
camera cares about is the container magic, not the file name.

The patched camera re-reads the module from the card on every menu redraw, runs
it, and frees the image afterwards. Modules are stateless between redraws
unless they keep state in the firmware heap; see `docs/`.

## Repository layout

| Path | Contents |
| --- | --- |
| `firmware/` | patcher that turns a stock firmware package into the module host build, plus its hash manifests (`firmware/known/`) |
| `sdk/` | C SDK used to build the modules, with a tested example |
| `docs/` | design notes, ABI notes, per-feature documentation |

## Status

| Item | State |
| --- | --- |
| Target | Nikon Z 5, firmware **C 1.50** only |
| Firmware patch (SD module host) | working on hardware |
| Module SDK (alloc / task / hook / audio) | working on hardware |
| Audio playback state machine | working on hardware |
| Silent-photography sound | **not solved** (the firmware never issues the notification there) |
| Burst (per-frame) trigger | working: one notification per frame |

`sdk/` and `firmware/` are still being written; the layout above is the target
shape and is documented in `firmware/README.md` and `docs/README.md`.

## Safety

* The patcher never writes to a card, never talks to a camera, and refuses to
  overwrite existing output files.
* Only an exact known stock input is accepted (size + SHA-256).
* Every patch declares the bytes it expects; a mismatch aborts the build.
* Offline verification is not a hardware safety guarantee.

## Licence

MIT for our own code - see [LICENSE](LICENSE). Third-party components keep their
own licences; see [NOTICE.md](NOTICE.md).
