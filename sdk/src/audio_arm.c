/* z5sdk - audio state machine driven by the ARM timer task (Round 563).
 *
 * WHY THIS EXISTS
 * ---------------
 * mktask356 posted all six engine messages in one burst from the pump handler.
 * That works while the stream stays open, but two things are impossible with
 * it: (a) closing the stream and opening it again later, (b) inserting a new
 * playback at an arbitrary moment. Both need the sequence to be driven by
 * something which keeps running between messages - this task.
 *
 * Also: an svc is a function call. Under AAPCS r0-r3 and ip are caller-saved,
 * so the inline asm declares them clobbered. Without that GCC keeps a live
 * constant (the fixed-word base) in r2 across the service call and the next
 * iteration dereferences garbage - that is what hung mktask389/390.
 *
 * STATE MACHINE (state kept in the control block, cp, 0x40 bytes)
 * --------------------------------------------------------------
 *   cp[0] mode      0 idle | 1 sending the 6-message sequence | 2 playing
 *   cp[1] deadline  when playing: tick at which to close
 *   cp[2] handled   last play request already taken
 *   cp[3] stops     how many closes we sent
 *   cp[4] step      next message index 0..6 (6 = sequence done)
 *   cp[5] next_tick next step time
 *   cp[6] posted    messages actually delivered (the "N" on screen)
 *   cp[7] renders   z5_main invocations (the "R" on screen)
 *
 * A play request is just "state+0x48 += 1" (done by the UI trigger and by the
 * cb1 hook). Taking a request resets step to 0, so the sequence always starts
 * with the close message: inserting a replay while playing is the same code
 * path as starting from idle.
 *
 * Messages come from the state block itself (s->msgs, s->snd at +0x2C/+0x30),
 * i.e. the six mktask356 messages, close first.
 */
#include "z5/z5.h"
#include "z5/svc.h"

typedef int (*post_msg_fn)(void *task, void *msg);

/* libsvc-style service call: ip = function code, svc #6.
 * r2/r3/ip declared clobbered - an svc is a function call (see header). */
static inline s32 ctl_svc(u32 tfn, u32 a1, u32 a2)
{
    register u32 r0 asm("r0") = a1;
    register u32 r1 asm("r1") = a2;
    __asm__ volatile ("mov   ip, %[tfn]\n\t"
                      "stmfd sp!, {r4}\n\t"
                      "add   r4, sp, #4\n\t"
                      "stmfd sp!, {lr}\n\t"
                      "svc   #6\n\t"
                      "ldmfd sp!, {lr}\n\t"
                      "ldmfd sp!, {r4}"
                      : "+r"(r0), "+r"(r1)
                      : [tfn]"r"(tfn)
                      : "r2", "r3", "ip", "cc", "r4", "memory");
    return (s32)r0;
}

#define CTL_FW_STATE  0xC18117C8u   /* play state block        */
#define CTL_CP        0x50u         /* state + 0x50: control block      */
#define CTL_MS        0x54u         /* state + 0x54: clip length in ms  */
#define CTL_TICK      0xC18EB388u   /* firmware ms clock      */
#define CTL_REQ       0x48u         /* state+0x48: play request */
#define CTL_MSGS      0x2Cu         /* state+0x2C: messages   */
#define CTL_SND       0x30u         /* state+0x30: SNDSRV task */
#define CTL_BUSY      0x20u         /* state+0x20: hook busy  */
#define STEP_MS       20u           /* gap between messages   */
#define NMESSAGES     6u

#define RAM_OK(p)     ((p) >= 0xC1800000u && (p) < 0xC8700000u)

__attribute__((section(".z5img.armctl"), noinline, used))
u32 z5_ctl_addr(void)
{
    u32 r;
    __asm__ volatile ("adr %0, z5_ctl_main" : "=r"(r));
    return r;
}

__attribute__((section(".z5img.armctl"), noinline, used))
void z5_ctl_main(void *arg)
{
    (void)arg;
    for (;;) {
        u32 s  = *(volatile u32 *)CTL_FW_STATE;
        u32 cp = RAM_OK(s) ? *(volatile u32 *)(s + CTL_CP) : 0u;

        if (RAM_OK(cp) && RAM_OK(s)) {
            u32 now  = *(volatile u32 *)CTL_TICK;
            u32 req  = *(volatile u32 *)(s + CTL_REQ);
            u32 mode = *(volatile u32 *)(cp + 0x00u);
            u32 step = *(volatile u32 *)(cp + 0x10u);
            u32 nt   = *(volatile u32 *)(cp + 0x14u);
            u32 dl   = *(volatile u32 *)(cp + 0x04u);
            u32 msgs = *(volatile u32 *)(s + CTL_MSGS);
            u32 snd  = *(volatile u32 *)(s + CTL_SND);

            if (req != *(volatile u32 *)(cp + 0x08u)) {
                /* new play request: (re)start the sequence from the close */
                *(volatile u32 *)(cp + 0x08u) = req;
                *(volatile u32 *)(s + CTL_BUSY) = 0u;
                mode = 1u;
                step = 0u;
                nt   = now;
            }

            if (mode == 1u) {
                if ((s32)(now - nt) >= 0) {
                    if (step < NMESSAGES && RAM_OK(msgs) && RAM_OK(snd)) {
                        post_msg_fn fn = (post_msg_fn)(0x1021BF8Cu | 1u);
                        fn((void *)snd, (void *)(msgs + step * 0x60u));
                        *(volatile u32 *)(cp + 0x18u) += 1u;   /* posted */
                        step++;
                        nt = now + STEP_MS;
                    } else {
                        mode = 2u;
                        step = 0u;
                        dl = now + *(volatile u32 *)(s + CTL_MS);
                    }
                }
            } else if (mode == 2u) {
                if ((s32)(now - dl) >= 0) {
                    if (RAM_OK(msgs) && RAM_OK(snd)) {
                        post_msg_fn fn = (post_msg_fn)(0x1021BF8Cu | 1u);
                        fn((void *)snd, (void *)msgs);         /* close */
                        *(volatile u32 *)(cp + 0x18u) += 1u;
                        *(volatile u32 *)(cp + 0x0Cu) += 1u;   /* stops  */
                    }
                    mode = 0u;
                }
            }

            *(volatile u32 *)(cp + 0x00u) = mode;
            *(volatile u32 *)(cp + 0x10u) = step;
            *(volatile u32 *)(cp + 0x14u) = nt;
            *(volatile u32 *)(cp + 0x04u) = dl;
        }
        ctl_svc(Z5_TFN_ROT_RDQ, 0u, 0u);
    }
}

__attribute__((section(".z5img.armctl"), noinline, used))
void z5_ctl_main_end(void) { }

__attribute__((section(".z5img.armctl"), noinline, used))
u32 z5_ctl_end_addr(void)
{
    u32 r;
    __asm__ volatile ("adr %0, z5_ctl_main_end" : "=r"(r));
    return r;
}
