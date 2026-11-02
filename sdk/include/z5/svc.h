/* z5sdk - ARM-state T-Kernel service trampolines (Round 563).
 *
 * The firmware's service stubs do "bx pc" to reach ARM state before the svc,
 * and the kernel's SVC entry assumes an ARM caller, so inline `svc` from Thumb
 * C code is NOT safe. The trampolines are assembled as ARM in src/imgarm.S,
 * copied into alloc() memory and called through a function pointer (the blx is
 * the Thumb->ARM switch):
 *
 *     void *mbx = z5_img_copy_arm_snd_mbx();
 *     void *rdq = z5_img_copy_arm_rot_rdq();
 *     ((z5_snd_mbx_fn)mbx)(mbx_id, node);
 *     ((z5_void_fn)rdq)();
 *
 * DO NOT take the address of z5_arm_snd_mbx / z5_arm_rot_rdq from C: module
 * code is -fpic, so a global's address goes through the GOT and yields the
 * link-time address, not the runtime one. Use the accessors below (they are
 * `adr`, i.e. PC-relative) or, better, the two copy wrappers in z5img.h.
 *
 * Function codes (the firmware's own literals, see NOTES 9.1/9.2):
 *   CRE_TSK 0x80010100   STA_TSK 0x80030200   CHG_SLT 0x800A0200
 *   SND_MBX 0x80340200   ROT_RDQ 0x800B0100
 */
#ifndef Z5_SVC_H
#define Z5_SVC_H

#include "z5.h"

#define Z5_TFN_CRE_TSK  0x80010100u
#define Z5_TFN_STA_TSK  0x80030200u
#define Z5_TFN_CHG_SLT  0x800A0200u
#define Z5_TFN_SND_MBX  0x80340200u
#define Z5_TFN_ROT_RDQ  0x800B0100u

/* Runtime addresses of the trampolines (spelled with adr in imgarm.S). */
u32 z5_imgarm_snd_mbx_addr(void);
u32 z5_imgarm_snd_mbx_end_addr(void);
u32 z5_imgarm_rot_rdq_addr(void);
u32 z5_imgarm_rot_rdq_end_addr(void);

typedef void (*z5_snd_mbx_fn)(u32, void *);
typedef void (*z5_void_fn)(void);

#endif /* Z5SVC_H */
