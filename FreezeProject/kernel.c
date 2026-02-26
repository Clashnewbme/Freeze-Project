
#include <stdint.h>

volatile uint16_t* vga = (uint16_t*)0xB8000;
int row=0,col=0;
uint8_t color=0x0F;

void putc(char c){
    if(c=='\n'){row++;col=0;return;}
    vga[row*80+col]=(color<<8)|c;
    col++; if(col>=80){col=0;row++;}
}

void print(const char* s){ for(int i=0;s[i];i++) putc(s[i]); }

void clear(){
    for(int i=0;i<80*25;i++) vga[i]=(0x07<<8)|' ';
    row=0;col=0;
}

// Read keyboard scancode (VERY basic)
unsigned char inb(unsigned short port){
    unsigned char ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

char scancode_to_ascii(unsigned char sc){
    char map[128] = {
        0,27,'1','2','3','4','5','6','7','8','9','0','-','=',8,
        9,'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
        'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
        'z','x','c','v','b','n','m',',','.','/',0,'*',0,' '
    };
    if(sc<sizeof(map)) return map[sc];
    return 0;
}

void get_input(char* buffer){
    int i=0;
    while(1){
        unsigned char sc = inb(0x60);
        if(sc & 0x80) continue; // key release ignore

        char c = scancode_to_ascii(sc);
        if(!c) continue;

        if(c=='\n'){
            buffer[i]=0;
            putc('\n');
            return;
        }

        buffer[i++]=c;
        putc(c);
    }
}

int strcmp(const char* a,const char* b){
    int i=0;
    while(a[i] && b[i]){
        if(a[i]!=b[i]) return 0;
        i++;
    }
    return a[i]==b[i];
}

void shell(){
    char buf[128];

    while(1){
        print("> ");
        get_input(buf);

        if(strcmp(buf,"help")){
            print("help clear about echo\n");
        } else if(strcmp(buf,"clear")){
            clear();
        } else if(strcmp(buf,"about")){
            print("Freeze OS v3\n");
        } else {
            print("Unknown\n");
        }
    }
}

void kernel_main(){
    clear();
    print("Freeze Project\n");
    print("Welcome\n");
    shell();
}
