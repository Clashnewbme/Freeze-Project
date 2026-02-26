// MULTIBOOT HEADER (REQUIRED)
// Use flags=3 (align modules, request memory info) and checksum = -(magic+flags)
__attribute__((section(".multiboot")))
const unsigned int multiboot_header[] = {
    0x1BADB002,
    0x00000003,
    -(0x1BADB002 + 0x00000003)
};

#include <stdint.h>

/* serial I/O (implemented in serial.c) */
extern void serial_putc(char c);
extern void serial_print(const char* s);
extern int serial_available(void);
extern char serial_getc(void);

/* simple outb for reset */
static inline void outb(unsigned short port, unsigned char val){
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

volatile uint16_t* vga = (uint16_t*)0xB8000;
int row = 0, col = 0;

/* default text color: green on black */
uint8_t color = 0x02;

void putc(char c){
    if(c=='\n'){
        row++;
        col = 0;
        /* mirror newline to serial (CRLF) */
        serial_putc('\r');
        serial_putc('\n');
        return;
    }

    if(row >= 25) row = 0;

    vga[row*80 + col] = (color << 8) | c;

    col++;
    if(col >= 80){ col = 0; row++; }

    /* mirror to serial so -nographic shows output */
    serial_putc(c);
}

void print(const char* s){ for(int i=0; s[i]; i++) putc(s[i]); }

void clear(){
    for(int i=0;i<80*25;i++) vga[i] = (color<<8) | ' ';
    row = 0; col = 0;
    /* also clear serial terminal if connected */
    serial_print("\x1b[2J\x1b[H");
}

// PORT INPUT
unsigned char inb(unsigned short port){
    unsigned char ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// SCANCODE MAP
char scancode_to_ascii(unsigned char sc){
    char map[128] = {
        0,27,'1','2','3','4','5','6','7','8','9','0','-','=',8,
        9,'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
        'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
        'z','x','c','v','b','n','m',',','.','/',0,'*',0,' '
    };
    if(sc < sizeof(map)) return map[sc];
    return 0;
}

/* Erase previous character on VGA and serial (backspace) */
void erase_last_char(){
    if(col > 0){
        col--;
        vga[row*80 + col] = (0x07 << 8) | ' ';
        serial_putc('\b'); serial_putc(' '); serial_putc('\b');
    } else if(row > 0){
        row--; col = 79;
        vga[row*80 + col] = (0x07 << 8) | ' ';
        serial_putc('\b'); serial_putc(' '); serial_putc('\b');
    }
}

// SERIAL input-aware line reader
void get_input(char* buffer){
    int i = 0;
    while(1){
        if(serial_available()){
            char c = serial_getc();
            if(c == '\r') c = '\n';
            if(c == '\n'){ buffer[i]=0; putc('\n'); return; }
            if(c==8 || c==127){ if(i>0){ i--; erase_last_char(); } continue; }
            if(i < 127){ buffer[i++] = c; putc(c); }
            continue;
        }

        unsigned char sc = inb(0x60);
        if(sc & 0x80) continue;
        char c = scancode_to_ascii(sc);
        if(!c) continue;
        if(c=='\n'){ buffer[i]=0; putc('\n'); return; }
        if(c==8 && i>0){ i--; erase_last_char(); continue; }
        if(i < 127){ buffer[i++] = c; putc(c); }
    }
}

// simple strcmp
int strcmp(const char* a,const char* b){ int i=0; while(a[i] && b[i]){ if(a[i]!=b[i]) return 0; i++; } return a[i]==b[i]; }

/* expose bss symbols from linker */
extern unsigned char __bss_start;
extern unsigned char __bss_end;

/* print hex value (32-bit) */
void print_hex(unsigned int v){
    const char* hex = "0123456789ABCDEF";
    char buf[9];
    for(int i=0;i<8;i++) buf[7-i] = hex[v & 0xF], v >>= 4;
    buf[8]=0;
    print("0x");
    print(buf);
}

int startswith(const char* s,const char* p){ int i=0; while(p[i]){ if(s[i]!=p[i]) return 0; i++; } return 1; }

// SHELL
void shell(){
    char buf[128];
    while(1){
        print("> ");
        get_input(buf);
        if(strcmp(buf,"help")){
            print("Commands: help clear about echo ls cat mem uname version reboot\n");
        } else if(strcmp(buf,"clear")){
            clear();
        } else if(strcmp(buf,"about")){
            print("Freeze OS v4\n");
        } else if(strcmp(buf,"ls")){
            print("kernel.bin\nboot/grub/grub.cfg\n");
        } else if(startswith(buf,"cat ")){
            char* fname = buf + 4;
            print("No filesystem: "); print(fname); putc('\n');
        } else if(strcmp(buf,"cat")){
            print("Usage: cat <file>\n");
        } else if(strcmp(buf,"mem")){
            print("BSS start: "); print_hex((unsigned int)&__bss_start); putc('\n');
            print("BSS end:   "); print_hex((unsigned int)&__bss_end); putc('\n');
        } else if(strcmp(buf,"uname")){
            print("FreezeOS v4 (i386)\n");
        } else if(strcmp(buf,"version")){
            print("FreezeOS version 0.4\n");
        } else if(strcmp(buf,"reboot")){
            print("Rebooting...\n"); outb(0x64,0xFE); for(;;); }
        else if(startswith(buf,"echo ")){
            print(buf + 5); putc('\n');
        } else if(strcmp(buf,"echo")){
            print("Type something:\n"); get_input(buf); print(buf); putc('\n');
        } else {
            print("Unknown command\n");
        }
    }
}

// ENTRY POINT
void kernel_main(void){
    clear();
    print("Freeze OS v4\n");
    print("Welcome!\n");
    print("Type 'help'\n\n");
    shell();
}
