# NOTICE

## No affiliation

z5mod is an independent, experimental project. It is not affiliated with,
authorised by, endorsed by, or supported by Nikon Corporation or any of its
subsidiaries. "Nikon" and "Z 5" are trademarks of their respective owners and
are used here only to describe compatibility.

## No firmware, no keys, no dumps

This repository does not contain, and will not contain:

* Nikon firmware images, in original or modified form;
* firmware decryption keys or tables derived from Nikon firmware, with one
  exception described directly below.

### The XOR mask

`firmware/src/z5mod_fw/platform/z5_150_mask.hex` holds the 768-byte XOR mask
that converts a stock package you already own into the layout the patcher
reads. It is the only derived material here: it is not a firmware image and
cannot reconstruct one on its own. It is present because a single stock
package does not contain enough known-plaintext to re-derive it, unlike some
other bodies. Without the file the patcher refuses to run; nothing else in
this repository depends on it.
* memory dumps, firmware extractions, or repackaged update files.

You must obtain the stock firmware yourself from Nikon and comply with Nikon's
terms of use. The patcher only transforms a copy you already own, on your own
machine.

## Nothing happens automatically

The tooling in this repository:

* does not download anything;
* does not connect to a camera;
* does not write to an SD card;
* does not modify the stock input file (outputs always go to a new path, and
  existing outputs are refused).

Installing a patched build on a camera, and everything that follows from it, is
the user's own decision and responsibility.

## Experimental software, no warranty

This is experimental firmware tooling. Building or installing it can cause data
loss, card corruption, or a camera that no longer starts. Offline checks and
emulator results are not a hardware safety guarantee. Do not use it for
irreplaceable work. The software is provided "as is", without warranty of any
kind, as stated in [LICENSE](LICENSE).

## Reporting results honestly

Build reports carry a `camera_verified` field. It is `false` unless the exact
byte output was run on a camera. Please keep that field accurate in your own
reports: a valid checksum says nothing about camera behaviour.

## Third-party components

* Keystone Engine (assembler used by some build steps) is GPLv2. It is a build
  time dependency only and is not bundled in any release artefact.
* If code or data from another project is reused, its licence and attribution
  belong in this file and in `LICENSES/`.
