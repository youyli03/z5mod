/* z5mod - string building without libc.
 *
 * The row renders UTF-16: every character is a 16-bit unit, `pos` counts
 * characters, and the byte length handed to z5_row_set_text is pos * 2.
 * Writing raw bytes makes the row show garbage. */
#ifndef Z5_STR_H
#define Z5_STR_H

#include "z5.h"

u32 z5_strlen(const char *s);
u32 z5_puts(char *dst, u32 cap, u32 pos, const char *s);
u32 z5_puthex(char *dst, u32 cap, u32 pos, u32 value, u32 digits);
u32 z5_putdec(char *dst, u32 cap, u32 pos, u32 value);

#endif /* Z5_STR_H */
