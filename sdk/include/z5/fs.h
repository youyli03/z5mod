/* z5mod - the SD card file API.
 *
 * The card is drive A:. One transfer is limited to 512 bytes: larger reads
 * return nothing at all, and larger writes are not proven.
 */
#ifndef Z5_FS_H
#define Z5_FS_H

#include "z5.h"

/* Caller-provided, zeroed before use: 0x40 bytes of file object. */
typedef struct {
    u8 raw[0x40];
} z5_fobj;

#define z5_bind    Z5_FN(int (*)(void *, const char *),     0x1033A82A)
#define z5_open    Z5_FN(int (*)(void *, u32, u32),         0x1033A82E)
#define z5_close   Z5_FN(int (*)(void *),                   0x1033A8BA)
#define z5_readat  Z5_FN(int (*)(void *, void *, u32, u32), 0x1020B0DE)
/* Writes take the handle the open call left at fobj + 8, not the fobj. */
#define z5_write   Z5_FN(int (*)(u32, const void *, u32, u32 *), 0x102CB162)

#define Z5_OPEN_OK        0x043A0080u
#define Z5_OPEN_CREATE    0x30Au    /* create or truncate                */
#define Z5_OPEN_APPEND    0x0Au    /* open existing, positioned at end  */
#define Z5_OPEN_READ      0x000u    /* open existing for reading         */

#define Z5_FS_CHUNK 512u            /* largest transfer the FS accepts   */

int  z5_fs_open_path(z5_fobj *f, const char *path, u32 mode);
u32  z5_fs_handle(const z5_fobj *f);
int  z5_fs_write_all(u32 handle, const void *data, u32 len);
int  z5_fs_read_all(z5_fobj *f, void *dst, u32 len);

#endif /* Z5_FS_H */
