# sdk/

Build `.Z5M` modules that the patched camera loads from the card.

```sh
python sdk/examples/hello/build.py                     # -> examples/hello/build/Z5APP.BIN
python sdk/tools/z5pack.py check sdk/examples/hello/build/Z5APP.BIN
```

The output is named the file the loader opens on the card: copy
`examples/hello/build/Z5APP.BIN` to the card root and the patched firmware runs it
on the next redraw.

Requires `arm-none-eabi-gcc` and binutils on `PATH`. The image checker also
wants `capstone` (`python -m pip install capstone`) and is optional.

## Layout

```
include/z5/   one header per area, that is the whole public API
src/          one source per area (an area may add one *_arm.c)
z5mod.ld      linked at 0; position independence comes from r9
tools/        z5build.py (compile+link+pack), z5pack.py, imgcheck.py
examples/     one directory per example: main.c, build.py, and prebuilt/ once
              the example has been run on a camera
```

| Header | Contents |
| --- | --- |
| `z5.h` | types, `Z5_FN`, `z5_alloc` / `z5_free` / `z5_memset` |
| `module.h` | `z5_ctx`, `z5_main` entry contract |
| `fs.h` | file object, open modes, chunked read/write helpers |
| `str.h` | UTF-16 string building without libc |
| `ui.h` | row text fields, `z5_row_set_text`, `z5_row_draw` |
| `log.h` | RAM ring, whole-file log helpers, streaming log |
| `task.h` | task create/start/post/find and the object layout |
| `time.h` | `z5_time_ms`, `z5_delay_ms` |
| `image.h` | `Z5_IMAGE` code images and the copy helpers |
| `svc.h` | T-Kernel service trampolines (ARM state) |
| `wav.h` | `z5_wav_load` |

## Three rules

1. **One area, one header, one source.** An area that needs ARM-state code adds
   one `*_arm.c`. No catch-all `utils.c`.
2. **Dependencies point down.** Area headers include only `z5.h`; anything two
   areas need is promoted to `z5.h`. That keeps every area removable.
3. **`z5.h` stays small.** Types, `Z5_FN`, memory. When it grows past about
   eighty lines, something in it wants its own header.

If an area ever needs more than three sources, its own data tables, or optional
distribution, promote it to `sdk/<area>/{include,src}`; the include path for
users stays the same.

## What a module must respect

* it is re-read and re-run on every redraw and its image is freed afterwards, so
  statics reset and a pointer into the image must never reach the firmware;
* anything the firmware keeps - above all the row text the row object stores -
  must come from `z5_state_alloc()` / `z5_ui_text()`, never from a static and
  never from a fresh `z5_alloc()` on every redraw (that leaks a little per frame);
* code copied into the heap (`Z5_IMAGE`) may not touch r9, call other module
  functions, or use string literals - `tools/imgcheck.py` enforces this;
* the menu waits for the callback to return, so return quickly.

## Prebuilt modules

An example that has been tested on a camera ships its tested module as
`examples/<name>/prebuilt/Z5APP.BIN`. That one file is the only module tracked in
git; everything else under `build/` is a build product. Rebuilding the example
must reproduce it byte for byte - the commit that added it records the hash.
