    .text
    .global _start
    .type _start, @function
_start:
    /* set up a simple stack at 2MB */
    mov $0x200000, %esp
    mov $0x200000, %ebp
    /* call the C runtime entry which will clear BSS and call kernel_main */
    call c_entry
    /* If kernel returns, halt */
hang:
    cli
    hlt
    jmp hang
