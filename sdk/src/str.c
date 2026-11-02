/* z5sdk - helpers and SD logging.
 *
 * NOTE (2026-10-29): the hooked row renders UTF-16 text - every character is
 * stored as a 16-bit code unit. The string helpers below therefore write one
 * u16 per character and `pos` counts characters (the byte length passed to
 * z5_row_set_text is chars*2). Writing raw bytes made the row show garbage. No libc, no globals (the module body runs
 * from a read-execute region; only stack and alloc() blocks are writable). */
#include "z5/str.h"

u32 z5_strlen(const char *s)
{
    u32 n = 0;
    while (s[n]) n++;
    return n;
}

u32 z5_puts(char *dst, u32 cap, u32 pos, const char *s)
{
    u16 *w = (u16 *)dst;
    while (*s && pos < cap) w[pos++] = (u16)(u8)*s++;
    return pos;
}

u32 z5_puthex(char *dst, u32 cap, u32 pos, u32 value, u32 digits)
{
    static const char hex[] = "0123456789ABCDEF";
    for (u32 i = 0; i < digits; i++) {
        u32 shift = (digits - 1 - i) * 4;
        if (pos < cap) ((u16 *)dst)[pos++] = (u16)(u8)hex[(value >> shift) & 0xF];
    }
    return pos;
}

u32 z5_putdec(char *dst, u32 cap, u32 pos, u32 value)
{
    /* subtraction loop on purpose: keeps the blob free of libgcc helpers
     * (__aeabi_uidivmod would otherwise be needed for / 10 and % 10). */
    static const u32 pow10[] = {
        1000000000u, 100000000u, 10000000u, 1000000u, 100000u,
        10000u, 1000u, 100u, 10u, 1u
    };
    u32 started = 0;
    for (u32 i = 0; i < 10; i++) {
        u32 digit = 0;
        while (value >= pow10[i]) { value -= pow10[i]; digit++; }
        if (digit || started || i == 9) {
            if (pos < cap) ((u16 *)dst)[pos++] = (u16)('0' + digit);
            started = 1;
        }
    }
    return pos;
}
