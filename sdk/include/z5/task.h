/* z5sdk - RTOS task creation helpers (SMP T-Kernel 1.00.01 on EXPEED 6).
 *
 * ---------------------------------------------------------------------------
 * Round 318 CORRECTION: creating the task and starting it are TWO calls.
 *
 *   1. create with handler     0x1021BF64(obj, handler, descriptor)
 *        -> node queue init    0x101ED8B0(obj + 0x20, name)
 *        -> 0x10232862(obj, pump, descriptor)
 *             = tk_cre_tsk     TFN 0x80010100   (stub 0x10035FDC)
 *             + tk_chg_slt     TFN 0x800A0200   (stub 0x10035E6C), slice = 1
 *      Neither of those starts the task: it stays DORMANT.
 *
 *   2. start                   0x1021BF86(obj)
 *        -> 0x1023290C: r0 = obj->id
 *        -> tk_sta_tsk           TFN 0x80030200   (stub 0x10036440)
 *           with r1 = obj, i.e. stacd = obj. T-Kernel's setup_stacd() puts
 *           stacd into the task's r0, which is exactly what the pump expects
 *           (it uses r0 as the task object).
 *
 * The firmware always does 1 then 2 (23 call sites to 0x1021BF64, 10 to
 * 0x1021BF86; e.g. 0x104644BA then 0x104644E0). Earlier z5sdk versions did
 * only step 1, which is why the task object, task id and mailbox were all
 * valid, posts returned 0, and the handler still never ran.
 *
 * The T-Kernel 2.0 function-code table (tmp/tkernel_2/include/sys/svc/
 * tkfncd.h) matches the literals observed in the firmware exactly for the
 * task services, so the IDs above are read from the stubs, not guessed.
 * ---------------------------------------------------------------------------
 */
#ifndef Z5_TASK_H
#define Z5_TASK_H

#include "z5.h"

#define Z5_TASK_REG_LOOKUP  Z5_FN(void *(*)(u32), 0x1021C04A)

/* Raw kernel create: tk_cre_tsk + tk_chg_slt. Leaves the task DORMANT.      */
#define Z5_TASK_CREATE      Z5_FN(int (*)(void *, u32, void *), 0x10232862)

/* Firmware's own node-task wrapper: queue init + raw create (step 1).       */
#define Z5_TASK_CREATE_EX   Z5_FN(int (*)(void *, void *, void *), 0x1021BF64)

/* Start by task object: tk_sta_tsk(obj->id, obj) (step 2).                 */
#define Z5_TASK_START       Z5_FN(int (*)(void *), 0x1021BF86)

/* Node mailbox init the wrapper uses; 0x1019A10C is a *different* object.   */
#define Z5_TASK_QINIT_NODE  Z5_FN(void (*)(void *, void *), 0x101ED8B0)
#define Z5_TASK_QINIT       Z5_FN(void (*)(void *, void *), 0x1019A10C)

#define Z5_TASK_POST        Z5_FN(int (*)(void *, void *), 0x101ED942)

#define Z5_TASK_PUMP_NODE   0x1021BF45u
#define Z5_TASK_PUMP_MSG    0x1019969Du

/* Task-object field offsets (Nikon's app-layer object, NOT the kernel TCB). */
#define Z5_TCB_ID       0x00u
#define Z5_TCB_PUMP     0x04u
#define Z5_TCB_PRIO     0x08u
#define Z5_TCB_STACK    0x0Cu
#define Z5_TCB_NAME     0x18u
#define Z5_TCB_QUEUE    0x20u
#define Z5_TCB_HANDLER  0x2Cu
#define Z5_TCB_MSGSIZE  0x30u

typedef struct {
    void *obj;      /* task object, 0x100 bytes of alloc() memory          */
    void *blob;     /* handler blob, 0x60 bytes of alloc() memory         */
    u32   id;       /* T-Kernel task id after create                      */
    u32   queue;    /* mailbox id after create                            */
    s32   start_rc; /* tk_sta_tsk result; 0 = task is running             */
} z5_task;

/* Create a node-pump task and start it. name is 8 bytes (padded with NULs),
 * prio is a T-Kernel priority (0x14 = the SWSRV value), handler_blob must be
 * a position-independent function taking r0 = node[+8] and returning 0.
 * The Thumb bit is added automatically.
 * Returns the create result (0 on success); the start result is in
 * t->start_rc. */
int  z5_task_create(z5_task *t, const char name[8], u32 prio, void *handler_blob);

/* Raw create (Round 502): no mailbox, no shared pump. `entry` becomes the task's
 * own entry point (the kernel trampoline 0x10232859 does `ldr r1,[obj,#4]; bx r1`,
 * and the raw create stores our pointer at obj+4). The body therefore runs in a
 * real T-Kernel task context and may busy-poll the clock (z5_delay_ms) without
 * blocking the firmware's message pump. */
int  z5_task_create_raw(z5_task *t, const char name[8], u32 prio, void *entry);

/* Explicit start (needed after a task was created raw/dormant, or restarted
 * after it exited). Returns tk_sta_tsk's ER: 0 = running. */
int  z5_task_start(z5_task *t);

/* Post one node to the task mailbox: node[+4] = 2, node[+8] = payload. */
int  z5_task_post(z5_task *t, void *payload);

/* Find a task object by its 8-byte name (scans 0xC1800000-0xC3000000). */
void *z5_task_find(const char name[8]);

#define Z5_TCB_GET(tcb, off) (*(volatile u32 *)((u32)(tcb) + (off)))

#endif /* Z5_TASK_H */
