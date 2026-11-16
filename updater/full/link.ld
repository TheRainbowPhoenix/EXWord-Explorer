OUTPUT_ARCH(sh)
ENTRY(_start)
SECTIONS
{
    . = 0x8c18ff80;
    .header : { KEEP(*(.header)) }
    ASSERT(SIZEOF(.header) == 0x80, "UPDADN2 header must be 128 bytes")
    .text : { *(.text.start) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) }
    ASSERT(. < 0x8c190f00, "bootstrap too large for reserved 4 KiB page")
    . = 0x8c190ff0;
    .metadata : { LONG(0) LONG(0) LONG(0) LONG(0) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
    ASSERT(_start == 0x8c190000, "Unexpected UPDADN2 entry")
}
