/* z5time.h - firmware clock and a non-suspending delay (Round 500)
 *
 * The firmware keeps a 64-bit tick counter in RAM, incremented by the timer IRQ:
 *
 *   ldr  sb, =0xC18EB380      ; struct base        (literal @0x10036E48 -> 0xC18EB380)
 *   ldr  r0, [sb]             ; increment per tick  (1 or 10)
 *   ldr  r2, [sb, #8]         ; time, low 32 bits
 *   ldr  r1, [sb, #0xc]       ; time, high 32 bits
 *   adds r0, r0, r2
 *   adc  r1, r1, #0
 *   strd r0, r1, [sb, #8]
 *
 * Reading it never suspends, so it is usable from module code, from a task and from a
 * firmware callback. Delays built on it are busy polls: they burn CPU, so keep them
 * short and run them from a low-priority task, never from the shared message pump.
 */
#ifndef Z5_TIME_H
#define Z5_TIME_H

#include "z5.h"

#define Z5_TIME_DELTA  0xC18EB380u   /* increment per tick        */
#define Z5_TIME_LO     0xC18EB388u   /* system time, low 32 bits  */
#define Z5_TIME_HI     0xC18EB38Cu   /* system time, high 32 bits */

/* Current firmware time in milliseconds (wraps after ~49.7 days). */
u32 z5_time_ms(void);

/* Busy-wait for `ms` milliseconds. Never suspends; must not be called from the
 * shared message pump. */
void z5_delay_ms(u32 ms);

#endif
