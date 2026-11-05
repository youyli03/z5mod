/* z5mod - background log: a RAM ring, a BE_REC flusher and card batches.
 *
 * This area is separate from the small z5/log.h ring.  It is compiled only
 * when Z5_LOG_ENABLE is 1.  Each SDK area that logs has its own compile-time
 * switch (Z5_LOG_UI, Z5_LOG_FS, ...); user code can define its own flag and
 * call Z5_LOG(user_flag, ...) or z5_bglog_put() directly.  There is no runtime
 * configuration: a disabled switch removes the call from the image.
 *
 * On-disk form:
 *
 *   ['Z5LB' | count | bytes][32-byte records]['Z5LE' | count | bytes]
 *
 * The batch trailer is the commit; there is no per-record CRC.  A torn last
 * batch is discarded as a whole, the complete prefix is kept.
 *
 * Fixed words: 0xC18117C0/17C4 (magic + control pointer).  The copied put
 * image pointer lives in the control block at 0xF4, so hooks reach it through
 * the control block.  The SDK already uses 0xC1811738 (state),
 * 0xC1811788/178C/1790 (log ring) and 0xC18117C8 (audio state).
 */
#ifndef Z5_BGLOG_H
#define Z5_BGLOG_H

#include "z5/z5.h"
#include "z5/module.h"

/* Master switch: off by default.  Enable with -DZ5_LOG_ENABLE=1. */
#ifndef Z5_LOG_ENABLE
#define Z5_LOG_ENABLE 0
#endif

/* SDK area switches: only read when the master switch is on.  User code can
 * define its own Z5_LOG_<name> flag with the same pattern. */
#ifndef Z5_LOG_BOOT
#define Z5_LOG_BOOT 0
#endif
#ifndef Z5_LOG_HB
#define Z5_LOG_HB 0
#endif
#ifndef Z5_LOG_UI
#define Z5_LOG_UI 0
#endif
#ifndef Z5_LOG_DRIVER
#define Z5_LOG_DRIVER 0
#endif
#ifndef Z5_LOG_FLUSH
#define Z5_LOG_FLUSH 0
#endif
#ifndef Z5_LOG_FS
#define Z5_LOG_FS 0
#endif
#ifndef Z5_LOG_HOOK
#define Z5_LOG_HOOK 0
#endif
#ifndef Z5_LOG_AUDIO
#define Z5_LOG_AUDIO 0
#endif
#ifndef Z5_LOG_WAV
#define Z5_LOG_WAV 0
#endif
#ifndef Z5_LOG_TASK
#define Z5_LOG_TASK 0
#endif
#ifndef Z5_LOG_USER
#define Z5_LOG_USER 0
#endif

#if Z5_LOG_ENABLE

#define Z5_BGLOG_FIXED    0xC18117C0u   /* [0] magic, [1] control block        */
#define Z5_BGLOG_CTRL_PUTIMG 0xF4u      /* control block: copied put image     */
#define Z5_BGLOG_MAGIC    0x474C355Au   /* "Z5LG"                              */
#define Z5_BGLOG_VERSION  2u

/* record types (low nibble of the type/version byte) */
#define Z5_BGLOG_T_BOOT   1u
#define Z5_BGLOG_T_HB     2u
#define Z5_BGLOG_T_FLUSH  3u
#define Z5_BGLOG_T_ERROR  4u
#define Z5_BGLOG_T_SNAP   5u
#define Z5_BGLOG_T_CLEAR  6u
#define Z5_BGLOG_T_ENV    7u
#define Z5_BGLOG_T_NODE   8u
#define Z5_BGLOG_T_FS     9u
#define Z5_BGLOG_T_TASK   10u
#define Z5_BGLOG_T_UI     11u

/* node ids for Z5_BGLOG_T_NODE */
#define Z5_BGLOG_NODE_UI_ENTRY    1u
#define Z5_BGLOG_NODE_UI_ALLOC    2u
#define Z5_BGLOG_NODE_UI_HOOK     3u
#define Z5_BGLOG_NODE_UI_TASK     4u
#define Z5_BGLOG_NODE_UI_VISIT    5u
#define Z5_BGLOG_NODE_UI_TOGGLE   6u
#define Z5_BGLOG_NODE_UI_CLEAR    7u
#define Z5_BGLOG_NODE_UI_ROW      8u
#define Z5_BGLOG_NODE_DRIVER      9u
#define Z5_BGLOG_NODE_PAGE        10u
#define Z5_BGLOG_NODE_INIT        11u
#define Z5_BGLOG_NODE_FLUSH_START 12u
#define Z5_BGLOG_NODE_FLUSH_DONE  13u
#define Z5_BGLOG_NODE_FLUSH_ERROR 14u
#define Z5_BGLOG_NODE_CLEAR       15u
#define Z5_BGLOG_NODE_SHUTTER     16u
#define Z5_BGLOG_NODE_AUDIO       17u
#define Z5_BGLOG_NODE_WAV         18u
#define Z5_BGLOG_NODE_HOOK_INSTALL 19u
#define Z5_BGLOG_NODE_SHUTTER_TAKE 20u
#define Z5_BGLOG_NODE_LOAD        21u
#define Z5_BGLOG_NODE_UNLOAD      22u
#define Z5_BGLOG_NODE_UI_BEGIN    23u
#define Z5_BGLOG_NODE_UI_END      24u
#define Z5_BGLOG_NODE_TIMER       25u
#define Z5_BGLOG_NODE_HOOK        26u

/* Z5_BGLOG_T_FS operations */
#define Z5_BGLOG_FS_OPEN   1u
#define Z5_BGLOG_FS_SEEK   2u
#define Z5_BGLOG_FS_WRITE  3u
#define Z5_BGLOG_FS_CLOSE  4u
#define Z5_BGLOG_FS_BATCH  5u

/* producer source, high byte of the record flags */
#define Z5_BGLOG_SRC_UI      1u
#define Z5_BGLOG_SRC_FLUSHER 2u
#define Z5_BGLOG_SRC_BE_REC  3u
#define Z5_BGLOG_SRC_HOOK    4u
#define Z5_BGLOG_SRC_TASK    5u

/* Demo entry: init + visit + row text + draw.  Returns 0 after drawing. */
int z5_bglog_ui(z5_ctx *ctx);

/* Initialize/ensure the control block, ring, copied put image, BE_REC hook and
 * flusher task.  Idempotent; call once per redraw before z5_bglog_put. */
int z5_bglog_init(z5_ctx *ctx);

/* Demo on/off for the visit heuristic; not a logging configuration switch. */
void z5_bglog_set_mode(u32 on);

/* Append one record from module code (r9 valid).  Safe from any task; memory
 * only.  Hooks/raw tasks use the copied image pointer at Z5_BGLOG_PUT. */
void z5_bglog_put(u32 type, u32 a, u32 b, u32 c, u32 flags);

/* Compile-time filtered append: `module` is a macro such as Z5_LOG_UI or a
 * user-defined Z5_LOG_<name>.  When it is 0 the compiler removes the call. */
#define Z5_LOG(module, type, a, b, c) do { \
    if (module) z5_bglog_put((type), (a), (b), (c), 0u); \
} while (0)

/* Self-contained append for a Z5_IMAGE callback (no r9).  The module flag is
 * compile-time; the control block is only touched when it is 1. */
#define Z5_BGLOG_PUT_HOOK(module, node, a, b) do { \
    if (module) { \
        u32 z5_bglog_fn_ = 0u; \
        if (*(volatile u32 *)Z5_BGLOG_FIXED == Z5_BGLOG_MAGIC) { \
            u32 z5_bglog_c_ = *(volatile u32 *)(Z5_BGLOG_FIXED + 4u); \
            if (z5_bglog_c_ >= 0xC1800000u && z5_bglog_c_ < 0xC7A00000u && \
                *(volatile u32 *)z5_bglog_c_ == Z5_BGLOG_MAGIC) { \
                z5_bglog_fn_ = *(volatile u32 *)(z5_bglog_c_ + Z5_BGLOG_CTRL_PUTIMG); \
                if (z5_bglog_fn_ < 0xC1800000u || z5_bglog_fn_ >= 0xC7A00000u) \
                    z5_bglog_fn_ = 0u; \
            } \
        } \
        if (z5_bglog_fn_ != 0u) \
            ((void (*)(u32, u32, u32, u32, u32))z5_bglog_fn_)( \
                Z5_BGLOG_T_NODE, (node), (a), (b), \
                (Z5_BGLOG_SRC_HOOK << 8)); \
    } \
} while (0)

#else /* Z5_LOG_ENABLE */

static inline int z5_bglog_init(z5_ctx *ctx)
{
    (void)ctx;
    return 0;
}

static inline int z5_bglog_ui(z5_ctx *ctx)
{
    (void)ctx;
    return 1;                    /* let the firmware draw the row */
}

static inline void z5_bglog_set_mode(u32 on)
{
    (void)on;
}

static inline void z5_bglog_put(u32 type, u32 a, u32 b, u32 c, u32 flags)
{
    (void)type; (void)a; (void)b; (void)c; (void)flags;
}

#define Z5_LOG(module, type, a, b, c) ((void)0)
#define Z5_BGLOG_PUT_HOOK(module, node, a, b) ((void)0)

#endif /* Z5_LOG_ENABLE */

#endif /* Z5_BGLOG_H */
