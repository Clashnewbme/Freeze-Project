/* C runtime: clear BSS and call kernel_main */
#include <stdint.h>

extern void kernel_main(void);
extern unsigned char __bss_start;
extern unsigned char __bss_end;
extern void serial_init(void);
extern void serial_print(const char* s);

void c_entry(void){
    /* Clear BSS */
    unsigned char *b = &__bss_start;
    while(b < &__bss_end) *b++ = 0;

    /* initialize serial for debug output */
    serial_init();
    serial_print("[crt] C runtime initialized\n");

    /* jump to main kernel */
    kernel_main();

    /* If kernel returns, halt */
    for(;;) __asm__ volatile ("cli; hlt");
}
