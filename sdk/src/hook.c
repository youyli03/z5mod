/* z5mod - shutter hook. Modelled on the implementation that works on hardware:
 * the first notification callback, chained, raising the audio request word.
 */
#include "z5/hook.h"
#include "z5/image.h"
#include "z5/state.h"
#include "z5/audio.h"

#define CB1_SLOT   0xC18E648Cu      /* first notification callback object     */
#define SLOT_AT    8u               /* the callback pointer lives at +8       */

#define AUDIO_SLOT   0xC18117C8u    /* audio state, read by the image         */
#define AUDIO_MAGIC  0x5A3550BFu
#define ST_CB1_ORIG  0x38u          /* inside the audio state: replaced cb1   */
#define ST_REQ       0x48u          /* play request the task polls            */
#define ST_EDGES     0x10u          /* full-press edges, read by z5_hook_take */

#define CODE_FULL_PRESS 0x03030003u

#define CODE_LO 0x10020000u
#define CODE_HI 0x12100000u

/* Code image: the firmware calls this with no PIC base, so it reaches the audio
 * state through a fixed word and touches nothing else. */
Z5_IMAGE(hook_cb1)
static u32 hook_cb1(u32 code, u32 a1, u32 a2, u32 a3)
{
    u32 s = *(volatile u32 *)AUDIO_SLOT;
    u32 orig = 0u;

    if (s >= 0xC1800000u && s < 0xC8700000u &&
        *(volatile u32 *)(s + 0x08u) == AUDIO_MAGIC) {
        orig = *(volatile u32 *)(s + ST_CB1_ORIG);
        if (code == CODE_FULL_PRESS) {
            *(volatile u32 *)(s + ST_EDGES) += 1u;  /* visible to z5_main */
            *(volatile u32 *)(s + ST_REQ) += 1u;    /* the play request   */
        }
    }
    if (orig < CODE_LO || orig >= CODE_HI) return 0u;
    return ((u32 (*)(u32, u32, u32, u32))orig)(code, a1, a2, a3);
}
Z5_IMAGE_END(hook_cb1) static void hook_cb1_end(void) { }

int z5_hook_shutter(void)
{
    u32 s = *(volatile u32 *)AUDIO_SLOT;
    if (s < 0xC1800000u || s >= 0xC8700000u) return -1;
    if (*(volatile u32 *)(s + 0x08u) != AUDIO_MAGIC) return -1;

    /* The saved original is also the "already installed" mark. The previous
     * build wrote a magic word just before the image, which was an out-of-bounds
     * write because z5_img_copy hands out the block start. */
    if (*(volatile u32 *)(s + ST_CB1_ORIG)) return 0;

    volatile u32 *slot = (volatile u32 *)(CB1_SLOT + SLOT_AT);
    void *image = z5_img_copy(hook_cb1, hook_cb1_end);
    if (!image) return -1;

    *(volatile u32 *)(s + ST_CB1_ORIG) = *slot;
    *slot = (u32)image;
    return 0;
}

u32 z5_hook_take(void)
{
    u32 s = *(volatile u32 *)AUDIO_SLOT;
    if (s < 0xC1800000u || s >= 0xC8700000u) return 0;
    if (*(volatile u32 *)(s + 0x08u) != AUDIO_MAGIC) return 0;
    u32 edges = *(volatile u32 *)(s + ST_EDGES);
    *(volatile u32 *)(s + ST_EDGES) = 0u;
    return edges;
}

u32 z5_hook_shutter_count(void)
{
    u32 s = *(volatile u32 *)AUDIO_SLOT;
    if (s < 0xC1800000u || s >= 0xC8700000u) return 0;
    if (*(volatile u32 *)(s + 0x08u) != AUDIO_MAGIC) return 0;
    return *(volatile u32 *)(s + ST_REQ);
}
