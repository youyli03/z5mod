/* z5mod - audio engine: setup and the request side.
 *
 * The sequence itself lives in audio_arm.c, which runs as its own ARM task.
 * This file only builds the buffers, the six messages and the state the task
 * reads, then raises requests.
 */
#include "z5/audio.h"
#include "z5/image.h"
#include "z5/state.h"
#include "z5/task.h"

#define AUDIO_MAGIC    0x5A3550BFu
#define AUDIO_TAG      0x0000010Fu
#define SNDSRV_HANDLER 0x101307F1u      /* the firmware's sound service task */
#define FW_STATE       0xC18117C8u      /* where the task finds the state     */

/* State block offsets. The first group is what the firmware's engine reads. */
#define ST_MAGIC   0x08u
#define ST_TAG     0x0Cu
#define ST_SELF    0x1Cu
#define ST_BUSY    0x20u
#define ST_PCM     0x28u
#define ST_MSGS    0x2Cu
#define ST_SND     0x30u
#define ST_SCRATCH 0x40u
#define ST_REQ     0x48u
#define ST_LEN     0x4Cu
#define ST_CP      0x50u                /* control block, for the ARM task    */
#define ST_MS      0x54u                /* clip length in ms, for the task    */

/* Control block offsets, matching audio_arm.c. */
#define CP_MODE    0x00u
#define CP_DEADLINE 0x04u
#define CP_HANDLED 0x08u
#define CP_STOPS   0x0Cu
#define CP_STEP    0x10u
#define CP_NEXT    0x14u
#define CP_POSTED  0x18u

#define MSG_SIZE   0x60u
#define MSG_COUNT  6u

#define RAM_OK(p)  ((p) >= 0xC1800000u && (p) < 0xC8700000u)

/* The engine's buffer slot; the firmware expects these words to describe the
 * clip before a stream starts. */
#define MEDIA_STATE 0xC1805A10u

static void *find_task(u32 handler)
{
    for (u32 a = 0xC1800000u; a < 0xC8700000u; a += 4u) {
        if (*(volatile u32 *)a != handler) continue;
        u32 obj = a - Z5_TCB_HANDLER;
        u32 id = *(volatile u32 *)obj;
        if (id < 0x01000000u || id >= 0x02000000u) continue;
        return (void *)obj;
    }
    return 0;
}

static u32 state_block(void)
{
    u32 s = *(volatile u32 *)FW_STATE;
    if (RAM_OK(s) && *(volatile u32 *)(s + ST_MAGIC) == AUDIO_MAGIC &&
        *(volatile u32 *)(s + ST_SELF) == s) {
        return s;
    }
    return 0;
}

static void build_messages(u8 *msgs, u32 pcm, u32 len)
{
    static const u32 ops[MSG_COUNT] = {
        0x43140016u, 0x43140017u, 0x43140014u,
        0x4314001Au, 0x4314001Au, 0x43140015u,
    };
    u32 w[MSG_COUNT][8];

    for (u32 i = 0; i < MSG_COUNT; i++)
        for (u32 j = 0; j < 8; j++) w[i][j] = 0;

    w[0][2] = 0x20u;                        /* close the previous stream      */
    w[1][0] = 0; w[1][1] = 1u; w[1][2] = 0x0Cu;
    w[1][4] = 0x0Cu; w[1][6] = 0x0Cu;       /* open descriptor                */
    w[2][0] = 0x01010500u;                  /* 48 kHz, 16 bit, stereo         */
    w[3][0] = pcm; w[3][1] = len; w[3][2] = 4u; w[3][4] = 1u; w[3][7] = 2u;
    w[4][0] = pcm; w[4][1] = len; w[4][2] = 4u; w[4][4] = 1u; w[4][7] = 2u;
    w[5][0] = 0; w[5][1] = 0x1023E01Du;     /* start                          */

    for (u32 i = 0; i < MSG_COUNT; i++) {
        u8 *m = msgs + i * MSG_SIZE;
        u8 *d = m + 0x40;
        for (u32 j = 0; j < 8; j++) *(u32 *)(d + j * 4) = w[i][j];
        *(u32 *)(m + 0x10) = ops[i];
        *(u32 *)(m + 0x14) = (u32)d;
    }
}

int z5_audio_init(const void *pcm, u32 bytes)
{
    if (!pcm || bytes < 64u) return -1;
    if (state_block()) return 0;            /* already installed */

    u8 *state = (u8 *)z5_state_alloc(0x60u);
    u8 *cp    = (u8 *)z5_state_alloc(0x40u);
    u8 *msgs  = (u8 *)z5_state_alloc(MSG_COUNT * MSG_SIZE);
    u8 *scratch = (u8 *)z5_state_alloc(MSG_COUNT * MSG_SIZE);
    void *snd = find_task(SNDSRV_HANDLER);
    if (!state || !cp || !msgs || !scratch || !snd) return -1;

    /* Four times the clip: the engine loops the buffer at the clip's period, so
     * everything after the clip must be silence rather than heap contents. */
    u32 total = bytes * 4u;
    u8 *buf = (u8 *)z5_alloc(total, 0);
    if (!buf) return -1;
    for (u32 i = 0; i < total; i++) buf[i] = 0x80u;
    for (u32 i = 0; i < bytes; i++) buf[i] = ((const u8 *)pcm)[i];

    build_messages(msgs, (u32)buf, bytes);

    *(u32 *)(state + ST_MAGIC) = AUDIO_MAGIC;
    *(u32 *)(state + ST_TAG) = AUDIO_TAG;
    *(u32 *)(state + ST_SELF) = (u32)state;
    *(u32 *)(state + ST_PCM) = (u32)buf;
    *(u32 *)(state + ST_MSGS) = (u32)msgs;
    *(u32 *)(state + ST_SND) = (u32)snd;
    *(u32 *)(state + ST_SCRATCH) = (u32)scratch;
    *(u32 *)(state + ST_REQ) = 0;
    *(u32 *)(state + ST_LEN) = bytes;
    *(u32 *)(state + ST_CP) = (u32)cp;
    *(u32 *)(state + ST_MS) = bytes / 192u + 40u;   /* + 40 ms margin */

    *(u32 *)(cp + CP_MODE) = 0;
    *(u32 *)(cp + CP_DEADLINE) = 0;
    *(u32 *)(cp + CP_HANDLED) = 0;
    *(u32 *)(cp + CP_STOPS) = 0;
    *(u32 *)(cp + CP_STEP) = 0;
    *(u32 *)(cp + CP_NEXT) = 0;
    *(u32 *)(cp + CP_POSTED) = 0;

    volatile u32 *c = (volatile u32 *)MEDIA_STATE;
    c[0] = 0; c[1] = 0; c[2] = bytes; c[3] = bytes; c[4] = 0x108u;

    *(volatile u32 *)FW_STATE = (u32)state;

    void *blob = z5_img_copy_arm_ctl();
    if (!blob) return -1;

    /* Priority 0x30, not 0x14: this task spins with rot_rdq, and a smaller
     * number means higher priority in T-Kernel, so a spinning task above the UI
     * starves it. The proven build used 0x14 for the pump task and 0x30 here. */
    static const char name[8] = { 'Z','5','S','N','D','6','0', 0 };
    z5_task task;
    if (z5_task_create_arm(&task, name, 0x30u, blob) != 0) return -1;
    return 0;
}

void z5_audio_play(void)
{
    u32 s = state_block();
    if (s) *(volatile u32 *)(s + ST_REQ) += 1u;
}

void z5_audio_stop(void)
{
    u32 s = state_block();
    if (!s) return;
    u32 cp = *(volatile u32 *)(s + ST_CP);
    if (RAM_OK(cp)) *(volatile u32 *)(cp + CP_DEADLINE) = 1u;   /* due now */
}

u32 z5_audio_posted(void)
{
    u32 s = state_block();
    if (!s) return 0;
    u32 cp = *(volatile u32 *)(s + ST_CP);
    return RAM_OK(cp) ? *(volatile u32 *)(cp + CP_POSTED) : 0;
}

u32 z5_audio_stops(void)
{
    u32 s = state_block();
    if (!s) return 0;
    u32 cp = *(volatile u32 *)(s + ST_CP);
    return RAM_OK(cp) ? *(volatile u32 *)(cp + CP_STOPS) : 0;
}
