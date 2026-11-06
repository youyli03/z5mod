/* z5mod - finished-frame overlay on the shooting page.  See overlay.h. */
#include "z5/overlay.h"
#include "z5/state.h"
#include "z5/task.h"
#include "z5/image.h"
#include "z5/time.h"

#include "overlay_font.h"

#define ST_OFF      0xF00u
#define STATE_SLOT  0xC1811738u
#define CUR_PAGE    0xC1819F90u
#define APPLV_PAGE  0xC18216A0u
#define DEFAULT_SURF 0xD045F200u   /* comp1 / page 1, measured on hardware */
#define STRIDE      2880u
#define OVL_MAGIC   0x4F564C59u    /* 'OVLY' */
#define OVL_VER     1u
#define FONT_BYTES  (FONT_N * FONT_COLS * 4u)
#define MAX_W      720u
#define MAX_H      480u
#define SPIN_IDLE   40u
/* Hardware free-running counter, 48 MHz (low word at 0x04266008, it wraps every
 * ~89 s which is fine for wrap-safe 32-bit subtraction).  This is the register
 * the firmware's own millisecond counter is derived from; reading it directly
 * gives 20.8 ns resolution instead of guessing a unit. */
#define FW_DELAY   0x10232937u          /* firmware delay(ms), used by the audio/log paths */
#define CLOCK      0xC18EB388u          /* firmware millisecond counter */
#define FIXED_OVERHEAD 0u
/* Kernel sleep.  0x100364B8 is the T-Kernel entry whose service code is
 * 0x80060100 (one argument, task-wait group).  Passing a timeout makes the
 * task block in the kernel instead of spinning, so the cadence comes from the
 * scheduler rather than from how fast the loop happens to run. */


struct widget {
    u32 kind;
    u32 x, y, w, h;
    u32 bg, edge;
    u32 fg;
    u32 toff, tlen;      /* text: arena offset and length */
};

struct ovl_state {
    u32 magic;
    u32 ver;
    u32 started;
    u32 surf;
    u32 font;
    u32 arena;
    u32 arena_used;
    u32 period_ms;
    u32 seq;             /* seqlock: odd while a frame is being built */
    u32 n;
    u32 paints;
    u32 last_paint;
    u32 rate_t0;
    u32 rate_n;
    u32 rate;            /* EMA of repaints per second */
    u32 rate_min;        /* worst one-second window since boot */
    u32 paint_sum_ms;    /* sum of paint durations inside the window */
    u32 paint_us;        /* average paint duration, microseconds */
    u32 cycle_t0;        /* start of the current cycle (ms) */
    struct widget w[Z5_OVL_MAX_WIDGETS];
};

/* ---- paint task: copied image, so every helper must be inlined ---------- */
#define INLINE __attribute__((always_inline)) static inline

/* freestanding divide: the module links no libgcc, so a division by a variable
 * would otherwise pull in __aeabi_uidiv */
INLINE u32 udiv32(u32 n, u32 d)
{
    unsigned long long r = 0u;
    u32 q = 0u;
    if (d == 0u) return 0u;
    for (int i = 31; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) { r -= d; q |= (1u << i); }
    }
    return q;
}

INLINE int ovl_glyph(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'Z') return 10 + (c - 'A');
    if (c == ':') return 37;
    if (c == '.') return 38;
    if (c == '-') return 39;
    if (c == '/') return 40;
    if (c == '%') return 41;
    if (c == '+') return 42;
    return 36;                       /* space */
}

Z5_IMAGE(ovl) static void ovl_entry(void *arg)
{
    (void)arg;
    struct widget w[Z5_OVL_MAX_WIDGETS];
    union {
        u64 w64[MAX_W / 2u];
        u32 w32[MAX_W];
    } rb;
#define row rb.w32

    for (;;) {
        u32 blk = *(volatile u32 *)STATE_SLOT;
        if (blk >= 0xC1800000u && blk < 0xC8700000u) {
            struct ovl_state *st = (struct ovl_state *)(blk + ST_OFF);
            u32 nowms = *(volatile u32 *)CLOCK;
            u32 period = st->period_ms ? st->period_ms : 11u;
            u32 units = period;
            u32 surf = st->surf;
            const unsigned int *font = (const unsigned int *)st->font;
            const char *arena = (const char *)st->arena;
            if (st->magic == OVL_MAGIC && st->ver == OVL_VER && st->started &&
                surf >= 0x80000000u && surf < 0xF0000000u && font &&
                (st->last_paint == 0u || (u32)(nowms - st->last_paint) >= units)) {
                /* seqlock read: snapshot the widget list */
                u32 n = 0u;
                int ok = 0;
                for (u32 tries = 0u; tries < 4u && !ok; tries++) {
                    u32 s1 = st->seq;
                    if (s1 & 1u) continue;
                    n = st->n;
                    if (n > Z5_OVL_MAX_WIDGETS) n = 0u;
                    for (u32 i = 0u; i < n; i++) w[i] = st->w[i];
                    if (st->seq == s1) ok = 1;
                }
                if (ok) {
                    u32 paint_t0 = nowms;
                    st->last_paint = nowms;
                    if (*(volatile u32 *)CUR_PAGE == APPLV_PAGE) {
                        for (u32 i = 0u; i < n; i++) {
                            u32 x0 = w[i].x, y0 = w[i].y;
                            u32 ww = w[i].w, hh = w[i].h;
                            if (x0 >= MAX_W || y0 >= MAX_H) continue;
                            if (x0 + ww > MAX_W) ww = MAX_W - x0;
                            if (y0 + hh > MAX_H) hh = MAX_H - y0;
                            for (u32 y = 0u; y < hh; y++) {
                                u32 edge = (w[i].edge &&
                                            (y == 0u || y == hh - 1u));
                                for (u32 x = 0u; x < ww; x++) {
                                    u32 e = edge || (w[i].edge &&
                                                     (x == 0u || x == ww - 1u));
                                    row[x] = e ? w[i].edge : w[i].bg;
                                }
                                if (w[i].kind == Z5_OVL_TEXT && y >= 2u && y < 2u + FONT_ROWS) {
                                    u32 r = y - 2u;
                                    const char *s = arena + w[i].toff;
                                    u32 len = w[i].tlen;
                                    u32 cols = (ww > 4u) ? (ww - 4u) / FONT_COLS : 0u;
                                    if (len > cols) len = cols;
                                    for (u32 ci = 0u; ci < len; ci++) {
                                        int gi = ovl_glyph(s[ci]);
                                        const unsigned int *g = font + gi * FONT_COLS;
                                        for (u32 c = 0u; c < FONT_COLS; c++)
                                            if ((g[c] >> r) & 1u)
                                                row[2u + ci * FONT_COLS + c] = w[i].fg;
                                    }
                                }
                                {
                                    volatile u32 *dst = (volatile u32 *)
                                        (surf + (y0 + y) * STRIDE + x0 * 4u);
                                    if (((x0 & 1u) == 0u) && (ww & 1u) == 0u) {
                                        /* two pixels per store: fewer, wider
                                         * transactions into display memory */
                                        volatile u64 *d64 = (volatile u64 *)dst;
                                        u32 half = ww / 2u;
                                        for (u32 i = 0u; i < half; i++)
                                            d64[i] = rb.w64[i];
                                    } else {
                                        for (u32 x = 0u; x < ww; x++) dst[x] = row[x];
                                    }
                                }
                            }
                        }
                        st->paints = st->paints + 1u;
                        st->paint_sum_ms = st->paint_sum_ms +
                                           (u32)(*(volatile u32 *)CLOCK - paint_t0);
                        /* achieved repaint rate: if the paint costs more than the
                         * period this number drops below 1000/period, which is
                         * the only honest way to see the real cost. */
                        st->rate_n = st->rate_n + 1u;
                        if (st->rate_t0 == 0u) st->rate_t0 = nowms;
                        else if ((u32)(nowms - st->rate_t0) >= 1000u) {
                            /* EMA (weight 1/8) so the readout does not jitter
                             * with every one-second window. */
                            u32 n = st->rate_n;
                            if (n != 0u) st->paint_us = udiv32(st->paint_sum_ms * 1000u, n);
                            if (st->rate == 0u) st->rate = n;
                            else st->rate = st->rate - (st->rate >> 3) + (n >> 3);
                            if (st->rate_min == 0u || n < st->rate_min) st->rate_min = n;
                            st->rate_n = 0u;
                            st->paint_sum_ms = 0u;
                            st->rate_t0 = nowms;
                        }
                    }
                }
            }
        }
        /* Poll the clock until the period is up.  The firmware delay entry
         * (svc 0x80520100) turned out to be unstable here (66..80 fps for an
         * 11 ms request), while a deadline poll is what kept M == R before, so
         * the cadence is enforced by the loop itself.  Spin tight: the wait is
         * only the part of the period the paint did not use. */
        {
            u32 blk2 = *(volatile u32 *)STATE_SLOT;
            if (blk2 >= 0xC1800000u && blk2 < 0xC8700000u) {
                struct ovl_state *s2 = (struct ovl_state *)(blk2 + ST_OFF);
                u32 per = s2->period_ms ? s2->period_ms : 11u;
                if (s2->cycle_t0 != 0u) {
                    while ((u32)(*(volatile u32 *)CLOCK - s2->cycle_t0) < per) { }
                }
            }
        }
    }
}
Z5_IMAGE_END(ovl) static void ovl_end(void) { }

/* ---- module side ------------------------------------------------------- */

static struct ovl_state *ovl_state(void)
{
    u8 *blk = (u8 *)z5_state();
    if (!blk) return 0;
    return (struct ovl_state *)(blk + ST_OFF);
}

int z5_overlay_init(const z5_overlay_cfg *cfg)
{
    struct ovl_state *st = ovl_state();
    if (!st) return -1;

    if (st->magic != OVL_MAGIC || st->ver != OVL_VER) {
        u8 *font = (u8 *)z5_alloc(FONT_BYTES, 0);
        u8 *arena = (u8 *)z5_alloc(Z5_OVL_ARENA, 0);
        st->magic = OVL_MAGIC;
        st->ver = OVL_VER;
        st->started = 0u;
        st->surf = DEFAULT_SURF;
        st->seq = 0u;
        st->n = 0u;
        st->paints = 0u;
        st->last_paint = 0u;
        st->rate_n = 0u;
        st->rate = 0u;
        st->rate_min = 0u;
        st->rate_t0 = 0u;
        st->paint_sum_ms = 0u;
        st->paint_us = 0u;
        st->cycle_t0 = 0u;
        st->arena_used = 0u;
        st->period_ms = cfg && cfg->period_ms ? cfg->period_ms : 11u;
        st->font = (u32)font;
        st->arena = (u32)arena;
        if (font) {
            const u8 *src = (const u8 *)FONT5X7;
            for (u32 i = 0u; i < FONT_BYTES; i++) font[i] = src[i];
        }
        if (font && arena) {
            z5_task *tsk = (z5_task *)z5_alloc(sizeof(z5_task), 0);
            void *entry = z5_img_copy(ovl_entry, ovl_end);
            if (tsk && entry) {
                if (z5_task_create_raw(tsk, "Z5OVL", 0x30, entry) == 0)
                    st->started = 1u;
            }
        }
    } else if (cfg && cfg->period_ms) {
        st->period_ms = cfg->period_ms;
    }
    return st->started ? 0 : -1;
}

int z5_overlay_set_period(u16 period_ms)
{
    struct ovl_state *st = ovl_state();
    if (!st || st->magic != OVL_MAGIC) return -1;
    if (period_ms < 5u) period_ms = 5u;
    if (period_ms > 100u) period_ms = 100u;
    st->period_ms = period_ms;
    return 0;
}

int z5_overlay_begin(void)
{
    struct ovl_state *st = ovl_state();
    if (!st || st->magic != OVL_MAGIC) return -1;
    st->seq = st->seq + 1u;          /* odd: a frame is being built */
    st->n = 0u;
    st->arena_used = 0u;
    return 0;
}

static int ovl_add(u32 kind, u16 x, u16 y, u16 w, u16 h, u32 bg, u32 edge,
                   u32 fg, u32 toff, u32 tlen)
{
    struct ovl_state *st = ovl_state();
    if (!st || st->magic != OVL_MAGIC) return -1;
    if (st->n >= Z5_OVL_MAX_WIDGETS) return -1;
    struct widget *wd = &st->w[st->n];
    wd->kind = kind;
    wd->x = x; wd->y = y; wd->w = w; wd->h = h;
    wd->bg = bg; wd->edge = edge; wd->fg = fg;
    wd->toff = toff; wd->tlen = tlen;
    st->n = st->n + 1u;
    return 0;
}

int z5_overlay_rect(u16 x, u16 y, u16 w, u16 h, u32 bg, u32 edge)
{
    return ovl_add(Z5_OVL_RECT, x, y, w, h, bg, edge, bg, 0u, 0u);
}

int z5_overlay_text(u16 x, u16 y, u16 w, u16 h, u32 bg, u32 fg, const char *s)
{
    struct ovl_state *st = ovl_state();
    if (!st || st->magic != OVL_MAGIC) return -1;
    char *arena = (char *)st->arena;
    if (!arena || !s) return -1;
    u32 len = 0u;
    while (s[len] && len < 200u) len++;
    if (st->arena_used + len + 1u > Z5_OVL_ARENA) st->arena_used = 0u;   /* wrap */
    u32 off = st->arena_used;
    for (u32 i = 0u; i < len; i++) arena[off + i] = s[i];
    st->arena_used = off + len + 1u;
    return ovl_add(Z5_OVL_TEXT, x, y, w, h, bg, 0u, fg, off, len);
}

int z5_overlay_commit(void)
{
    struct ovl_state *st = ovl_state();
    if (!st || st->magic != OVL_MAGIC) return -1;
    st->seq = st->seq + 1u;          /* even again: the frame is published */
    return 0;
}

void z5_overlay_stats(z5_overlay_stats_t *out)
{
    struct ovl_state *st = ovl_state();
    if (!out) return;
    if (!st || st->magic != OVL_MAGIC) {
        out->paints = 0u; out->rate_per_s = 0u;
        out->rate_min = 0u; out->paint_us = 0u;
        return;
    }
    out->paints = st->paints;
    out->rate_per_s = st->rate;
    out->rate_min = st->rate_min;
    out->paint_us = st->paint_us;
}
