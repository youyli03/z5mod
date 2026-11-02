# Notification callbacks

The firmware calls a chain of callbacks as the shutter moves. Hooking the first
one is how a module learns that a picture is being taken.

Everything below was verified on hardware.

## The first callback

The object at `0xC18E648C` holds a pointer at `+8`; that is the callback the
firmware invokes, and replacing it is the hook. The replacement must call the old
pointer when it is done, or the camera loses the event.

The code in the first argument says what happened:

| code | meaning |
| --- | --- |
| `0x03030001` | half press begins |
| `0x03030004` | half press released |
| `0x03030003` | **full press** |
| `0x03030005` | full press released |

A burst raises `0x03030003` once per frame, so a sound hooked there repeats
through the burst.

## A second path, and why it is not the one used

The switch bitmap has its own callback: a pointer at `0xC1811708`, enabled by bit
0 of the byte at `0xC1811750`. Its argument points at an object holding the
current bitmap at `+4` and the previous one at `+8`; bit `0x80000000` is the
shutter half press and `0x40000000` the full press.

It has two notable properties:

* it **does** report the shutter in silent-photography mode, where the first
  callback is never invoked;
* a burst produces **one** rising edge, because the bit stays set while the
  button is held.

The SDK hooks the first callback, because that is the path proven to produce
sound. Silent photography therefore stays silent: unsolved, not forgotten.

## What a callback may do

A callback runs in a firmware context. Verified the hard way:

* it may read and write memory;
* it must **not** post messages, arm timers, call the audio service or do file
  I/O - each of those wedged the camera.

It also runs **without the module's PIC base**: `r9` is not set, so an ordinary
module function would fault on its first global access. That is why the SDK
installs a copied code image rather than a C function, and why such an image may
only use its arguments, the stack and absolute addresses.

## API

```c
z5_hook_shutter();        /* install, idempotent; needs the audio state */
z5_hook_take();           /* full-press edges since the last call, then cleared */
z5_hook_shutter_count();  /* total, for display */
```

The image raises the play request by itself - it increments the audio request
word - because pressing the shutter does not redraw the row, so raising the
request from `z5_main` would never happen.

The install is marked by the saved original callback: if that word is already
set, the hook is in place and the call returns. An earlier version wrote a magic
word just before the image instead, which was an out-of-bounds write, because the
copy helper hands out the block start.
