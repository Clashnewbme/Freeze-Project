/* Simple 16550 UART serial driver for debug output */
#include <stdint.h>

#define SERIAL_PORT 0x3F8

static inline void outb(unsigned short port, unsigned char val){
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned char inb(unsigned short port){
    unsigned char ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void serial_init(void){
    outb(SERIAL_PORT + 1, 0x00);    // Disable all interrupts
    outb(SERIAL_PORT + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    outb(SERIAL_PORT + 0, 0x03);    // Set divisor to 3 (38400 baud)
    outb(SERIAL_PORT + 1, 0x00);
    outb(SERIAL_PORT + 3, 0x03);    // 8 bits, no parity, one stop bit
    outb(SERIAL_PORT + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
    outb(SERIAL_PORT + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

static int serial_is_transmit_empty(){
    return inb(SERIAL_PORT + 5) & 0x20;
}

void serial_putc(char c){
    while(!serial_is_transmit_empty());
    outb(SERIAL_PORT, (unsigned char)c);
}

void serial_print(const char* s){
    for(int i=0; s[i]; ++i) serial_putc(s[i]);
}

int serial_available(void){
    return inb(SERIAL_PORT + 5) & 1;
}

char serial_getc(void){
    while(!(inb(SERIAL_PORT + 5) & 1));
    return inb(SERIAL_PORT);
}
