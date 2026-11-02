/* shutter-sound - play A:\\SND.WAV when the shutter is pressed.
 *
 * The flow is three steps, and each one is visible here:
 *
 *   1. install the shutter hook      z5_hook_shutter()
 *      The hook is a copied code image. It cannot call module code, because the
 *      firmware runs it without our PIC base; it only raises an edge and a play
 *      request.
 *
 *   2. start the pacing task         z5_audio_init()
 *      The audio engine wants six messages about 20 ms apart, which no draw
 *      callback can do. This call copies the ARM task image that paces them and
 *      starts it (priority 0x30, so it never starves the UI).
 *
 *   3. collect the edges             z5_hook_take()
 *      Only to show a counter on the row; playback already happened. In
 *      silent-photography mode the firmware never issues the notification, so
 *      there are no edges and no sound - that part is unsolved.
 */
#include "z5/audio.h"
#include "z5/fs.h"
#include "z5/hook.h"
#include "z5/module.h"
#include "z5/state.h"
#include "z5/str.h"
#include "z5/ui.h"
#include "z5/wav.h"

#define WAV_PATH "A:\\SND.WAV"
#define WAV_CAP  (256u * 1024u)          /* ~1.36 s of 48 kHz stereo 16-bit */

int z5_main(z5_ctx *ctx)
{
    /* Slots live in the persistent block: the module image is freed after this
     * returns, so a static cannot hold them. */
    u8 **pcm = (u8 **)z5_state_alloc(4);
    u32 *len = (u32 *)z5_state_alloc(4);
    u32 *seen = (u32 *)z5_state_alloc(4);
    char *line = z5_ui_text(64);
    if (!pcm || !len || !seen || !line) return 1;

    if (!*pcm) {
        *pcm = (u8 *)z5_alloc(WAV_CAP, 0);
        if (!*pcm) return 1;
        if (z5_wav_load(WAV_PATH, *pcm, WAV_CAP, len) != 0 || *len < 2000u) {
            *len = 0;
        }
    }
    if (!*len) {
        u32 pos = z5_puts(line, 30, 0, "no A:\\SND.WAV");
        z5_row_set_text(ctx->element, line, pos * 2);
        z5_row_draw(ctx->element, ctx->r1, ctx->r2, ctx->r3);
        return 0;
    }

    /* 1: install the hook (idempotent). */
    if (z5_audio_init(*pcm, *len) == 0) {
        /* 2: the pacing task now exists; 1 and 2 are order-independent, but the
         *    hook needs the audio state, so initialise audio first. */
        z5_hook_shutter();
    }

    /* 3: consume the edges the hook recorded since the last redraw. */
    *seen += z5_hook_take();

    u32 pos = 0;
    pos = z5_puts(line, 30, pos, "shutter ");
    pos = z5_putdec(line, 30, pos, *seen);
    pos = z5_puts(line, 30, pos, " posted ");
    pos = z5_putdec(line, 30, pos, z5_audio_posted());
    pos = z5_puts(line, 30, pos, "/");
    pos = z5_putdec(line, 30, pos, *len / 4u);

    z5_row_set_text(ctx->element, line, pos * 2);
    z5_row_draw(ctx->element, ctx->r1, ctx->r2, ctx->r3);
    return 0;
}
