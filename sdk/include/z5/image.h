/* z5sdk - executable image helpers (Round 563).
 *
 * WHY THIS EXISTS
 * ---------------
 * The firmware's shared message pump (0x1021BF45) and the cb1 notification
 * slot call our code with NO PIC base in r9 and on a stack we do not own.
 * Ordinary module C code therefore faults there (Round 454), which is why the
 * working modules have always carried hand-written Thumb byte arrays and
 * patched their literal pools from C.
 *
 * This header turns that into plain C:
 *
 *     Z5_IMAGE(hook) static u32 hook_cb(u32 code, u32 a, u32 b, u32 c)
 *     {
 *         ...                             // self-contained code only
 *         return orig(code, a, b, c);
 *     }
 *     Z5_IMAGE_END(hook) static void hook_cb_end(void) { }
 *
 *     void *p = z5_img_copy(hook_cb, hook_cb_end);   // p has bit0 set
 *
 * HOW IT WORKS
 * ------------
 * Both functions land in one section, ".z5img.<name>". Because they are
 * `static`, GCC materialises their addresses PC-relatively
 * ("ldr r3, =delta ; add r3, pc"), so the delta survives a byte copy and the
 * copied image keeps finding its own literal pool wherever it is placed.
 * The destination must be 4-byte aligned (z5_alloc returns 8-byte aligned
 * blocks); a 2-mod-4 placement would break the literal-pool alignment of the
 * Thumb-2 ldr.w instructions - that is exactly why the old hand-written blobs
 * had to sit at blob+206 (2 mod 4) with a hand-packed literal table.
 *
 * RULES FOR IMAGE CODE (enforced by tools/imgcheck.py)
 * ---------------------------------------------------
 *   1. no globals, statics, string literals or jump tables - they need r9;
 *   2. no calls to other module functions - only this image is copied;
 *   3. only arguments, stack locals and absolute firmware addresses;
 *   4. use Z5_FN()/absolute pointers for firmware calls, never a direct call
 *      to an SDK function such as z5_alloc or z5_puts;
 *   5. end every image with a Z5_IMAGE_END marker function.
 */
#ifndef Z5_IMAGE_H
#define Z5_IMAGE_H

#include "z5.h"

/* Put a function into its own copyable section. */
#define Z5_IMAGE(name) \
    __attribute__((section(".z5img." #name), noinline, used))

/* End marker: its address is the exclusive end of the image. Defines an
 * unused symbol on purpose; keep the name unique per image. */
#define Z5_IMAGE_END(name) \
    __attribute__((section(".z5img." #name), noinline, used))

/* Copy [start, end) into an alloc() block and return a callable Thumb
 * pointer (bit0 set), or 0 when the allocation fails. Both arguments may be
 * function symbols: only their bit0 is masked off. */
void *z5_img_copy(const void *start, const void *end);

/* Same, but into caller-provided storage (must be 4-byte aligned and at
 * least (end-start) bytes). Returns a callable Thumb pointer. */
void *z5_img_copy_to(void *dst, const void *start, const void *end);

/* ARM-state images (src/imgarm.S service trampolines): same contract, but the
 * returned pointer has bit0 clear so a `blx` to it switches to ARM. */
void *z5_img_copy_arm(const void *start, const void *end);
void *z5_img_copy_to_arm(void *dst, const void *start, const void *end);

/* Copy the two service trampolines and return callable ARM pointers.
 * Use these, not z5_img_copy_arm(z5_arm_...): a GLOBAL symbol's address in
 * -fpic code comes from the GOT and is the link-time address, not the runtime
 * one (Round 563: that cost a day of hardware hangs). */
void *z5_img_copy_arm_snd_mbx(void);
void *z5_img_copy_arm_rot_rdq(void);

/* Copy the ARM-compiled control task (src/ctl_arm.c) and return a
 * callable ARM pointer (bit0 clear). */
void *z5_img_copy_arm_ctl(void);

/* Image length in bytes for a start/end pair (bit0-insensitive). */
u32 z5_img_size(const void *start, const void *end);

/* Instruction cache maintenance; call after writing code bytes. */
void z5_flush_icache(void);

#endif /* Z5_IMAGE_H */
