# shutter-sound

Plays `A:\SND.WAV` through the camera's audio engine, which is how a custom
shutter sound will work.

## Current behaviour

A full press (notification code 0x03030003) raises one play request, so the
sound follows the shutter, and a burst plays it once per frame. Half press does
nothing. In silent-photography mode the camera does not issue that notification,
so the sound stays off there.

## Card file

The clip must be 48 kHz stereo signed 16-bit. Make one from any audio file:

```sh
python scripts/wav2z5.py clip.wav -o SND.WAV --seconds 0.9 --trim --normalize
```

Copy it to the card root as `SND.WAV`. Without it the module shows a length of 0
and stays silent.

Two ready-made clips are in `resource/`, and they are byte-identical to what the
tests used on the card:

| Repository file | Card file | Length |
| --- | --- | --- |
| `resource/SND_MANBO.WAV` | `SND.WAV` | 0.69 s |
| `resource/SND_CIALLO.WAV` | `SND.WAV` (earlier tests) | 0.92 s |

Both are third-party audio - see `resource/README.md` - so use them for testing
and put your own clip in place for anything else.

## Build

```sh
python sdk/examples/shutter-sound/build.py
```

Output: `build/Z5APP.BIN`, copy it to the card root.

## What it shows

* `z5_audio_init()` copies the clip into the four-times buffer the engine needs
  and starts the ARM playback task;
* `z5_audio_play()` only increments a request counter, so the caller never has to
  know about the six-message sequence or its pacing;
* the row prints messages posted and clip frames, so a silent run can be told
  apart from a run where nothing was posted.

## Prebuilt

`prebuilt/Z5APP.BIN` is the module that ran on the camera:

```
sha256 8dc0e2008122218c1acef02d82b0c6a1ef3e24a5e4d8015793ed62053e7bed7d
size   2604 bytes
```

Rebuilding the sources must reproduce exactly those bytes.
