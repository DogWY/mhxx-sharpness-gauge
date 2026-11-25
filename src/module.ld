ENTRY(_start)
PHDRS {
    text PT_LOAD FLAGS(5);
    ro PT_LOAD FLAGS(4);
    rw PT_LOAD FLAGS(6);
}
SECTIONS {
    . = 0;
    .text : { *(.text.start) . = 0x100; *(.landing.paired) . = 0x5100; *(.landing.solo) *(.text*) } :text
    . = ALIGN(0x1000);
    .rodata : { *(.rodata*) } :ro
    . = ALIGN(0x1000);
    .data : { *(.data*) } :rw
    .bss : { bss_start = .; *(.bss*) . = ALIGN(0x1000); bss_end = .; } :rw
    /DISCARD/ : { *(.ARM.attributes) *(.comment) }
}
