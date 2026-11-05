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
#include "z5/bglog.h"
#include "z5/fs.h"
#include "z5/hook.h"
#include "z5/module.h"
#include "z5/state.h"
#include "z5/str.h"
#include "z5/time.h"
#include "z5/ui.h"
#include "z5/wav.h"

#define WAV_PATH "A:\\SND.WAV"
#define WAV_CAP  (256u * 1024u)          /* ~1.36 s of 48 kHz stereo 16-bit */

static int finish(z5_ctx *ctx, int rc, u32 seen)
{
    (void)ctx;
    Z5_LOG(Z5_LOG_UI, Z5_BGLOG_T_NODE, Z5_BGLOG_NODE_UI_END,
           seen, z5_time_ms());
    Z5_LOG(Z5_LOG_UI, Z5_BGLOG_T_NODE, Z5_BGLOG_NODE_UNLOAD,
           z5_time_ms(), (u32)rc);
    return rc;
}

int z5_main(z5_ctx *ctx)
{
    (void)z5_bglog_init(ctx);
    z5_bglog_set_mode(1u);
    Z5_LOG(Z5_LOG_UI, Z5_BGLOG_T_NODE, Z5_BGLOG_NODE_LOAD,
           ctx->sequence, ctx->length);
    Z5_LOG(Z5_LOG_UI, Z5_BGLOG_T_NODE, Z5_BGLOG_NODE_UI_BEGIN,
           (u32)ctx->element, *(volatile u32 *)0xC1819F90u);

    /* Slots live in the persistent block: the module image is freed after this
     * returns, so a static cannot hold them. */
    u8 **pcm = (u8 **)z5_state_alloc(4);
    u32 *len = (u32 *)z5_state_alloc(4);
    u32 *seen = (u32 *)z5_state_alloc(4);
    char *line = z5_ui_text(64);
    if (!pcm || !len || !seen || !line) return finish(ctx, 1, 0);

    if (!*pcm) {
        *pcm = (u8 *)z5_alloc(WAV_CAP, 0);
        if (!*pcm) return finish(ctx, 1, 0);
        int wav_rc = z5_wav_load(WAV_PATH, *pcm, WAV_CAP, len);
        if (wav_rc != 0 || *len < 2000u) {
            *len = 0;
        }
        Z5_LOG(Z5_LOG_WAV, Z5_BGLOG_T_NODE, Z5_BGLOG_NODE_WAV,
               (u32)wav_rc, *len);
    }
    if (!*len) {
        Z5_LOG(Z5_LOG_WAV, Z5_BGLOG_T_NODE, Z5_BGLOG_NODE_WAV, 0u, 0u);
        u32 pos = z5_puts(line, 30, 0, "no A:\\SND.WAV");
        z5_row_set_text(ctx->element, line, pos * 2);
        z5_row_draw(ctx->element, ctx->r1, ctx->r2, ctx->r3);
        return finish(ctx, 0, 0);
    }

    /* 1: install the hook (idempotent). */
    int audio_rc = z5_audio_init(*pcm, *len);
    Z5_LOG(Z5_LOG_AUDIO, Z5_BGLOG_T_NODE, Z5_BGLOG_NODE_AUDIO,
           (u32)audio_rc, *len / 4u);
    if (audio_rc == 0) {
        /* 2: the pacing task now exists; 1 and 2 are order-independent, but the
         *    hook needs the audio state, so initialise audio first. */
        int hook_rc = z5_hook_shutter();
        Z5_LOG(Z5_LOG_HOOK, Z5_BGLOG_T_NODE, Z5_BGLOG_NODE_HOOK_INSTALL,
               (u32)hook_rc, 0u);
    }

    /* 3: consume the edges the hook recorded since the last redraw. */
    *seen += z5_hook_take();
    Z5_LOG(Z5_LOG_UI, Z5_BGLOG_T_NODE, Z5_BGLOG_NODE_SHUTTER_TAKE,
           *seen, z5_audio_posted());

    u32 pos = 0;
    pos = z5_puts(line, 30, pos, "shutter ");
    pos = z5_putdec(line, 30, pos, *seen);
    pos = z5_puts(line, 30, pos, " posted ");
    pos = z5_putdec(line, 30, pos, z5_audio_posted());
    pos = z5_puts(line, 30, pos, "/");
    pos = z5_putdec(line, 30, pos, *len / 4u);

    z5_row_set_text(ctx->element, line, pos * 2);
    z5_row_draw(ctx->element, ctx->r1, ctx->r2, ctx->r3);
    return finish(ctx, 0, *seen);
}
