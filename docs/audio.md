# Audio

The engine plays a clip from RAM. It takes six messages, about 20 ms apart, and
that pacing is the whole difficulty: a single burst of six messages plays once
and then refuses to play again.

Everything below was verified on hardware with the module that plays `A:\SND.WAV`
on the shutter.

## The six messages

| # | opcode | meaning |
| --- | --- | --- |
| 0 | `0x43140016` | close the previous stream |
| 1 | `0x43140017` | open |
| 2 | `0x43140014` | format: `0x01010500` = 48 kHz, 16 bit, stereo |
| 3 | `0x4314001A` | prepare: buffer pointer and length |
| 4 | `0x4314001A` | prepare |
| 5 | `0x43140015` | start: `0x1023E01D` |

Each message is 0x60 bytes: the opcode sits at `+0x10` and a pointer to a
32-byte argument block at `+0x14`. The close message's argument word 2 is
`0x20`.

## The buffer

The engine loops the buffer at the clip's own period, so it needs **four times
the clip length**, with silence (`0x80` bytes) after the clip. Without the
silent tail the engine would repeat whatever happened to be in the heap.

The four words at `0xC1805A10` describe the clip to the firmware and are written
before the first stream starts.

## Pace it from a task

Posting from a draw callback cannot work: the menu needs the callback to return,
and the engine needs the gaps *between* messages. The SDK therefore copies a
small ARM task into the heap and starts it at **priority 0x30**.

Priority matters. A task that spins with `rot_rdq` only yields to tasks of the
same priority, and in T-Kernel a smaller number means a higher priority: the same
task at 0x14 starves the UI and the menu stops responding. The proven values are
`0x14` for a message-pump task and `0x30` for this one.

## The state the task reads

The task finds one pointer through a fixed word (`0xC18117C8`), then reads
everything from that block:

| offset | meaning |
| --- | --- |
| `+0x08` | magic `0x5A3550BF` |
| `+0x0C` | tag |
| `+0x1C` | pointer to itself, so a stale pointer can be rejected |
| `+0x28` | PCM buffer |
| `+0x2C` | the six messages |
| `+0x30` | the firmware's sound task |
| `+0x40` | scratch copy the task posts from |
| `+0x48` | **play request counter** |
| `+0x4C` | clip length in bytes |
| `+0x50` | control block |
| `+0x54` | clip length in ms |

The control block itself holds the state machine: mode, deadline, handled
request, stops sent, next step, next tick and the number of messages posted.

## The state machine

```
request counter changes -> send messages 0..5, one per tick
                       -> playing; arm a deadline at now + clip length
deadline reached       -> send one close
```

Taking a new request resets the step counter, so starting from idle and
retriggering during playback are the same path, and playback can be inserted at
any moment.

A play request is a plain counter increment, which is what makes it safe to raise
from a notification callback.

## API

```c
z5_audio_init(pcm, bytes);   /* copies the clip, starts the pacing task */
z5_audio_play();             /* one request; never called by the SDK itself */
z5_audio_stop();             /* close at the next tick */
z5_audio_posted();           /* diagnostics */
```

`z5_audio_init()` is idempotent: it finds its state through the fixed word and
returns immediately when it is already installed. That is what allows the example
to call it from `z5_main` on every redraw without creating a second task.
