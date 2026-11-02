/* z5sdk entry stub.
 *
 * The loader (stage0 v6) copies the module body to an alloc() block and calls
 * the first byte with r0 = context. The block address changes every run, so the
 * body must be position independent.
 *
 * We compile the C side with
 *     -fpic -msingle-pic-base -mpic-register=r9 -mpic-data-is-text-relative
 * which makes every data access an offset from r9. The stub therefore only has
 * to put the runtime address of the PIC base into r9; the offset itself is a
 * link-time constant (a delta between two symbols), so no relocation table and
 * no runtime patching are needed.
 *
 * r4-r11 belong to the caller: they are saved here and restored before return.
 */
    .syntax unified
    .thumb
    /* own section so the linker script can pin it at offset 0 regardless of
     * how the other objects happen to be ordered on the command line */
    .section .text.z5_entry, "ax", %progbits

    .global z5_entry
    .global z5_base                 /* exported so the linker script can use it */
    .type   z5_entry, %function
    .thumb_func
z5_entry:
z5_base:                        /* plain label: adr must not add the Thumb bit */
    push    {r4-r11, lr}
    mov     r4, r0                  /* keep the context pointer */
    adr     r9, z5_base             /* runtime address of this stub */
    ldr     r12, =z5_pic_delta      /* (_GLOBAL_OFFSET_TABLE_ - z5_base) */
    add     r9, r9, r12             /* r9 = runtime PIC base */
    mov     r0, r4
    bl      z5_main
    pop     {r4-r11, pc}
    .size   z5_entry, . - z5_entry

    .ltorg
