/* z5mod - base types, the firmware call helper, and memory.
 *
 * Nothing here depends on a specific camera feature: this is the floor every
 * other header stands on.
 */
#ifndef Z5_Z5_H
#define Z5_Z5_H

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef signed int         s32;
typedef unsigned long long u64;

/* A firmware function is reached by absolute address and entered in Thumb
 * state, so bit 0 must be set. */
#define Z5_FN(t, addr) ((t)((addr) | 1u))

/* The second argument of z5_alloc chooses one of two heaps; it is not an
 * alignment. Blocks survive a module reload, which makes the heap the only
 * place to keep state that must outlive one redraw. */
#define z5_alloc   Z5_FN(void *(*)(u32, u32), 0x1021C9E0)
#define z5_free    Z5_FN(void (*)(void *),     0x1021C9F2)
#define z5_memset  Z5_FN(void *(*)(void *, int, u32), 0x10199FC8)

#endif /* Z5_Z5_H */
