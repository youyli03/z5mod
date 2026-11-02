/* z5mod - run something when the shutter is pressed.
 *
 * The camera notifies a chain of callbacks as the shutter moves. The first one
 * carries a code that says what happened:
 *
 *     0x03030001  half press begins        0x03030004  half press released
 *     0x03030003  full press               0x03030005  full press released
 *
 * A custom shutter sound hangs on 0x03030003: the hook raises one play request
 * and chains to the callback it replaced, so the camera still sees everything.
 *
 * The callback runs in a firmware context that has no module state: it may only
 * touch memory. It cannot post messages, arm timers, call the audio service or
 * do file I/O - each of those wedges the camera. Raising the request is a single
 * increment of a word the playback task already polls, which is why it is safe.
 *
 * This callback fires once per shot, so a burst raises one request per frame and
 * the sound repeats. (The switch-bitmap path is the one that only sees a single
 * edge, because the bit stays set while the button is held.)
 *
 * Known limit: in silent-photography mode this callback is never invoked, so no
 * sound plays there - that path is still unsolved.
 */
#ifndef Z5_HOOK_H
#define Z5_HOOK_H

#include "z5.h"

/* Install the shutter hook. Idempotent; returns 0 when installed. Needs the
 * audio state, so call it after z5_audio_init(). */
int z5_hook_shutter(void);

/* Full-press notifications the hook has seen, one per shot; a burst adds
 * several. */
u32 z5_hook_shutter_count(void);

/* Edges since the last call; reading clears them. This is how a module "sees"
 * the shutter: the hook itself cannot call module code (see below). */
u32 z5_hook_take(void);

/* Why the hook cannot call your code
 * ----------------------------------
 * The firmware invokes the callback from its own context: no PIC base in r9, no
 * stack of ours, no module state. Any ordinary function in this SDK expects r9
 * and would fault on its first global access. So the hook is a copied code
 * image that only touches memory, and the module observes it by reading words
 * from z5_main. The same constraint applies to every task body and callback the
 * SDK installs. */

#endif /* Z5_HOOK_H */
