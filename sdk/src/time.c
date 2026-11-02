#include "z5/time.h"

u32 z5_time_ms(void)
{
    u32 d = *(volatile u32 *)Z5_TIME_DELTA;
    u32 t = *(volatile u32 *)Z5_TIME_LO;
    if (d == 0u || d == 1u) return t;      /* already milliseconds */
    return t * d;
}

void z5_delay_ms(u32 ms)
{
    u32 t0 = z5_time_ms();
    while ((z5_time_ms() - t0) < ms) { }
}
