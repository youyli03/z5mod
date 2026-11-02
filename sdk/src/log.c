/* z5mod - logging: a RAM ring, and files on the card. */
#include "z5/log.h"

/* Fixed words the firmware leaves free for a module to use; the ring pointer is
 * stored there so it survives a reload. */
#define LOG_PTR  0xC1811788u
#define LOG_IDX  0xC181178Cu
#define LOG_N    0xC1811790u

int z5_log_init(void)
{
    u32 p = *(volatile u32 *)LOG_PTR;
    if (p >= 0xC1800000u && p < 0xC8700000u) return 0;
    u8 *ring = (u8 *)z5_alloc(Z5_LOG_MAX_RECORDS * 16u, 0);
    if (!ring) return -1;
    for (u32 i = 0; i < Z5_LOG_MAX_RECORDS * 16u; i++) ring[i] = 0;
    *(volatile u32 *)LOG_PTR = (u32)ring;
    *(volatile u32 *)LOG_IDX = 0u;
    *(volatile u32 *)LOG_N = 0u;
    return 0;
}

void z5_log_put4(u32 a, u32 b, u32 c, u32 d)
{
    u32 base = *(volatile u32 *)LOG_PTR;
    if (base < 0xC1800000u || base >= 0xC8700000u) return;
    u32 idx = *(volatile u32 *)LOG_IDX;
    u32 *rec = (u32 *)(base + ((idx & (Z5_LOG_MAX_RECORDS - 1u)) * 16u));
    rec[0] = a; rec[1] = b; rec[2] = c; rec[3] = d;
    *(volatile u32 *)LOG_IDX = idx + 1u;
    *(volatile u32 *)LOG_N = idx + 1u;
}

u32 z5_log_count(void)
{
    return *(volatile u32 *)LOG_N;
}

int z5_log_dump(const char *path)
{
    u32 base = *(volatile u32 *)LOG_PTR;
    u32 n = *(volatile u32 *)LOG_N;
    if (base < 0xC1800000u || base >= 0xC8700000u || n == 0u) return -1;
    if (n > Z5_LOG_MAX_RECORDS) n = Z5_LOG_MAX_RECORDS;

    z5_fobj f;
    if (z5_fs_open_path(&f, path, Z5_OPEN_APPEND) != (int)Z5_OPEN_OK) return -1;
    int rc = z5_fs_write_all(z5_fs_handle(&f), (const void *)base, n * 16u);
    z5_close(&f);
    return rc;
}

int z5_log_truncate(const char *path, const void *data, u32 len)
{
    z5_fobj f;
    if (z5_fs_open_path(&f, path, Z5_OPEN_CREATE) != (int)Z5_OPEN_OK) return -1;
    int rc = z5_fs_write_all(z5_fs_handle(&f), data, len);
    z5_close(&f);
    return rc;
}

int z5_log_read(const char *path, void *dst, u32 cap, u32 *out_len)
{
    z5_fobj f;
    if (out_len) *out_len = 0;
    if (z5_fs_open_path(&f, path, Z5_OPEN_READ) != (int)Z5_OPEN_OK) return -1;
    z5_fs_read_all(&f, dst, cap);
    z5_close(&f);
    if (out_len) *out_len = cap;
    return 0;
}

/* Append opens the existing file positioned at its end, so one open plus
 * chunked writes is the whole operation. */
int z5_log_append(const char *path, const void *data, u32 len)
{
    z5_fobj f;
    if (z5_fs_open_path(&f, path, Z5_OPEN_APPEND) != (int)Z5_OPEN_OK) return -1;
    int rc = z5_fs_write_all(z5_fs_handle(&f), data, len);
    z5_close(&f);
    return rc;
}

int z5_logfile_open(z5_logfile *lf, const char *path)
{
    z5_memset(&lf->f, 0, sizeof(lf->f));
    lf->handle = 0;
    lf->pos = 0;
    lf->open = 0;
    if (z5_fs_open_path(&lf->f, path, Z5_OPEN_CREATE) != (int)Z5_OPEN_OK) return -1;
    lf->handle = z5_fs_handle(&lf->f);
    lf->open = 1;
    return 0;
}

int z5_logfile_write(z5_logfile *lf, const void *data, u32 len)
{
    if (!lf->open) return -1;
    int rc = z5_fs_write_all(lf->handle, data, len);
    if (rc == 0) lf->pos += len;
    return rc;
}

void z5_logfile_close(z5_logfile *lf)
{
    if (lf->open) z5_close(&lf->f);
    lf->open = 0;
}
