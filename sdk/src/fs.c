/* z5mod - the SD card file API. */
#include "z5/fs.h"

int z5_fs_open_path(z5_fobj *f, const char *path, u32 mode)
{
    z5_memset(f, 0, sizeof(*f));
    z5_bind(f, path);
    return z5_open(f, mode, 0);
}

/* The write primitive wants the handle the open call stored at fobj + 8, not
 * the file object itself. */
u32 z5_fs_handle(const z5_fobj *f)
{
    return *(volatile u32 *)((const u8 *)f + 8);
}

/* One 512-byte chunk at a time: that is the largest transfer this FS accepts,
 * and a longer one fails silently rather than reporting an error. */
int z5_fs_write_all(u32 handle, const void *data, u32 len)
{
    const u8 *p = (const u8 *)data;
    u32 off = 0;
    while (off < len) {
        u32 n = len - off;
        u32 out = 0;
        if (n > Z5_FS_CHUNK) n = Z5_FS_CHUNK;
        (void)z5_write(handle, p + off, n, &out);
        if (out != n) return -2;
        off += n;
    }
    return 0;
}

/* Offset reads are the only working read path; the signature is
 * (fobj, dst, offset, length). */
int z5_fs_read_all(z5_fobj *f, void *dst, u32 len)
{
    u8 *p = (u8 *)dst;
    u32 off = 0;
    while (off < len) {
        u32 n = len - off;
        if (n > Z5_FS_CHUNK) n = Z5_FS_CHUNK;
        (void)z5_readat(f, p + off, off, n);
        off += n;
    }
    return 0;
}
