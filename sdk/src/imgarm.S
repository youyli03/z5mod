/* z5sdk - ARM-state T-Kernel service trampolines (Round 563).
 *
 * WHY ARM
 * -------
 * A T-Kernel service is entered with
 *      ip = 0x80<service><argc>00        ; function code
 *      svc #6
 * and the kernel's entry code (cpu_init.c: define_inthdr(SWI_SVC, call_entry))
 * expects an ARM-state caller: the firmware's own stubs are Thumb wrappers that
 * do "bx pc" to switch to ARM before the svc (0x10036440 is one of them, see
 * NOTES 9.2), and the libsvc sources it was built from are plain ARM with
 * "stmfd sp!, {r4} / add r4, sp, #4 / stmfd sp!, {lr}" around the svc.
 * Issuing svc from Thumb is therefore not safe, and a C task body cannot use
 * inline svc. These trampolines are assembled as ARM, copied into alloc()
 * memory and called through a function pointer: the Thumb->ARM switch is the
 * blx, exactly as the firmware does it.
 *
 * ADDRESSING (Round 563, learned the hard way)
 * --------------------------------------------
 * Module code is compiled -fpic, so taking the address of a GLOBAL symbol goes
 * through the GOT and yields the *link-time* address (0x11f8...), not the
 * runtime one - the first version of this code copied from there and the
 * camera died on the first entry. The accessors below hand out runtime
 * addresses with `adr` (PC-relative by construction); C must go through
 * z5_img_copy_arm_snd_mbx()/z5_img_copy_arm_rot_rdq() in img.c.
 */
    .syntax unified
    .arm

    .section .z5img.arm, "ax", %progbits
    .balign 4

/* void z5_arm_snd_mbx(u32 mbx_id, void *msg) */
    .global z5_arm_snd_mbx
    .type   z5_arm_snd_mbx, %function
z5_arm_snd_mbx:
    stmfd   sp!, {r4}
    add     r4, sp, #4
    stmfd   sp!, {lr}
    ldr     ip, =0x80340200          /* TFN_SND_MBX */
    svc     #6
    ldmfd   sp!, {lr}
    ldmfd   sp!, {r4}
    bx      lr
    .ltorg
    .global z5_arm_snd_mbx_end
z5_arm_snd_mbx_end:

/* void z5_arm_rot_rdq(void) */
    .balign 4
    .global z5_arm_rot_rdq
    .type   z5_arm_rot_rdq, %function
z5_arm_rot_rdq:
    stmfd   sp!, {r4}
    add     r4, sp, #4
    stmfd   sp!, {lr}
    ldr     ip, =0x800B0100          /* TFN_ROT_RDQ */
    svc     #6
    ldmfd   sp!, {lr}
    ldmfd   sp!, {r4}
    bx      lr
    .ltorg
    .global z5_arm_rot_rdq_end
z5_arm_rot_rdq_end:

/* --- runtime addresses (never take &symbol from C: GOT!) ---------------- */

    .global z5_imgarm_snd_mbx_addr
    .type   z5_imgarm_snd_mbx_addr, %function
z5_imgarm_snd_mbx_addr:
    adr     r0, z5_arm_snd_mbx
    bx      lr

    .global z5_imgarm_snd_mbx_end_addr
    .type   z5_imgarm_snd_mbx_end_addr, %function
z5_imgarm_snd_mbx_end_addr:
    adr     r0, z5_arm_snd_mbx_end
    bx      lr

    .global z5_imgarm_rot_rdq_addr
    .type   z5_imgarm_rot_rdq_addr, %function
z5_imgarm_rot_rdq_addr:
    adr     r0, z5_arm_rot_rdq
    bx      lr

    .global z5_imgarm_rot_rdq_end_addr
    .type   z5_imgarm_rot_rdq_end_addr, %function
z5_imgarm_rot_rdq_end_addr:
    adr     r0, z5_arm_rot_rdq_end
    bx      lr
