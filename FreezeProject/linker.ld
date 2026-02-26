ENTRY(_start)

SECTIONS {
    . = 1M;

    .multiboot : {
        *(.multiboot)
    }

    .text : {
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    /* mark start of BSS */
    __bss_start = .;
    .bss : {
        *(.bss*)
    }
    __bss_end = .;
}
