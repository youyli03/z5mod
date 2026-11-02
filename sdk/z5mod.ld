/* z5sdk layout: one contiguous blob, linked at 0.
 *
 * Position independence comes from r9 (see start.S), so the link address is
 * irrelevant; 0 keeps every delta small and makes accidental absolute
 * references easy to spot in the disassembly.
 *
 * Round 563: ".z5img.<name>" sections hold the code images that are copied
 * into alloc() memory and executed from there (see include/z5img.h). They are
 * pinned inside .text so they are always part of the module body.
 */
ENTRY(z5_entry)

SECTIONS
{
    . = 0;

    .text : {
        KEEP(*(.text.z5_entry))
        *(.text*)
        . = ALIGN(4);
        *(.z5img*)
        . = ALIGN(4);
        *(.rodata*)
    }

    . = ALIGN(4);
    .got : {
        _GLOBAL_OFFSET_TABLE_ = .;
        *(.got*)
        *(.data.rel*)
    }

    . = ALIGN(4);
    .data : {
        *(.data*)
        *(.bss*)
        *(COMMON)
    }

    z5_pic_delta = _GLOBAL_OFFSET_TABLE_ - z5_base;

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) }
}
