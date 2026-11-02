# resource/

Test audio. **None of these files is ours.** They are short clips that were
downloaded from the internet for experimenting with the audio engine; the rights
belong to whoever made them, not to this project, and the MIT licence in the
repository root does **not** apply to them.

They are here so that a reader can reproduce the tests described in
`examples/shutter-sound` without hunting for material. Only the converted,
ready-to-play files are kept; the original downloads are not redistributed.

If you fork this project or publish a build, replace them with audio you have the
right to use. The SDK does not depend on any particular clip.

| File | What it is |
| --- | --- |
| `SND_MANBO.WAV` | one phrase cut out of a longer clip by an RMS envelope: 48 kHz stereo 16-bit, 0.69 s |
| `SND_CIALLO.WAV` | the earlier test clip, 48 kHz stereo 16-bit, 0.92 s |

## Making your own

```sh
python scripts/wav2z5.py your_clip.wav -o SND.WAV --seconds 0.9 --trim --normalize
```

Copy the result to the card root as `SND.WAV`; that is the file the example loads.
