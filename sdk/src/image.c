/* z5sdk - executable image helpers. See include/z5img.h for the rationale. */
#include "z5/image.h"

void z5_flush_icache(void)
{
    register u32 z asm("r0") = 0;
    __asm__ volatile ("dsb sy\n\tmcr p15, 0, %0, c7, c5, 0\n\tdsb sy\n\tisb sy"
                      :: "r"(z));
}

u32 z5_img_size(const void *start, const void *end)
{
    u32 a = (u32)start & ~1u;
    u32 b = (u32)end & ~1u;
    return b - a;
}

static void *img_copy(u32 *dst, const void *start, const void *end, int thumb)
{
    const u8 *src = (const u8 *)((u32)start & ~1u);
    u8 *d = (u8 *)dst;
    u32 len = z5_img_size(start, end);
    u32 i;

    if (!d || !len) return 0;
    for (i = 0; i < len; i++) d[i] = src[i];
    z5_flush_icache();
    return thumb ? (void *)((u32)d | 1u) : (void *)d;
}

void *z5_img_copy_to(void *dst, const void *start, const void *end)
{
    return img_copy((u32 *)dst, start, end, 1);
}

void *z5_img_copy_to_arm(void *dst, const void *start, const void *end)
{
    return img_copy((u32 *)dst, start, end, 0);
}

/* +0x1F so a literal-pool tail never shares a cache line with the next
 * allocation; z5_alloc returns 8-byte aligned blocks, i.e. 4-aligned. */
static u8 *img_alloc(const void *start, const void *end)
{
    u32 len = z5_img_size(start, end);
    if (!len) return 0;
    return (u8 *)z5_alloc(len + 0x1Fu, 0);
}

void *z5_img_copy(const void *start, const void *end)
{
    u8 *d = img_alloc(start, end);
    return d ? z5_img_copy_to(d, start, end) : 0;
}

void *z5_img_copy_arm(const void *start, const void *end)
{
    u8 *d = img_alloc(start, end);
    return d ? z5_img_copy_to_arm(d, start, end) : 0;
}

/* The ARM trampolines are globals in another object: taking their address from
 * PIC C goes through the GOT and yields the LINK-TIME address (0x11f8), which
 * is not mapped at runtime - the first version of this code died exactly
 * there. src/imgarm.S hands out runtime addresses with `adr` instead. */
void *z5_img_copy_arm_snd_mbx(void)
{
    u32 a = z5_imgarm_snd_mbx_addr();
    u32 b = z5_imgarm_snd_mbx_end_addr();
    return z5_img_copy_arm((void *)a, (void *)b);
}

void *z5_img_copy_arm_rot_rdq(void)
{
    u32 a = z5_imgarm_rot_rdq_addr();
    u32 b = z5_imgarm_rot_rdq_end_addr();
    return z5_img_copy_arm((void *)a, (void *)b);
}

/* The ARM control task lives in src/ctl_arm.c (compiled -marm). Its address
 * comes from adr-based accessors in the same section for the same reason as
 * the trampolines: a global's address in PIC code is a link-time value. */
void *z5_img_copy_arm_ctl(void)
{
    u32 a = z5_ctl_addr();
    u32 b = z5_ctl_end_addr();
    return z5_img_copy_arm((void *)a, (void *)b);
}
