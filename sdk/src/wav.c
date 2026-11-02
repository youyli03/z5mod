#include "z5/fs.h"
#include "z5/wav.h"

int z5_wav_load(const char *path, void *dst, u32 cap, u32 *out_len)
{
    z5_fobj f;
    if (out_len) *out_len = 0;
    z5_memset(&f, 0, sizeof(f));
    z5_bind(&f, path);
    if (z5_open(&f, 0, 0) != Z5_OPEN_OK) return -1;

    u8 hdr[64];
    for (u32 i = 0; i < sizeof(hdr); i++) hdr[i] = 0;
    (void)z5_readat(&f, hdr, 0, sizeof(hdr));

    if (hdr[0] != 'R' || hdr[1] != 'I' || hdr[2] != 'F' || hdr[3] != 'F') {
        z5_close(&f);
        return -2;
    }

    /* data-chunk length lives at 0x28 of a canonical 44-byte WAV header */
    u32 dlen = (u32)hdr[0x28] | ((u32)hdr[0x29] << 8) |
               ((u32)hdr[0x2A] << 16) | ((u32)hdr[0x2B] << 24);
    if (dlen == 0 || dlen > cap) dlen = cap;
    dlen &= ~3u;                       /* whole 4-byte frames only */

    u8 *p = (u8 *)dst;
    u32 off = 0;
    while (off < dlen) {
        u32 n = dlen - off;
        if (n > Z5_WAV_CHUNK) n = Z5_WAV_CHUNK;
        (void)z5_readat(&f, p + off, 44u + off, n);
        off += n;
    }
    z5_close(&f);
    if (out_len) *out_len = dlen;
    return 0;
}