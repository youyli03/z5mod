/* z5mod - state that outlives one module load. */
#include "z5/state.h"

/* The first 32 bytes are reserved: the bump offset plus the fixed slots the
 * hook image reads (see state.h and hook.c). */
#define USED_OFFSET 0u
#define DATA_START  Z5_STATE_RESERVED

void *z5_state(void)
{
    u32 p = *(volatile u32 *)Z5_STATE_SLOT;
    if (p >= 0xC1800000u && p < 0xC8700000u) return (void *)p;

    u8 *block = (u8 *)z5_alloc(Z5_STATE_SIZE, 0);
    if (!block) return 0;
    for (u32 i = 0; i < Z5_STATE_SIZE; i++) block[i] = 0;
    *(volatile u32 *)Z5_STATE_SLOT = (u32)block;
    return block;
}

void *z5_state_alloc(u32 bytes)
{
    u8 *block = (u8 *)z5_state();
    if (!block) return 0;

    u32 used = *(u32 *)(block + USED_OFFSET);
    u32 need = (bytes + 7u) & ~7u;
    if (DATA_START + used + need > Z5_STATE_SIZE) return 0;
    *(u32 *)(block + USED_OFFSET) = used + need;
    return block + DATA_START + used;
}
