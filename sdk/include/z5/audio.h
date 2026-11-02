/* z5mod - play a PCM clip through the camera's audio engine.
 *
 * The engine takes six messages and needs roughly 20 ms between them, which a
 * menu callback cannot provide. So the SDK installs one small ARM task that
 * drives the sequence, and a play request is nothing but a counter increment -
 * safe to raise from a notification callback.
 *
 * Usage:
 *
 *     z5_audio_init(pcm, bytes);      // once; the clip is copied
 *     z5_audio_play();                // any time, from any context
 *
 * The clip is 48 kHz stereo signed 16-bit. z5_audio_init() copies it into a
 * buffer four times its length with silence after it, because the engine loops
 * the buffer at the clip's own period.
 */
#ifndef Z5_AUDIO_H
#define Z5_AUDIO_H

#include "z5.h"

/* Install the playback task. Returns 0 when the clip is playable, -1 when a
 * buffer or the sound task could not be obtained. Safe to call on every redraw:
 * it finds its own state and returns immediately. */
int z5_audio_init(const void *pcm, u32 bytes);

/* Raise one play request. Only increments a counter. Do not call it from the
 * draw callback of a row that is being drawn; use z5_main or a notification. */
void z5_audio_play(void);

/* Stop at the next opportunity (sends one close). */
void z5_audio_stop(void);

/* Diagnostics: messages posted, closes sent, requests seen. */
u32 z5_audio_posted(void);
u32 z5_audio_stops(void);

#endif /* Z5_AUDIO_H */
