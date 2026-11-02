/* hello - the smallest module: draw one line of text on the module row.
 *
 * Build:  python sdk/examples/hello/build.py
 * Install: copy the .Z5M to the card as A:\\Z5APP.BIN
 */
#include "z5/module.h"
#include "z5/str.h"
#include "z5/ui.h"

int z5_main(z5_ctx *ctx)
{
    /* The row keeps this pointer after we return, and the module image is freed
     * at that point, so the buffer has to come from the persistent block. */
    char *line = z5_ui_text(64);
    if (!line) return 1;

    u32 pos = 0;
    pos = z5_puts(line, 30, pos, "z5mod hello ");
    pos = z5_puthex(line, 30, pos, (u32)ctx->element, 8);

    z5_row_set_text(ctx->element, line, pos * 2);
    z5_row_draw(ctx->element, ctx->r1, ctx->r2, ctx->r3);
    return 0;               /* we drew the row ourselves */
}
