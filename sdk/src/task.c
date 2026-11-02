/* z5sdk - RTOS task creation helpers (Round 318: create + start). */
#include "z5/task.h"

static u32 name_lo(const char n[8])
{
    return (u32)(u8)n[0] | ((u32)(u8)n[1] << 8) | ((u32)(u8)n[2] << 16) | ((u32)(u8)n[3] << 24);
}

static u32 name_hi(const char n[8])
{
    return (u32)(u8)n[4] | ((u32)(u8)n[5] << 8) | ((u32)(u8)n[6] << 16) | ((u32)(u8)n[7] << 24);
}

void *z5_task_find(const char name[8])
{
    u32 lo = name_lo(name), hi = name_hi(name);
    for (u32 a = 0xC1800000u; a < 0xC3000000u; a += 4) {
        const volatile u32 *p = (const volatile u32 *)a;
        if (p[0] == lo && p[1] == hi) return (void *)(a - Z5_TCB_NAME);
    }
    return 0;
}

/* Build a task object the way the firmware's own wrapper does:
 *   queue init (0x101ED8B0) + raw create (0x10232862), then start it.
 * The raw create alone leaves the task dormant (Round 318). */
int z5_task_create(z5_task *t, const char name[8], u32 prio, void *handler_blob)
{
    if (!t) return -1;
    u8 *obj  = (u8 *)z5_alloc(0x100, 0);
    u8 *args = (u8 *)z5_alloc(0x20, 0);
    if (!obj || !args) return -1;
    for (u32 i = 0; i < 0x100; i++) obj[i] = 0;
    for (u32 i = 0; i < 0x20; i++) args[i] = 0;

    /* copy the descriptor the boot code uses for its own SWSRV task */
    void *desc = Z5_TASK_REG_LOOKUP(0x40u);
    if (desc) {
        for (u32 i = 0; i < 0x1Cu; i++) args[i] = ((volatile u8 *)desc)[i];
    }
    *(u32 *)(args + 4) = prio;
    for (u32 i = 0; i < 8; i++) args[0x10 + i] = (u8)name[i];

    u32 h = (u32)handler_blob;
    if (!(h & 1u)) h |= 1u;             /* Thumb: the pump blx's this */

    *(u32 *)(obj + Z5_TCB_MSGSIZE) = 0x1FFu;

    /* Step 1: firmware node-task wrapper (queue init 0x101ED8B0 + create). */
    int rc = Z5_TASK_CREATE_EX(obj, (void *)h, args);

    t->obj = obj;
    t->blob = handler_blob;
    t->id = 0;
    t->queue = 0;
    t->start_rc = -1;
    if (rc == 0) {
        t->id = Z5_TCB_GET(obj, Z5_TCB_ID);
        t->queue = Z5_TCB_GET(obj, Z5_TCB_QUEUE);
        /* Step 2: start it. Without this the pump never runs. */
        t->start_rc = Z5_TASK_START(obj);
    }
    z5_free(args);
    return rc;
}

int z5_task_start(z5_task *t)
{
    if (!t || !t->obj) return -1;
    return Z5_TASK_START(t->obj);
}

int z5_task_post(z5_task *t, void *payload)
{
    if (!t || !t->obj) return -1;
    u8 *node = (u8 *)z5_alloc(0x20, 0);
    if (!node) return -1;
    for (u32 i = 0; i < 0x20; i++) node[i] = 0;
    *(u32 *)(node + 4) = 2u;
    *(u32 *)(node + 8) = (u32)payload;
    return Z5_TASK_POST((u8 *)t->obj + Z5_TCB_QUEUE, node);
}

/* Raw create: no queue/mailbox, our entry becomes obj+4 (the field the kernel
 * trampoline 0x10232859 jumps through). Verified on hardware in Round 502 -
 * the body runs and keeps running in its own task context. */
static int raw_create(z5_task *t, const char name[8], u32 prio, void *entry, int thumb)
{
    if (!t) return -1;
    u8 *obj  = (u8 *)z5_alloc(0x100, 0);
    u8 *args = (u8 *)z5_alloc(0x20, 0);
    if (!obj || !args) return -1;
    for (u32 i = 0; i < 0x100; i++) obj[i] = 0;
    for (u32 i = 0; i < 0x20; i++) args[i] = 0;

    void *desc = Z5_TASK_REG_LOOKUP(0x40u);
    if (desc) {
        for (u32 i = 0; i < 0x1Cu; i++) args[i] = ((volatile u8 *)desc)[i];
    }
    *(u32 *)(args + 4) = prio;
    for (u32 i = 0; i < 8; i++) args[0x10 + i] = (u8)name[i];

    u32 e = (u32)entry;
    if (thumb) {
        if (!(e & 1u)) e |= 1u;             /* Thumb: the kernel bx's this */
    } else {
        e &= ~1u;                           /* ARM: a clear low bit switches state */
    }
    *(u32 *)(obj + Z5_TCB_MSGSIZE) = 0x1FFu;

    int rc = Z5_TASK_CREATE(obj, e, (void *)args);
    t->obj = obj;
    t->blob = entry;
    t->id = 0;
    t->queue = 0;
    t->start_rc = -1;
    if (rc == 0) {
        t->id = Z5_TCB_GET(obj, Z5_TCB_ID);
        t->start_rc = Z5_TASK_START(obj);
    }
    z5_free(args);
    return rc;
}

int z5_task_create_raw(z5_task *t, const char name[8], u32 prio, void *entry)
{
    return raw_create(t, name, prio, entry, 1);
}

int z5_task_create_arm(z5_task *t, const char name[8], u32 prio, void *entry)
{
    return raw_create(t, name, prio, entry, 0);
}
