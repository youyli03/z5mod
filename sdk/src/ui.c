/* z5mod - row text and drawing. */
#include "z5/ui.h"
#include "z5/state.h"

/* The draw callback receives the row in ctx->element; the text fields live on
 * the object found at [row + 0x60]. All four fields are written because the
 * previous length otherwise truncates the new text. */
void z5_row_set_text(void *row, void *text, u32 bytes)
{
    u8 *e = *(u8 **)((u8 *)row + 0x60);
    if (!e) return;
    *(u32 *)(e + Z5_ROW_TEXT_PTR1) = (u32)text;
    *(u32 *)(e + Z5_ROW_TEXT_PTR2) = (u32)text;
    *(u32 *)(e + Z5_ROW_TEXT_LEN1) = bytes;
    *(u32 *)(e + Z5_ROW_TEXT_LEN2) = bytes;
}

/* One buffer for the module's lifetime. The row keeps the pointer after the
 * callback returns, so it cannot live in the module image. */
char *z5_ui_text(u32 bytes)
{
    return (char *)z5_state_alloc(bytes);
}
