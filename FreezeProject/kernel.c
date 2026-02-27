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

// helper that executes a single command string (no prompt)
void handle_command(char *buf){
    if(strcmp(buf,"help")){
        print("=== SYSTEM ===\n");
        print("uname, uptime, date, id, who, ps, top, lsmod, dmesg, systemctl, shutdown\n");
        print("=== TEXT ===\n");
        print("cat, echo, grep, sed, awk, wc, head, tail, more, less\n");
        print("=== FILE ===\n");
        print("ls, pwd, file, stat, chmod, chown, ln, mount, umount, df, du\n");
        print("=== PROCESS ===\n");
        print("kill, bg, fg, jobs, wait, exit, sleep\n");
        print("=== USER ===\n");
        print("useradd, userdel, passwd, groups, sudo\n");
        print("=== NETWORK ===\n");
        print("ifconfig, ping, ssh, scp, netstat, curl, wget\n");
        print("=== DEV ===\n");
        print("gcc, make, git, bash, sh, man, which, whereis\n");
        print("=== OTHER ===\n");
        print("clear, about, version, info, test, reboot, true, false\n");
    } else if(strcmp(buf,"clear")){
        clear();
    } else if(strcmp(buf,"-r")){
        print("Safety implementations deny this action.\n");
        print_hex((unsigned int)&__bss_start); print(" - "); print_hex((unsigned int)&__bss_end); print("\n");
    } else if(strcmp(buf,"about")){
        print("The FreezeOS Is an operating system created by Clashnewbme.\n");
    } else if(strcmp(buf,"fork while forking")){
         print("Forking while forking...\n");
         print("Forking while forking...\n"); outb(0x64,0xFE); for(;;);
    } else if(strcmp(buf,"version")){
        print("Freeze Project 0.5\n");
    } else if(strcmp(buf,"uname")){
        print("Freeze Project 0.5, SMP i386 GNU\n");
    } else if(strcmp(buf,"uptime")){
        print("05:00:00 up 12 days, 3:45, 1 user, load average: 0.15, 0.10, 0.08\n");
    } else if(strcmp(buf,"date")){
        print("Wed Feb 26 05:00:00 UTC 2026\n");
    } else if(strcmp(buf,"id")){
        print("uid=0(root) gid=0(root) groups=0(root),4(adm),27(sudo)\n");
    } else if(strcmp(buf,"who")){
        print("root     pts/0        2026-02-26 05:00 (:0)\n");
    } else if(strcmp(buf,"ps")){
        print("PID USER COMMAND\n1   root kernel\n2   root systemd\n");
    } else if(strcmp(buf,"top")){
        print("PID %%CPU %%MEM COMMAND\n1   5.2  12.5 kernel\n2   1.1  8.3  systemd\n");
    } else if(strcmp(buf,"lsmod")){
        print("Module       Size\nserial_core  2048\n");
    } else if(strcmp(buf,"dmesg")){
        print("[0.000000] Booting Freeze Project v4\n[0.001000] VGA buffer initialized\n");
    } else if(strcmp(buf,"systemctl")){
        print("Usage: systemctl [start|stop|status|restart] <service>\n");
    } else if(strcmp(buf,"shutdown")){
        print("Shutting down...\n"); outb(0x64,0xFE); for(;;);
    } else if(strcmp(buf,"ls")){
        print("boot/  kernel.bin  grub/  README.md\n");
    } else if(strcmp(buf,"pwd")){
        print("/root\n");
    } else if(strcmp(buf,"file")){
        print("kernel.bin: ELF 32-bit LSB executable\n");
    } else if(strcmp(buf,"stat")){
        print("File: kernel.bin Size: 10144 Blocks: 20 Inode: 12345\n");
    } else if(strcmp(buf,"chmod")){
        print("File permissions changed\n");
    } else if(strcmp(buf,"chown")){
        print("File owner changed\n");
    } else if(strcmp(buf,"ln")){
        print("Creating symlink...\n");
    } else if(strcmp(buf,"mount")){
        print("/ on /dev/sda1 type ext4\n");
    } else if(strcmp(buf,"umount")){
        print("Unmounting filesystem...\n");
    } else if(strcmp(buf,"df")){
        print("Filesystem Size Used Avail %%Use Mounted on\n/dev/sda1 10G  2G   8G   20%  /\n");
    } else if(strcmp(buf,"du")){
        print("4.1M  .\n");
    } else if(strcmp(buf,"cat")){
        print("Usage: cat <file>\n");
    } else if(startswith(buf,"cat ")){
        print("Cannot read file: no filesystem\n");
    } else if(strcmp(buf,"echo")){
        print("Type something:\n"); get_input(buf); print(buf); putc('\n');
    } else if(startswith(buf,"echo ")){
        print(buf + 5); putc('\n');
    } else if(strcmp(buf,"grep")){
        print("Usage: grep <pattern> <file>\n");
    } else if(strcmp(buf,"sed")){
        print("Usage: sed 's/old/new/' <file>\n");
    } else if(strcmp(buf,"awk")){
        print("Usage: awk '{print $1}' <file>\n");
    } else if(strcmp(buf,"wc")){
        print("Usage: wc [lines] [words] [chars] <file>\n");
    } else if(strcmp(buf,"head")){
        print("Usage: head -n <lines> <file>\n");
    } else if(strcmp(buf,"tail")){
        print("Usage: tail -n <lines> <file>\n");
    } else if(strcmp(buf,"more") || strcmp(buf,"less")){
        print("Usage: more <file>\n");
    } else if(strcmp(buf,"kill")){
        print("Usage: kill <pid>\n");
    } else if(strcmp(buf,"bg")){
        print("No background jobs\n");
    } else if(strcmp(buf,"fg")){
        print("No foreground jobs\n");
    } else if(strcmp(buf,"jobs")){
        print("No jobs\n");
    } else if(strcmp(buf,"wait")){
        print("Waiting for child process...\n");
    } else if(strcmp(buf,"sleep")){
        print("Sleeping...\n");
    } else if(strcmp(buf,"exit")){
        print("Exiting shell\n");
    } else if(strcmp(buf,"useradd")){
        print("Usage: useradd <username>\n");
    } else if(strcmp(buf,"userdel")){
        print("Usage: userdel <username>\n");
    } else if(strcmp(buf,"passwd")){
        print("Changing password...\n");
    } else if(strcmp(buf,"groups")){
        print("root adm sudo\n");
    } else if(strcmp(buf,"ifconfig")){
        print("lo: 127.0.0.1 netmask 255.0.0.0\neth0: 192.168.1.100 netmask 255.255.255.0\n");
    } else if(strcmp(buf,"ping")){
        print("ping: ICMP echo request not implemented\n");
    } else if(strcmp(buf,"ssh")){
        print("Usage: ssh <host>\n");
    } else if(strcmp(buf,"scp")){
        print("Usage: scp <file> <host>:<path>\n");
    } else if(strcmp(buf,"netstat")){
        print("Active Internet connections\nProto Local Address Foreign Address State\n");
    } else if(strcmp(buf,"curl") || strcmp(buf,"wget")){
        print("HTTP client not available\n");
    } else if(strcmp(buf,"gcc")){
        print("gcc (Freeze Project 0.50)\n");
    } else if(strcmp(buf,"make")){
        print("GNU Make 4.3\n");
    } else if(strcmp(buf,"git")){
        print("git version 2.34.1\n");
    } else if(strcmp(buf,"bash")){
        print("GNU bash, version 5.1.0\n");
    } else if(strcmp(buf,"sh")){
        print("POSIX shell\n");
    } else if(strcmp(buf,"man")){
        print("Manual page: see 'help' for available commands\n");
    } else if(strcmp(buf,"which")){
        print("Usage: which <command>\n");
    } else if(strcmp(buf,"whereis")){
        print("Usage: whereis <command>\n");
    } else if(strcmp(buf,"true")){
        print("\n");
    } else if(strcmp(buf,"false")){
        print("\n");
    } else if(strcmp(buf,"info") || strcmp(buf,"kernel") || strcmp(buf,"test")){
        print("Freeze OS v4 - Advanced Kernel Shell\n");
        print("Compiled with embedded 50+ Unix commands\n");
     } else if(strcmp(buf,"FreezeOS") || strcmp(buf,"freezeos") || strcmp(buf,"Freeze") || strcmp(buf,"freeze")){
         print("Freeze\n");
    } else if(strcmp(buf,":(){:|:&};:")){
        print("Forking :(){:|:&};:...\n");
        print("Forking :(){:|:&};:...\n"); outb(0x64,0xFE); for(;;);
    } else if(strcmp(buf,"Import /chkrootkit/*")){
        print("Denied\n");
        print("Restarting System\n"); outb(0x64,0xFE); for(;;);
    } else if(strcmp(buf,"reboot")){
        print("Rebooting...\n"); outb(0x64,0xFE); for(;;);
    } else {
        print("Command not found. Type 'help' for available commands.\n");
    }
}

// SHELL
void shell(){
    char buf[128];
    while(1){
        print("> ");
        get_input(buf);
        if(startswith(buf,"sudo ")){
            print("[sudo] ");
            char *rest = buf + 5;
            handle_command(rest);
        } else {
            handle_command(buf);
        }
    }
}

// ENTRY POINT
void kernel_main(void){
    clear();
    print("\033[96m=== \033[95mFreeze Project\033[96m ===\033[0m\n");
    print("\033[92mWelcome!\033[0m\n");
    print("\033[93mType 'help' for available commands\033[0m\n\n");
    print("\033[94m--------------------------------\033[0m\n");
    print("\033[92mCurrently: \033[93mVersion-0.5\033[0m\n");

    shell();
}

