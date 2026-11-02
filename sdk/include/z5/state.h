/* z5mod - state that outlives one module load.
 *
 * The loader frees the module image as soon as z5_main returns, and the menu
 * calls z5_main again on the next redraw with a freshly loaded image. So:
 *
 *   - a static in the module image is dead after the call  -> dangling pointer
 *   - alloc() on every redraw is never freed               -> leak per frame
 *
 * Both are easy to write by accident, so the SDK offers one place for state
 * that has to survive: a block taken from the heap once, remembered in a fixed
 * word the firmware does not touch, and handed out by a bump allocator.
 */
#ifndef Z5_STATE_H
#define Z5_STATE_H

#include "z5.h"

/* Fixed word the block pointer is remembered in. It survives module reloads
 * and is not used by the firmware (the working modules relied on it). */
#define Z5_STATE_SLOT  0xC1811738u

/* The first 32 bytes are reserved for the SDK: the bump offset and the slots
 * the hook image has to reach at a fixed address. User data starts after them. */
#define Z5_STATE_RESERVED 0x20u

/* Total size of the block. Nothing is ever freed, so keep it modest. */
#define Z5_STATE_SIZE  0x1000u

/* Return the persistent block, allocating and zeroing it on first use.
 * Returns 0 when the heap is exhausted. */
void *z5_state(void);

/* Bump-allocate inside that block. The result is zeroed, 8-byte aligned and
 * stays valid for the rest of the camera session. Returns 0 when full. */
void *z5_state_alloc(u32 bytes);

#endif /* Z5_STATE_H */
