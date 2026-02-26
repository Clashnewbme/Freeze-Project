
Freeze Project

Build:
gcc -ffreestanding -m32 -c kernel.c -o kernel.o
ld -m elf_i386 -T linker.ld -o kernel.bin kernel.o

mkdir -p iso/boot/grub
cp kernel.bin iso/boot/
cp grub/grub.cfg iso/boot/grub/

grub-mkrescue -o freeze.iso iso

Run:
qemu-system-i386 -cdrom freeze.iso
