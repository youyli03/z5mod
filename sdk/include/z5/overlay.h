/* z5mod - finished-frame overlay on the shooting page.
 *
 * Two phases, on purpose:
 *
 *   1. build  - z5_overlay_begin() .. z5_overlay_commit().  The caller appends
 *               widgets.  Only RAM is touched: no display access, no blocking,
 *               safe from the UI task, from a capture task and from a callback.
 *   2. paint  - a small task repaints the published frame every `period_ms`
 *               into the overlay layer.  Every row is written in one burst, so
 *               a display refresh can never sample half a row.
 *
 * The overlay layer is refreshed by the firmware, so the paint has to repeat
 * forever; `period_ms` trades CPU for freshness.  Budget the total widget area:
 * about 25k px is comfortable at 5 ms, about 55k px at 11 ms.
 *
 * A widget always carries its own box and background, and the paint fills that
 * box completely.  That is what keeps the panel free of gaps and stale pixels
 * from an earlier layout.
 *
 * Measured on hardware: the overlay surface accepts about 2.8 million pixels
 * per second (~11 MB/s).  Budget the widgets accordingly:
 *
 *     total widget area <= 2.8 * period_ms
 *
 * i.e. about 14k px at 5 ms, 31k px at 11 ms, 45k px at 16 ms.  Ask for a
 * cadence the area cannot sustain and the achieved rate just drops; that is
 * what z5_overlay_stats() reports.
 */
#ifndef Z5_OVERLAY_H
#define Z5_OVERLAY_H

#include "z5.h"

#define Z5_OVL_MAX_WIDGETS 12u
#define Z5_OVL_ARENA      1024u

/* Widget kinds. */
#define Z5_OVL_RECT   1u
#define Z5_OVL_TEXT   2u

/* Colours are the layer's 32-bit format, RGBA8888 with alpha last. */
#define Z5_RGBA(r, g, b, a) \
    ((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | ((u32)(a) << 24))

typedef struct {
    u16 period_ms;      /* repaint cadence, 5..100; 0 = default 11 ms */
    u16 reserved;
} z5_overlay_cfg;

/* Create the state, copy the font, install the paint task.  Call from z5_main;
 * idempotent.  cfg may be NULL for the defaults.  Returns 0 on success. */
int  z5_overlay_init(const z5_overlay_cfg *cfg);

/* Change the cadence at run time (5..100 ms). */
int  z5_overlay_set_period(u16 period_ms);

/* Phase 1: start a new frame.  Discards any uncommitted widgets. */
int  z5_overlay_begin(void);

/* Phase 1: append widgets.  Return 0 on success, <0 when the frame is full. */
int  z5_overlay_rect(u16 x, u16 y, u16 w, u16 h, u32 bg, u32 edge);
int  z5_overlay_text(u16 x, u16 y, u16 w, u16 h, u32 bg, u32 fg, const char *s);

/* Phase 2: publish the frame.  The paint task picks it up on its next tick. */
int  z5_overlay_commit(void);

typedef struct {
    u32 paints;       /* total repaints since init */
    u32 rate_per_s;   /* smoothed repaints per second */
    u32 rate_min;     /* worst one-second window since init */
    u32 paint_us;     /* average duration of one paint, microseconds */
} z5_overlay_stats_t;

/* Statistics for a debug row.  rate_per_s drops below 1000/period_ms when the
 * paint cannot keep up; paint_us says how much of the period it eats. */
void z5_overlay_stats(z5_overlay_stats_t *out);

#endif /* Z5_OVERLAY_H */
