# firmware/

Turns a stock Nikon Z 5 **C 1.50** package into a build that loads `.Z5M`
modules from the SD card.

```
firmware/
  patch.py                      entry point, no install needed
  src/z5mod_fw/
    cli.py                      verify / extract / build
    container.py                XOR decode, directory parse, CRC checks
    features/stage0.py          the SD module host patch
    platform/z5_150.py          every number this target depends on
    platform/z5_150_mask.hex    the XOR mask, see NOTICE.md
  known/1.5.0.json              input hashes, mask hash, module hashes, output hash
```

## Commands

```sh
python firmware/patch.py verify  Z_5_0150.bin          # decode, check every checksum
python firmware/patch.py extract Z_5_0150.bin --out main.bi
python firmware/patch.py build   Z_5_0150.bin --out EG165050.BIN
```

`build` writes the file the camera accepts. Outputs are never overwritten.

## What is checked

* the package size and SHA-256 must match `known/1.5.0.json` exactly;
* the decoded space label, every module CRC and the package CRC must hold;
* the main module must match its recorded SHA-256;
* the patch must find the exact bytes it replaces - otherwise the build aborts;
* the output is resealed so its CRC residue is zero.

A different firmware version needs a new platform module; nothing here patches
blindly.
