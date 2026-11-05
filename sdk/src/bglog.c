/* logtest - background SD-card logger for the Nikon Z5 (C1.50) module loader.
 *
 * WHY THIS SHAPE
 * --------------
 * A card module cannot write the card from its own raw task: every FS call
 * goes through the single synchronous FILESRV client and suspends on
 * wai_sem/wai_flg, and suspension from a module-created task wedges the whole
 * camera (NOTES R543/R570.8).  The one verified workaround is to let the
 * firmware's BE_REC task do the FS work: BE_REC's pump handler is replaced by a
 * small copied image ("trampoline") and our own raw task pokes BE_REC's event
 * flag; the trampoline runs in BE_REC's task context, where open/write/close
 * are proven to work (NOTES R581.8/R581.13/R581.31).
 *
 * So the data path is:
 *
 *   driver task (ours, raw, never returns)
 *        |  writes 32-byte records into a RAM ring (memory only)
 *        |  set_flg(BE_REC flag, bit31)
 *        v
 *   BE_REC task context (trampoline image, R581)
 *        |  open(APPEND) -> write 512-byte chunks -> close
 *        v
 *   A:\Z5LOG.BIN   (must already exist on the card; the module cannot create)
 *
 * The file is append-only, each record is 32 bytes, and every flush closes the
 * file so exFAT commits the new size (NOTES R581.26).  A power cut can only
 * tear the record currently being written; the parser validates magic+crc32
 * and keeps the complete prefix (P0).
 *
 * PAGE TRANSITIONS - the "SD card unavailable" fix (R581.19)
 * ---------------------------------------------------------
 * Entering the shooting page performs a media check/takeover.  An FS request
 * in flight at that moment makes the camera report "SD card unavailable" and
 * the next open returns 0x043A0089.  The driver therefore samples the current
 * page object (0xC1819F90) every 10 ms: on any change it keeps the FS quiet
 * for PAGE_GUARD_MS (3 s) and only flushes after the page has been stable for
 * 3 s.  This is best-effort: a request already in flight cannot be cancelled.
 * For the strict zero-conflict mode (RAM only while the shooting page is
 * current, flush after returning to the menu) compile with
 * -DSHOOTING_WRITE=0 (or change the #define below).
 *
 * WHAT IS VERIFIED AND WHAT IS NOT (read tmp/log-system-design.md)
 * ---------------------------------------------------------------
 *  - BE_REC trampoline + set_flg + open/write/close in BE_REC: hardware
 *    verified (R581.13/R581.15/R581.31).
 *  - APPEND (0x0A) after close: hardware verified (R581.27).
 *  - seek(0x0A FS op) to query the file size: STATIC ONLY (disassembled
 *    0x102CB190 -> 0x10289260 -> 0x1029614a).  If it fails, the module falls
 *    back to append-without-size and says so on the row (S0).
 *  - multi-producer append: UI, flusher and BE_REC all reserve tickets with
 *    ldrex/strex; the flusher only consumes the contiguous committed prefix.
 *
 * BUILD (from sdk/)
 *   python3 examples/bglog/build.py
 *   python3 tools/z5pack.py check examples/bglog/build/Z5APP.BIN
 *   ../venv/bin/python tools/imgcheck.py examples/bglog/build/obj/main.elf
 *
 * CARD PREPARATION (host side, never done by the module)
 *   create A:\Z5LOG.BIN as a 32-byte placeholder or an existing log; the
 *   module never creates or truncates the file.
 *
 * CONTROLS (the module row is a draw callback, not a button)
 *   leave the row for >1.2 s and come back  -> toggle logging on/off
 *   do that twice within 2 s                -> clear the log (truncate file)
 */
#include "z5/bglog.h"
#include "z5/fs.h"
#include "z5/image.h"
#include "z5/module.h"
#include "z5/str.h"
#include "z5/task.h"
#include "z5/time.h"
#include "z5/ui.h"

#if Z5_LOG_ENABLE

/* ------------------------------------------------------------------ */
/* fixed RAM word and control block                                    */
/* ------------------------------------------------------------------ */
#define FIXED_WORD   0xC18117C0u     /* [0] magic, [1] control-block pointer   */
#define CTL_MAGIC    0x474C355Au     /* bytes "Z5LG"                           */
#define CTL_VERSION  1u

#define C_MAGIC      0x00u
#define C_VERSION    0x04u
#define C_MODE       0x08u           /* 0 off, 1 on, 3 fault/full              */
#define C_FLAGS      0x0Cu           /* bit0 = clear request                   */
#define C_PRODUCED   0x10u
#define C_DROPPED    0x14u
#define C_FLUSHED    0x18u
#define C_FLUSHES    0x1Cu
#define C_ERRORS     0x20u
#define C_LAST_RC    0x24u
#define C_LAST_SIZE  0x28u           /* 0xFFFFFFFF = unknown                  */
#define C_NEXT_SEQ   0x2Cu
#define C_PROD       0x30u
#define C_CONS       0x34u
#define C_RING       0x38u
#define C_RINGCAP    0x3Cu
#define C_BOOTID     0x40u
#define C_LASTFLUSH  0x44u
#define C_LASTVISIT  0x48u
#define C_LASTCALL   0x4Cu
#define C_REQ_KIND   0x50u           /* 0 init, 1 flush, 2 clear              */
#define C_REQ_CONS   0x54u
#define C_REQ_PROD   0x58u
#define C_REQ_WR     0x5Cu
#define C_REQ_RC     0x60u
#define C_REQ_DONE   0x64u
#define C_STARTED    0x68u           /* 0 no, 1 yes, 2 failed                 */
#define C_BLOB       0x6Cu           /* trampoline image in alloc memory      */
#define C_READY      0x70u
#define C_SEEK_OK    0x74u
#define C_BOOT_A     0x78u           /* ctx->sequence                         */
#define C_BOOT_B     0x7Cu           /* ctx->element                          */
#define C_PATH       0x80u           /* 16 bytes, "A:\Z5LOG.BIN"               */
#define C_FOBJ       0x90u           /* 0x40-byte fobj                        */
#define C_REQ_TOKEN  0xD0u           /* request generation (stale-ack guard)   */
#define C_REQ_ACK    0xD4u
#define C_BUSY       0xD8u           /* 1 while BE_REC owns the request fields */
#define C_BOOT_BYTES 0xDCu           /* bytes written this boot (seek fallback) */
#define C_LAST_PAGE  0xE0u           /* last sampled page object               */
#define C_PAGE_SINCE 0xE4u           /* when that page became current          */
#define C_PAUSE_UNTIL 0xE8u          /* no FS writes before this time          */
#define C_FLUSH_REQ  0xECu           /* UI: flush once on this menu visit      */
#define C_REQ_RECS   0xF0u           /* records written by the last flush      */
#define C_PUTIMG     0xF4u           /* copied log_put image pointer           */
#define C_TEXT       0x100u          /* 160-byte UTF-16 row text buffer        */
#define C_SIZE       0x180u

#define CTL_ALLOC    0x1A0u
#define RING_RECORDS 1024u           /* power of two                          */
#define RING_BYTES   (RING_RECORDS * 32u)

/* ------------------------------------------------------------------ */
/* record / superblock format (32 bytes, exactly)                      */
/* ------------------------------------------------------------------ */
#define SB_MAGIC     0x474C355Au     /* "Z5LG" */
#define REC_MAGIC    0x524C355Au     /* "Z5LR" */
#define BATCH_MAGIC  0x424C355Au     /* "Z5LB" batch header */
#define BATCH_END    0x454C355Au     /* "Z5LE" batch trailer / commit */
#define BATCH_N      16u             /* records per batch (512 B payload) */
#define REC_SIZE     32u
#define LOG_MAX_BYTES 262144u        /* 256 KiB hard cap, 32-byte header + 8191 records */

#define R_MAGIC  0u
#define R_SEQ    4u
#define R_TIME   8u
#define R_TV     12u                 /* u8: low nibble type, high nibble version */
#define R_LEN    13u                 /* u8: record length, always 32            */
#define R_FLAGS  14u
#define R_A      16u
#define R_B      20u
#define R_C      24u
#define R_CRC    28u
#define REC_VERSION 1u

#define T_BOOT   1u
#define T_HB     2u
#define T_FLUSH  3u
#define T_ERROR  4u
#define T_SNAP   5u
#define T_CLEAR  6u
#define T_ENV    7u
#define T_NODE   8u
#define T_FS     9u
#define T_TASK   10u
#define T_UI     11u

/* ------------------------------------------------------------------ */
/* firmware addresses used from copied images                          */
/* ------------------------------------------------------------------ */
#define BE_SLOT       0xC236507Cu    /* BE_REC obj+0x2C, original 0x100A7FCF   */
#define BE_FLAGADDR   0xC2365070u    /* BE_REC obj+0x20, holds flag id         */
#define BE_ORIG       0x100A7FCFu
#define POKE          0x80000000u    /* firmware uses only bit0/bit1 (R581.2)   */

#define SET_FLG       0x1019A199u
#define DELAY         0x10232937u
#define FS_BIND       0x1033A82Bu
#define FS_OPEN       0x1033A82Fu
#define FS_CLOSE      0x1033A8BBu
#define FS_WRITE      0x102CB163u
#define FS_SEEK       0x102CB191u
#define OPEN_OK       0x043A0080u
#define O_READ        0x000u
#define O_APPEND      0x00Au
#define O_CREATE      0x30Au

#ifndef BATCH_RECORDS
#define BATCH_RECORDS 32u
#endif
#ifndef FLUSH_MS
#define FLUSH_MS      1000u
#endif
#ifndef HB_MS
#define HB_MS         500u
#endif
#ifndef REQ_TIMEOUT
#define REQ_TIMEOUT   5000u
#endif
#define FULL_RC       0x0000F001u

/* Page-transition guard (the "SD card unavailable" fix, R581.19).
 * The camera's shooting-page entry performs a media check/takeover; an FS
 * request in flight at that moment makes the app report "SD card unavailable"
 * and the next open returns 0x043A0089.  So: never flush within
 * PAGE_GUARD_MS of a page change, and only after the current page has been
 * stable for that long.  PAGE_GUARD_MS is tunable (default 3 s; try 500-1500
 * on hardware to find the smallest value that stays clean).  Set
 * SHOOTING_WRITE=0 for the strict zero-conflict mode (RAM only while the
 * APPLV/shooting page is current; flush after returning to the menu). */
#ifndef SHOOTING_WRITE
#define SHOOTING_WRITE 1u
#endif
#define PAGE_OBJ      0xC1819F90u
#define APPLV_OBJ     0xC18216A0u
#ifndef PAGE_GUARD_MS
#define PAGE_GUARD_MS 3000u
#endif
#ifndef VISIT_DELAY_MS
#define VISIT_DELAY_MS 1000u   /* menu quiet window after a new visit */
#endif

/* shared-state accessors; volatile because the other task writes them */
#define R32(p, o) (*(volatile u32 *)((u8 *)(p) + (o)))
#define R16(p, o) (*(volatile u16 *)((u8 *)(p) + (o)))
#define W32(p, o, v) (*(volatile u32 *)((u8 *)(p) + (o)) = (u32)(v))
#define W16(p, o, v) (*(volatile u16 *)((u8 *)(p) + (o)) = (u16)(v))
#define W8(p, o, v)  (*(volatile u8 *)((u8 *)(p) + (o)) = (u8)(v))

#define Z5_ALWAYS_INLINE __attribute__((always_inline)) static inline

/* z5sdk/src/time.c, inlined so the copied images do not call module code */
Z5_ALWAYS_INLINE u32 now_ms(void)
{
    u32 d = *(volatile u32 *)0xC18EB380u;
    u32 t = *(volatile u32 *)0xC18EB388u;
    if (d == 0u || d == 1u) return t;
    return t * d;
}

Z5_ALWAYS_INLINE u32 crc32(const u8 *p, u32 n)
{
    u32 c = 0xFFFFFFFFu;
    for (u32 i = 0; i < n; i++) {
        c ^= p[i];
        for (u32 k = 0; k < 8u; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

/* ARMv7 exclusive-monitor atomics.  These are used by the multi-producer ring:
 * the UI, the flusher and BE_REC can all append.  No libatomic, no locks. */
Z5_ALWAYS_INLINE u32 ldrex32(volatile u32 *p)
{
    u32 v;
    __asm__ volatile ("ldrex %0, [%1]" : "=&r"(v) : "r"(p) : "memory");
    return v;
}

Z5_ALWAYS_INLINE u32 strex32(u32 v, volatile u32 *p)
{
    u32 r;
    /* Early-clobber output: the status register must not alias the value
     * register.  With plain "=r" GCC emitted `strex r3, r3, [r4]`, which is
     * unpredictable and made every log_put spin in the CAS loop. */
    __asm__ volatile ("strex %0, %1, [%2]"
                      : "=&r"(r) : "r"(v), "r"(p) : "memory");
    return r;
}

Z5_ALWAYS_INLINE void clrex(void)
{
    __asm__ volatile ("clrex" ::: "memory");
}

Z5_ALWAYS_INLINE void atomic_inc(volatile u32 *p)
{
    u32 old, n, fail;
    do {
        old = ldrex32(p);
        n = old + 1u;
        fail = strex32(n, p);
    } while (fail != 0u);
}

Z5_ALWAYS_INLINE void atomic_add(volatile u32 *p, u32 v)
{
    u32 old, n, fail;
    do {
        old = ldrex32(p);
        n = old + v;
        fail = strex32(n, p);
    } while (fail != 0u);
}

Z5_ALWAYS_INLINE void memzero(void *p, u32 n)
{
    u8 *q = (u8 *)p;
    for (u32 i = 0; i < n; i++) q[i] = 0u;
}

/* Multi-producer ring append: reserve a ticket with the exclusive monitor,
 * write the record, publish magic last.  The flusher only advances over
 * records whose magic and seq both match the ticket, so a producer that is
 * preempted mid-write stops the consumer at that slot instead of exposing a
 * half-written record. */
Z5_ALWAYS_INLINE u32 log_put(u8 *c, u32 type, u32 a, u32 b, u32 v, u32 flags)
{
    u32 cap = R32(c, C_RINGCAP);
    u32 ring = R32(c, C_RING);
    if (ring == 0u || cap == 0u) {
        atomic_inc((volatile u32 *)(c + C_DROPPED));
        return 0u;
    }

    u32 cons = R32(c, C_CONS);
    volatile u32 *pp = (volatile u32 *)(c + C_PROD);
    u32 ticket, n, fail;
    for (;;) {
        ticket = ldrex32(pp);
        if ((u32)(ticket - cons) >= cap) {
            clrex();
            atomic_inc((volatile u32 *)(c + C_DROPPED));
            return 0u;
        }
        n = ticket + 1u;
        fail = strex32(n, pp);
        if (fail == 0u) break;
    }

    u8 *r = (u8 *)(ring + ((ticket & (cap - 1u)) << 5));
    W32(r, R_SEQ, ticket);
    W32(r, R_TIME, now_ms());
    W8(r, R_TV, (u8)((REC_VERSION << 4) | (type & 0x0Fu)));
    W8(r, R_LEN, (u8)REC_SIZE);
    W16(r, R_FLAGS, flags);
    W32(r, R_A, a);
    W32(r, R_B, b);
    W32(r, R_C, v);
    W32(r, R_CRC, 0u);                  /* batch trailer is the commit */
    __asm__ volatile ("dmb sy" ::: "memory");
    W32(r, R_MAGIC, REC_MAGIC);         /* publish last */
    __asm__ volatile ("dmb sy" ::: "memory");
    atomic_inc((volatile u32 *)(c + C_PRODUCED));
    return 1u;
}

/* Highest contiguous committed ticket; the consumer never reads past it. */
Z5_ALWAYS_INLINE u32 log_end(u8 *c)
{
    u32 cap = R32(c, C_RINGCAP);
    u32 ring = R32(c, C_RING);
    u32 prod = R32(c, C_PROD);
    u32 end = R32(c, C_CONS);
    if (ring == 0u || cap == 0u) return end;
    __asm__ volatile ("dmb sy" ::: "memory");
    while (end != prod) {
        u8 *r = (u8 *)(ring + ((end & (cap - 1u)) << 5));
        if (R32(r, R_MAGIC) != REC_MAGIC || R32(r, R_SEQ) != end) break;
        end++;
    }
    return end;
}

/* Copied image for contexts without r9 (hooks / raw tasks).  It reads the
 * fixed control pointer and appends through the same multi-producer path. */
Z5_IMAGE(putimg) static void putimg(u32 type, u32 a, u32 b, u32 c, u32 flags)
{
    u32 cp = *(volatile u32 *)(FIXED_WORD + 4u);
    if (cp == 0u) return;
    u8 *ctl = (u8 *)cp;
    if (R32(ctl, C_MAGIC) != CTL_MAGIC) return;
    log_put(ctl, type, a, b, c, flags);
}
Z5_IMAGE_END(putimg) static void putimg_end(void) { }

void z5_bglog_put(u32 type, u32 a, u32 b, u32 c, u32 flags)
{
    u32 fn = 0u;
    if (*(volatile u32 *)FIXED_WORD == CTL_MAGIC) {
        u32 cp = *(volatile u32 *)(FIXED_WORD + 4u);
        if (cp >= 0xC1800000u && cp < 0xC7A00000u &&
            R32((u8 *)cp, C_MAGIC) == CTL_MAGIC) {
            fn = R32((u8 *)cp, C_PUTIMG);
            if (fn < 0xC1800000u || fn >= 0xC7A00000u) fn = 0u;
        }
    }
    if (fn != 0u)
        ((void (*)(u32, u32, u32, u32, u32))fn)(type, a, b, c, flags);
}

Z5_ALWAYS_INLINE int write_all(u32 h, const u8 *p, u32 n, u32 *total)
{
    u32 done = 0u;
    *total = 0u;
    while (done < n) {
        u32 len = n - done;
        u32 out = 0u;
        if (len > 512u) len = 512u;
        int rc = ((int (*)(u32, const void *, u32, u32 *))FS_WRITE)(h, p + done, len, &out);
        if (rc != 0 || out != len) {
            *total = done;
            return rc ? rc : -1;
        }
        done += out;
    }
    *total = done;
    return 0;
}

Z5_ALWAYS_INLINE int write_batch(u32 h, const u8 *recs, u32 count, u32 *total)
{
    u32 hdrw[2];
    u32 trlw[2];
    u8 *hdr = (u8 *)hdrw;
    u8 *trl = (u8 *)trlw;
    u32 nbytes = count << 5;
    W32(hdr, 0u, BATCH_MAGIC);
    W16(hdr, 4u, (u16)count);
    W16(hdr, 6u, (u16)nbytes);
    W32(trl, 0u, BATCH_END);
    W16(trl, 4u, (u16)count);
    W16(trl, 6u, (u16)nbytes);

    u32 o = 0u;
    u32 sum = 0u;
    int rc = write_all(h, hdr, 8u, &o);
    sum += o;
    if (rc == 0) { rc = write_all(h, recs, nbytes, &o); sum += o; }
    if (rc == 0) { rc = write_all(h, trl, 8u, &o); sum += o; }
    *total = sum;
    return rc;
}

Z5_ALWAYS_INLINE void fs_begin(u8 *c, const char *path, u32 mode)
{
    u8 *f = c + C_FOBJ;
    memzero(f, 0x40u);
    ((int (*)(void *, const char *))FS_BIND)(f, path);
    (void)mode;}

Z5_ALWAYS_INLINE int fs_open(u8 *c, u32 mode)
{
    return ((int (*)(void *, u32, u32))FS_OPEN)(c + C_FOBJ, mode, 0u);
}

Z5_ALWAYS_INLINE u32 fs_handle(u8 *c)
{
    return *(volatile u32 *)(c + C_FOBJ + 8u);
}

Z5_ALWAYS_INLINE void fs_close(u8 *c)
{
    (void)((int (*)(void *))FS_CLOSE)(c + C_FOBJ);
}

Z5_ALWAYS_INLINE int write_sb(u8 *c, u32 h)
{
    u32 sbw[8];                       /* 4-byte aligned: W32 below */
    u8 *sb = (u8 *)sbw;
    memzero(sb, 32u);
    W32(sb, 0u, SB_MAGIC);
    W32(sb, 4u, CTL_VERSION);
    W32(sb, 8u, REC_SIZE);
    W32(sb, 12u, LOG_MAX_BYTES);
    W32(sb, 16u, R32(c, C_BOOTID));
    W32(sb, 20u, 0u);
    W32(sb, 24u, 0u);
    W32(sb, 28u, crc32(sb, 28u));
    u32 out = 0u;
    return write_all(h, sb, 32u, &out);
}

/* INIT: only learn the file size.  Never create, never truncate: a wrong or
 * missing size is not evidence that the file is empty.  A 16-byte placeholder
 * is fine - records append after it and the parser scans for their magic.  The
 * PC can pre-create a 32-byte superblock (z5log_parse.py --init) for metadata. */
Z5_ALWAYS_INLINE int log_init_file(u8 *c)
{
    const char *path = (const char *)(c + C_PATH);
    fs_begin(c, path, O_APPEND);
    int rc = fs_open(c, O_APPEND);
    u32 size = 0xFFFFFFFFu;
    u32 seek_ok = 0u;
    if (rc != (int)OPEN_OK) {
        log_put(c, T_FS, Z5_BGLOG_FS_OPEN, (u32)rc, 0u, (Z5_BGLOG_SRC_BE_REC << 8));
        return rc;
    }
    log_put(c, T_FS, Z5_BGLOG_FS_OPEN, 0u, fs_handle(c), (Z5_BGLOG_SRC_BE_REC << 8));

    u32 pos = 0u;
    int sr = ((int (*)(u32, u32, u32, u32 *))FS_SEEK)(fs_handle(c), 0u, 2u, &pos);
    log_put(c, T_FS, Z5_BGLOG_FS_SEEK, (u32)sr, pos, (Z5_BGLOG_SRC_BE_REC << 8));
    if (sr == 1) { size = pos; seek_ok = 1u; }
    fs_close(c);
    log_put(c, T_FS, Z5_BGLOG_FS_CLOSE, 0u, size, (Z5_BGLOG_SRC_BE_REC << 8));
    W32(c, C_SEEK_OK, seek_ok);

    if (!seek_ok) {
        u32 last = R32(c, C_LAST_SIZE);
        size = (last >= 32u && last != 0xFFFFFFFFu) ? last : 0xFFFFFFFFu;
    }

    W32(c, C_LAST_SIZE, size);
    /* Batch headers/trailers make (size-32)/32 invalid; seq is per-boot and
     * the parser splits sessions on the BOOT record. */
    W32(c, C_NEXT_SEQ, 0u);
    W32(c, C_READY, 1u);
    return 0;
}

Z5_ALWAYS_INLINE int log_flush(u8 *c)
{
    if (R32(c, C_READY) == 0u) {
        int r = log_init_file(c);
        if (r != 0) return r;
    }
    u32 cons = R32(c, C_REQ_CONS);
    u32 prod = R32(c, C_REQ_PROD);
    u32 n = prod - cons;
    if (n == 0u) { W32(c, C_REQ_RECS, 0u); W32(c, C_REQ_WR, 0u); return 0; }
    u32 ring = R32(c, C_RING);
    u32 cap = R32(c, C_RINGCAP);
    if (ring == 0u || cap == 0u) return -3;

    /* Batch overhead: 8 B header + 8 B trailer per <=16 records. */
    u32 bmax = ((n + BATCH_N - 1u) >> 4) + 1u;
    u32 bytes = (n << 5) + (bmax << 4);
    u32 size = R32(c, C_LAST_SIZE);
    if (size != 0xFFFFFFFFu && size + bytes > LOG_MAX_BYTES)
        return (int)FULL_RC;
    if (size == 0xFFFFFFFFu &&
        R32(c, C_BOOT_BYTES) + bytes > LOG_MAX_BYTES)
        return (int)FULL_RC;          /* seek failed: at least bound this boot */

    fs_begin(c, (const char *)(c + C_PATH), O_APPEND);
    int rc = fs_open(c, O_APPEND);
    if (rc != (int)OPEN_OK) {
        log_put(c, T_FS, Z5_BGLOG_FS_OPEN, (u32)rc, 0u, (Z5_BGLOG_SRC_BE_REC << 8));
        return rc;
    }
    rc = 0;                            /* open OK; 0x043A0080 is not a flush rc */
    u32 h = fs_handle(c);
    log_put(c, T_FS, Z5_BGLOG_FS_OPEN, 0u, h, (Z5_BGLOG_SRC_BE_REC << 8));

    /* Re-read the true end before every flush: a previous partial write can
     * leave C_LAST_SIZE too small, and a stale size would break the cap. */
    u32 cur = 0u;
    int sr = ((int (*)(u32, u32, u32, u32 *))FS_SEEK)(h, 0u, 2u, &cur);
    log_put(c, T_FS, Z5_BGLOG_FS_SEEK, (u32)sr, cur, (Z5_BGLOG_SRC_BE_REC << 8));
    if (sr == 1) {
        size = cur;
        W32(c, C_LAST_SIZE, cur);
        if (size + bytes > LOG_MAX_BYTES) {
            fs_close(c);
            return (int)FULL_RC;
        }
    }

    u32 slot = cons & (cap - 1u);
    u32 n1 = n;
    if (n1 > cap - slot) n1 = cap - slot;
    u32 wrote_recs = 0u;
    u32 wrote_bytes = 0u;

    for (u32 done = 0u; done < n1; ) {
        u32 bn = n1 - done;
        u32 wb = 0u;
        if (bn > BATCH_N) bn = BATCH_N;
        int r = write_batch(h, (const u8 *)(ring + ((slot + done) << 5)),
                            bn, &wb);
        if (r != 0) { rc = r; break; }
        log_put(c, T_FS, Z5_BGLOG_FS_BATCH, bn, wb, (Z5_BGLOG_SRC_BE_REC << 8));
        wrote_recs += bn;
        wrote_bytes += wb;
        done += bn;
    }
    if (rc == 0 && n1 < n) {
        u32 seg = n - n1;
        for (u32 done = 0u; done < seg; ) {
            u32 bn = seg - done;
            u32 wb = 0u;
            if (bn > BATCH_N) bn = BATCH_N;
            int r = write_batch(h, (const u8 *)(ring + (done << 5)), bn, &wb);
            if (r != 0) { rc = r; break; }
            log_put(c, T_FS, Z5_BGLOG_FS_BATCH, bn, wb, (Z5_BGLOG_SRC_BE_REC << 8));
            wrote_recs += bn;
            wrote_bytes += wb;
            done += bn;
        }
    }
    fs_close(c);
    log_put(c, T_FS, Z5_BGLOG_FS_CLOSE, rc == 0 ? 0u : (u32)rc, wrote_bytes,
            (Z5_BGLOG_SRC_BE_REC << 8));

    W32(c, C_REQ_RECS, wrote_recs);
    W32(c, C_REQ_WR, wrote_bytes);
    if (wrote_recs != 0u) {
        W32(c, C_CONS, cons + wrote_recs);
        W32(c, C_FLUSHED, R32(c, C_FLUSHED) + wrote_recs);
        W32(c, C_BOOT_BYTES, R32(c, C_BOOT_BYTES) + wrote_bytes);
        if (size != 0xFFFFFFFFu) W32(c, C_LAST_SIZE, size + wrote_bytes);
    }
    if (rc == 0) W32(c, C_FLUSHES, R32(c, C_FLUSHES) + 1u);
    return rc;
}

Z5_ALWAYS_INLINE int log_clear(u8 *c)
{
    fs_begin(c, (const char *)(c + C_PATH), O_CREATE);
    int rc = fs_open(c, O_CREATE);
    if (rc != (int)OPEN_OK) {
        log_put(c, T_FS, Z5_BGLOG_FS_OPEN, (u32)rc, 0u, (Z5_BGLOG_SRC_BE_REC << 8));
        return rc;
    }
    log_put(c, T_FS, Z5_BGLOG_FS_OPEN, 0u, fs_handle(c), (Z5_BGLOG_SRC_BE_REC << 8));
    rc = write_sb(c, fs_handle(c));
    log_put(c, T_FS, Z5_BGLOG_FS_WRITE, (u32)rc, 32u, (Z5_BGLOG_SRC_BE_REC << 8));
    fs_close(c);
    log_put(c, T_FS, Z5_BGLOG_FS_CLOSE, 0u, 32u, (Z5_BGLOG_SRC_BE_REC << 8));
    if (rc != 0) return rc;

    W32(c, C_LAST_SIZE, 32u);
    W32(c, C_NEXT_SEQ, 0u);
    W32(c, C_PROD, 0u);
    W32(c, C_CONS, 0u);
    W32(c, C_PRODUCED, 0u);
    W32(c, C_DROPPED, 0u);
    W32(c, C_FLUSHED, 0u);
    W32(c, C_FLUSHES, 0u);
    W32(c, C_ERRORS, 0u);
    W32(c, C_BOOT_BYTES, 0u);
    W32(c, C_READY, 1u);
    return 0;
}

/* ------------------------------------------------------------------ */
/* image 1: BE_REC handler trampoline                                  */
/* ------------------------------------------------------------------ */
Z5_IMAGE(bh) static u32 bh(u32 payload)
{
    if (payload & POKE) {
        volatile u32 *fw = (volatile u32 *)FIXED_WORD;
        u32 cp = fw[1];
        if (fw[0] == CTL_MAGIC && cp >= 0xC1800000u && cp < 0xC7A00000u) {
            u8 *c = (u8 *)cp;
            if (R32(c, C_MAGIC) == CTL_MAGIC) {
                u32 kind = R32(c, C_REQ_KIND);
                int rc = 0;
                if (kind == 0u) rc = log_init_file(c);
                else if (kind == 1u) rc = log_flush(c);
                else if (kind == 2u) rc = log_clear(c);
                W32(c, C_REQ_RC, (u32)rc);
                W32(c, C_REQ_ACK, R32(c, C_REQ_TOKEN));
                W32(c, C_BUSY, 0u);
            }
        }
    }
    if (payload & ~POKE)
        return ((u32 (*)(u32))BE_ORIG)(payload);
    return 0u;
}
Z5_IMAGE_END(bh) static void bh_end(void) { }

/* ------------------------------------------------------------------ */
/* image 2: driver task (raw T-Kernel task; must never return)         */
/* ------------------------------------------------------------------ */
Z5_ALWAYS_INLINE int request(u8 *c, u32 kind, u32 cons, u32 prod)
{
    u32 t0 = now_ms();
    while (R32(c, C_BUSY) != 0u) {
        if ((u32)(now_ms() - t0) > REQ_TIMEOUT) return -2;
        ((void (*)(u32))DELAY)(5u);
    }
    u32 tok = R32(c, C_REQ_TOKEN) + 1u;
    if (tok == 0u) tok = 1u;
    W32(c, C_REQ_TOKEN, tok);
    W32(c, C_REQ_ACK, 0u);
    W32(c, C_BUSY, 1u);
    W32(c, C_REQ_CONS, cons);
    W32(c, C_REQ_PROD, prod);
    W32(c, C_REQ_WR, 0u);
    W32(c, C_REQ_RC, 0xFFFFFFFFu);
    W32(c, C_REQ_KIND, kind);
    __asm__ volatile ("dmb sy" ::: "memory");
    ((void (*)(u32 *, u32))SET_FLG)((u32 *)BE_FLAGADDR, POKE);
    t0 = now_ms();
    while (R32(c, C_REQ_ACK) != tok) {
        if ((u32)(now_ms() - t0) > REQ_TIMEOUT) return -1;
        ((void (*)(u32))DELAY)(5u);
    }
    return (int)R32(c, C_REQ_RC);
}

Z5_ALWAYS_INLINE void do_flush(u8 *c)
{
    u32 prod = log_end(c);
    u32 cons = R32(c, C_CONS);
    if (prod == cons) return;
    log_put(c, T_NODE, Z5_BGLOG_NODE_FLUSH_START, cons, prod, (Z5_BGLOG_SRC_FLUSHER << 8));
    int rc = request(c, 1u, cons, prod);
    W32(c, C_LAST_RC, (u32)rc);
    if (rc == 0) {
        log_put(c, T_NODE, Z5_BGLOG_NODE_FLUSH_DONE, R32(c, C_REQ_RECS),
                R32(c, C_REQ_WR), (Z5_BGLOG_SRC_FLUSHER << 8));
        log_put(c, T_FLUSH, R32(c, C_REQ_RECS), R32(c, C_REQ_WR),
                R32(c, C_LAST_SIZE), 0u);
    } else {
        W32(c, C_ERRORS, R32(c, C_ERRORS) + 1u);
        if ((u32)rc == FULL_RC || rc == -1 || rc == -2) W32(c, C_MODE, 3u);
        log_put(c, T_NODE, Z5_BGLOG_NODE_FLUSH_ERROR, (u32)rc, cons,
                (Z5_BGLOG_SRC_FLUSHER << 8));
        log_put(c, T_ERROR, (u32)rc, 1u, cons, 0u);
    }
}

Z5_IMAGE(drv) static void drv(void)
{
    volatile u32 *fw = (volatile u32 *)FIXED_WORD;
    u32 s = 0u;
    for (u32 i = 0; i < 1000u && s == 0u; i++) {
        if (fw[0] == CTL_MAGIC) s = fw[1];
        if (s == 0u) ((void (*)(u32))DELAY)(10u);
    }
    if (s < 0xC1800000u || s >= 0xC7A00000u || fw[0] != CTL_MAGIC)
        for (;;) ((void (*)(u32))DELAY)(1000u);
    u8 *c = (u8 *)s;
    if (R32(c, C_MAGIC) != CTL_MAGIC) for (;;) ((void (*)(u32))DELAY)(1000u);
    log_put(c, T_NODE, Z5_BGLOG_NODE_DRIVER, (u32)c, 0u, (Z5_BGLOG_SRC_FLUSHER << 8));

    /* INIT is deferred into the main loop and gated by the page guard: no FS
     * request at all until the current page has been stable for 3 s.  This
     * covers the menu -> shooting-page transition (the R581.19 window). */
    u32 last_hb = 0u;
    u32 last_fl = 0u;
    u32 last_timer = 0u;
    u32 booted = 0u;
    W32(c, C_LAST_PAGE, 0xFFFFFFFFu);   /* force a 3 s guard on the first sample */
    for (;;) {
        u32 now = now_ms();
        u32 page = *(volatile u32 *)PAGE_OBJ;

        /* Any page change means the camera may be doing a media check/takeover.
         * Keep the FS quiet for PAGE_GUARD_MS; this is the R581.19 fix. */
        if (page != R32(c, C_LAST_PAGE)) {
            u32 oldp = R32(c, C_LAST_PAGE);
            W32(c, C_LAST_PAGE, page);
            W32(c, C_PAGE_SINCE, now);
            W32(c, C_PAUSE_UNTIL, now + PAGE_GUARD_MS);
            log_put(c, T_NODE, Z5_BGLOG_NODE_PAGE, page, oldp, (Z5_BGLOG_SRC_FLUSHER << 8));
        }
        u32 win = (now >= R32(c, C_PAUSE_UNTIL)) &&
                  ((u32)(now - R32(c, C_PAGE_SINCE)) >= PAGE_GUARD_MS);
#if SHOOTING_WRITE == 0
        if (page == APPLV_OBJ) win = 0u;   /* strict: RAM only in shooting page */
#endif

        if (booted == 0u) {
            if (win) {
                int rc = request(c, 0u, 0u, 0u);
                if (rc == 0) {
                    log_put(c, T_BOOT, R32(c, C_BOOT_A), R32(c, C_BOOT_B),
                            page, 0u);
                    log_put(c, T_SNAP, R32(c, C_SEEK_OK), R32(c, C_LAST_SIZE),
                            *(volatile u32 *)0xC1D64790u, 0u);
                    log_put(c, T_ENV, *(volatile u32 *)0xC2365070u,
                            *(volatile u32 *)0xC2365080u,
                            *(volatile u32 *)0xC236507Cu, 0u);
                    log_put(c, T_NODE, Z5_BGLOG_NODE_INIT, 0u,
                            R32(c, C_LAST_SIZE), (Z5_BGLOG_SRC_FLUSHER << 8));
                    booted = 1u;
                    last_hb = now;
                    last_timer = now;
                } else {
                    W32(c, C_LAST_RC, (u32)rc);
                    W32(c, C_ERRORS, R32(c, C_ERRORS) + 1u);
                    log_put(c, T_NODE, Z5_BGLOG_NODE_INIT, (u32)rc, 0u,
                            (Z5_BGLOG_SRC_FLUSHER << 8));
                    ((void (*)(u32))DELAY)(5000u);
                }
            }
            ((void (*)(u32))DELAY)(10u);
            continue;
        }

        if ((R32(c, C_FLAGS) & 1u) && win) {
            if (request(c, 2u, 0u, 0u) == 0) {
                log_put(c, T_NODE, Z5_BGLOG_NODE_CLEAR, R32(c, C_BOOTID), 0u,
                        (Z5_BGLOG_SRC_FLUSHER << 8));
                log_put(c, T_CLEAR, R32(c, C_BOOTID), 0u, 0u, 0u);
                W32(c, C_FLAGS, R32(c, C_FLAGS) & ~1u);
                W32(c, C_FLUSH_REQ, 0u);
            } else {
                ((void (*)(u32))DELAY)(1000u);
            }
            last_fl = now;
        }

        if (R32(c, C_MODE) == 1u) {
            if ((u32)(now - last_hb) >= HB_MS) {
                u32 surf = *(volatile u32 *)0xC181FAE8u;
                log_put(c, T_HB, page, APPLV_OBJ, surf,
                        (u32)(R32(c, C_DROPPED) & 0xFFFFu));
                last_hb = now;
            }
        }

        /* In the shooting page: flush continuously (1 s / 32 records) once the
         * page is stable.  In the menu: flush once per visit (C_FLUSH_REQ),
         * which is the safe point that drains the transition buffer without
         * leaving a request in flight when the user switches pages. */
        u32 prod = log_end(c);
        u32 cons = R32(c, C_CONS);
        if (R32(c, C_MODE) == 1u &&
            (u32)(now - last_timer) >= 1000u) {
            log_put(c, T_NODE, Z5_BGLOG_NODE_TIMER, now, page,
                    (Z5_BGLOG_SRC_FLUSHER << 8));
            last_timer = now;
        }
        u32 in_shoot = (page == APPLV_OBJ) ? 1u : 0u;
        u32 want = 0u;
        if (in_shoot) {
            if (R32(c, C_MODE) == 1u &&
                (prod - cons >= BATCH_RECORDS ||
                 (prod > cons && (u32)(now - last_fl) >= FLUSH_MS)))
                want = 1u;
        } else {
            if (prod > cons && R32(c, C_FLUSH_REQ) != 0u) want = 1u;
        }
        if (win && !(R32(c, C_FLAGS) & 1u) && want) {
            /* Re-read the page immediately before the FS call: if it moved
             * during the last 10 ms poll, skip this window. */
            if (*(volatile u32 *)PAGE_OBJ != page) {
                last_fl = now;
            } else {
                do_flush(c);
                last_fl = now;
                if (in_shoot == 0u) W32(c, C_FLUSH_REQ, 0u);
            }
        }
        ((void (*)(u32))DELAY)(10u);
    }
}
Z5_IMAGE_END(drv) static void drv_end(void) { }

/* ------------------------------------------------------------------ */
/* UI side (module entry: menu task)                                   */
/* ------------------------------------------------------------------ */
static u8 *get_ctrl(void)
{
    volatile u32 *fw = (volatile u32 *)FIXED_WORD;
    if (fw[0] != CTL_MAGIC) return 0;
    u32 p = fw[1];
    if (p < 0xC1800000u || p >= 0xC7A00000u || (p & 3u) != 0u) return 0;
    u8 *c = (u8 *)p;
    if (R32(c, C_MAGIC) != CTL_MAGIC) return 0;
    return c;
}

static u8 *new_ctrl(z5_ctx *ctx)
{
    u8 *c = (u8 *)z5_alloc(CTL_ALLOC, 0);
    if (!c) return 0;
    memzero(c, CTL_ALLOC);

    u8 *ring = (u8 *)z5_alloc(RING_BYTES, 0);
    if (!ring) return 0;
    memzero(ring, RING_BYTES);

    W32(c, C_MAGIC, CTL_MAGIC);
    W32(c, C_VERSION, CTL_VERSION);
    W32(c, C_MODE, 0u);
    W32(c, C_RING, (u32)ring);
    W32(c, C_RINGCAP, RING_RECORDS);
    log_put(c, T_NODE, Z5_BGLOG_NODE_UI_ALLOC, (u32)c, (u32)ring, (Z5_BGLOG_SRC_UI << 8));
    W32(c, C_LAST_SIZE, 0xFFFFFFFFu);
    W32(c, C_BOOTID, now_ms() ^ ((u32)ctx->sequence << 16) ^ 0x5A354C47u);
    if (R32(c, C_BOOTID) == 0u) W32(c, C_BOOTID, 1u);
    W32(c, C_BOOT_A, ctx->sequence);
    W32(c, C_BOOT_B, (u32)ctx->element);

    static const char path[] = "A:\\Z5LOG.BIN";   /* module rodata; copied once */
    for (u32 i = 0; i < 16u; i++) {
        u8 ch = (u8)path[i];
        *(u8 *)(c + C_PATH + i) = ch;
        if (ch == 0u) break;
    }
    return c;
}

static void ensure_hook(u8 *c)
{
    volatile u32 *slot = (volatile u32 *)BE_SLOT;
    u32 cur = *slot;
    u32 blob = R32(c, C_BLOB);

    if (blob != 0u && cur == blob) return;
    if (cur != BE_ORIG && cur != blob) {
        /* somebody else owns BE_REC; do not overwrite it */
        return;
    }
    if (cur == BE_ORIG && (R32(c, C_STARTED) != 0u || R32(c, C_READY) != 0u)) {
        /* First install after a boot, or the slot was restored: a kernel
         * restart also kills any previous driver task, and the old image
         * block is gone with the old heap.  Drop the stale blob pointer and
         * reset the runtime counters; ensure_task() creates a fresh driver.
         * The card file is untouched; INIT recomputes seq/size from it. */
        W32(c, C_STARTED, 0u);
        W32(c, C_READY, 0u);
        W32(c, C_MODE, 0u);
        W32(c, C_FLAGS, 0u);
        W32(c, C_LASTCALL, 0u);
        W32(c, C_LASTVISIT, 0u);
        W32(c, C_PROD, 0u);
        W32(c, C_CONS, 0u);
        W32(c, C_PRODUCED, 0u);
        W32(c, C_DROPPED, 0u);
        W32(c, C_FLUSHED, 0u);
        W32(c, C_FLUSHES, 0u);
        W32(c, C_ERRORS, 0u);
        W32(c, C_BUSY, 0u);
        W32(c, C_LAST_SIZE, 0xFFFFFFFFu);
        W32(c, C_NEXT_SEQ, 0u);
        W32(c, C_BLOB, 0u);
        W32(c, C_RING, 0u);
        W32(c, C_RINGCAP, 0u);
        blob = 0u;
    }
    if (blob == 0u) {
        void *h = z5_img_copy(bh, bh_end);
        if (!h) return;
        blob = (u32)h;
        W32(c, C_BLOB, blob);
    }
    if (cur == BE_ORIG) *slot = blob;
    log_put(c, T_NODE, Z5_BGLOG_NODE_UI_HOOK, cur, blob, (Z5_BGLOG_SRC_UI << 8));
}

static void ensure_task(u8 *c)
{
    if (R32(c, C_STARTED) != 0u) return;
    if (R32(c, C_BLOB) == 0u) return;
    if (*(volatile u32 *)BE_SLOT != R32(c, C_BLOB)) return;
    if (R32(c, C_RING) == 0u) {
        u8 *ring = (u8 *)z5_alloc(RING_BYTES, 0);
        if (!ring) return;
        memzero(ring, RING_BYTES);
        W32(c, C_RING, (u32)ring);
        W32(c, C_RINGCAP, RING_RECORDS);
    }

    void *d = z5_img_copy(drv, drv_end);
    if (!d) return;
    const char name[8] = { 'Z','5','L','O','G','D',0,0 };
    z5_task t;
    int rc = z5_task_create_raw(&t, name, 0x30u, d);
    W32(c, C_STARTED, (rc == 0 && t.start_rc == 0) ? 1u : 2u);
    W32(c, C_LAST_RC, rc == 0 ? (u32)t.start_rc : (u32)rc);
    log_put(c, T_NODE, Z5_BGLOG_NODE_UI_TASK, (u32)rc, (u32)t.start_rc, (Z5_BGLOG_SRC_UI << 8));
}

static void visit(u8 *c)
{
    u32 now = now_ms();
    u32 last = R32(c, C_LASTCALL);
    if (last != 0u && (u32)(now - last) > 1200u) {
        u32 lv = R32(c, C_LASTVISIT);
        W32(c, C_FLUSH_REQ, 1u);       /* persist pending records on this visit */
        /* Quiet window: if the user enters the row and immediately switches to
         * the shooting page, the page-change guard resets and no FS request is
         * made.  If they stay, the flush happens after this delay. */
        W32(c, C_PAGE_SINCE, now);
        W32(c, C_PAUSE_UNTIL, now + VISIT_DELAY_MS);
        log_put(c, T_NODE, Z5_BGLOG_NODE_UI_VISIT, (u32)now, lv, (Z5_BGLOG_SRC_UI << 8));
        if (R32(c, C_STARTED) == 2u) W32(c, C_STARTED, 0u);  /* retry create */
        if (lv != 0u && (u32)(now - lv) < 2000u) {
            W32(c, C_FLAGS, R32(c, C_FLAGS) | 1u);       /* clear */
            log_put(c, T_NODE, Z5_BGLOG_NODE_UI_CLEAR, (u32)now, lv, (Z5_BGLOG_SRC_UI << 8));
        } else {
            u32 m = R32(c, C_MODE);
            W32(c, C_MODE, (m == 1u) ? 0u : 1u);
            log_put(c, T_NODE, Z5_BGLOG_NODE_UI_TOGGLE, (u32)now, m, (Z5_BGLOG_SRC_UI << 8));
        }
        W32(c, C_LASTVISIT, now);
    }
    W32(c, C_LASTCALL, now);
}

int z5_bglog_init(z5_ctx *ctx)
{
    u8 *c = get_ctrl();
    volatile u32 *fw = (volatile u32 *)FIXED_WORD;
    /* If BE_REC's slot is still the original handler, this is a new boot (or
     * the slot was restored).  A control block left in RAM may point at a
     * freed heap block from the previous boot; do not trust it. */
    if (c && *(volatile u32 *)BE_SLOT == BE_ORIG) c = 0;
    if (!c) {
        c = new_ctrl(ctx);
        if (!c) return 1;
        fw[1] = (u32)c;
        fw[0] = CTL_MAGIC;
        ensure_hook(c);
    }
    /* One copied put image per control block; it lives in the system heap and
     * survives module reloads, so hooks/raw tasks can call it without r9.  The
     * hook image finds it in the control block (C_PUTIMG), not in a separate
     * fixed word. */
    if (R32(c, C_PUTIMG) == 0u) {
        void *p = z5_img_copy(putimg, putimg_end);
        if (p) W32(c, C_PUTIMG, (u32)p);
    }
    /* A driver created now (cold boot / stale control block) reads these when
     * it writes the BOOT record, so refresh them before ensure_task(). */
    W32(c, C_BOOT_A, ctx->sequence);
    W32(c, C_BOOT_B, (u32)ctx->element);
    ensure_hook(c);
    ensure_task(c);
    return 0;
}

void z5_bglog_set_mode(u32 on)
{
    u8 *c = get_ctrl();
    if (c) W32(c, C_MODE, on ? 1u : 0u);
}

int z5_bglog_ui(z5_ctx *ctx)
{
    if (z5_bglog_init(ctx) != 0) return 1;
    u8 *c = get_ctrl();
    if (!c) return 1;
    log_put(c, T_NODE, Z5_BGLOG_NODE_UI_ENTRY, ctx->sequence,
            (u32)ctx->element, (Z5_BGLOG_SRC_UI << 8));
    visit(c);

    u32 slot = *(volatile u32 *)BE_SLOT;
    u32 blob = R32(c, C_BLOB);
    u32 mode = R32(c, C_MODE);
    u32 n = 0u;
    char *t = (char *)(c + C_TEXT);

    n = z5_puts(t, 48u, n, (blob != 0u && slot == blob) ? "O " : "X ");
    n = z5_puts(t, 48u, n, "T");
    n = z5_puthex(t, 48u, n, R32(c, C_STARTED), 1u);
    n = z5_puts(t, 48u, n, " L");
    n = z5_puthex(t, 48u, n, mode, 1u);
    n = z5_puts(t, 48u, n, " S");
    n = z5_puthex(t, 48u, n, R32(c, C_SEEK_OK), 1u);
    n = z5_puts(t, 48u, n, " N");
    n = z5_puthex(t, 48u, n, R32(c, C_PRODUCED), 8u);
    n = z5_puts(t, 48u, n, " D");
    n = z5_puthex(t, 48u, n, R32(c, C_DROPPED), 4u);
    n = z5_puts(t, 48u, n, " F");
    n = z5_puthex(t, 48u, n, R32(c, C_FLUSHES), 4u);
    n = z5_puts(t, 48u, n, " R");
    n = z5_puthex(t, 48u, n, R32(c, C_LAST_RC) & 0xFFu, 2u);

    u32 sz = R32(c, C_LAST_SIZE);
    n = z5_puts(t, 48u, n, " Z");
    if (sz == 0xFFFFFFFFu) {
        n = z5_puts(t, 48u, n, "??????");
    } else {
        n = z5_puthex(t, 48u, n, sz, 6u);
    }

    log_put(c, T_NODE, Z5_BGLOG_NODE_UI_ROW, R32(c, C_PRODUCED),
            R32(c, C_MODE), (Z5_BGLOG_SRC_UI << 8));
    z5_row_set_text(ctx->element, t, n * 2u);
    z5_row_draw(ctx->element, ctx->r1, ctx->r2, ctx->r3);
    return 0;
}

#else /* Z5_LOG_ENABLE */

typedef int z5_bglog_disabled;

#endif /* Z5_LOG_ENABLE */
