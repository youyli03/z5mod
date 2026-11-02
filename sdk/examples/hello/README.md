# hello

The smallest complete module: it writes one line of text on its own menu row and
draws the row itself.

```
z5mod hello C1FFA074
```

The number is `ctx->element`, the row object the draw callback received, so the
line changes if the firmware hands us a different object.

## Build

```sh
python sdk/examples/hello/build.py
```

Output: `build/Z5APP.BIN`. Copy that file to the card root and the patched
firmware runs it on the next redraw.

## Prebuilt

`prebuilt/Z5APP.BIN` is the module that was run on the camera:

```
sha256 83cd80571908e2bdfa7bb35f48a98bb57d27bbbdc51091a024248b12b829bcc5
size   2928 bytes
```

Rebuilding the sources must reproduce exactly those bytes.

## What it demonstrates

* the toolchain, the container and the loader all work end to end;
* UTF-16 row text renders correctly through `z5_row_set_text`;
* the text buffer comes from `z5_ui_text()`, so it survives the module image being
  freed and does not leak per redraw - a static array here would dangle, and a
  per-redraw `z5_alloc()` would leak.
