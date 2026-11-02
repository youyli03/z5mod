/* z5mod - row text and drawing. */
#ifndef Z5_UI_H
#define Z5_UI_H

#include "z5.h"

/* All four fields must be written, or the previous length truncates the new
 * text. */
#define Z5_ROW_TEXT_PTR1  0x20
#define Z5_ROW_TEXT_PTR2  0x70
#define Z5_ROW_TEXT_LEN1  0x24
#define Z5_ROW_TEXT_LEN2  0x28
#define Z5_ROW_ALIGN      0x14

/* row is ctx->element; the text fields live on [row + 0x60]. The buffer must
 * stay valid after the call, so keep one buffer for the module's lifetime. */
void z5_row_set_text(void *row, void *text, u32 bytes);

/* A text buffer the row may keep: allocated once from the persistent block
 * (see state.h),
 * so it neither dangles when the image is freed nor leaks on every redraw.
 * Use this instead of a static array or a per-render z5_alloc(). */
char *z5_ui_text(u32 bytes);

/* The original draw callback. A module that returns 0 must call it. */
#define z5_row_draw  Z5_FN(void (*)(void *, u32, u32, u32), 0x104B1F58)

#endif /* Z5_UI_H */
