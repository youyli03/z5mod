/* z5mod - logging: a RAM ring, and files on the card. */
#ifndef Z5_LOG_H
#define Z5_LOG_H

#include "z5.h"
#include "fs.h"

#define Z5_LOG_MAX_RECORDS 256u         /* 256 * 16 B = 4 KB ring */

int  z5_log_init(void);
void z5_log_put4(u32 a, u32 b, u32 c, u32 d);
u32  z5_log_count(void);
int  z5_log_dump(const char *path);

/* Whole-file helpers. 8.3 names on drive A: only. */
int  z5_log_truncate(const char *path, const void *data, u32 len);
int  z5_log_read(const char *path, void *dst, u32 cap, u32 *out_len);
int  z5_log_append(const char *path, const void *data, u32 len);

/* Streaming log: one open file, every write advances the position. The object
 * must live in alloc() memory because module statics are reset on reload. */
typedef struct {
    z5_fobj f;
    u32 handle;
    u32 pos;
    int open;
} z5_logfile;

int  z5_logfile_open(z5_logfile *lf, const char *path);
int  z5_logfile_write(z5_logfile *lf, const void *data, u32 len);
void z5_logfile_close(z5_logfile *lf);

#endif /* Z5_LOG_H */
