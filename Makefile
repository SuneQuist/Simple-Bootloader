# Linux host GNU binutils/GCC, QEMU. No NASM and no cross-compiler required.
AS = as
CC = gcc
LD = ld
OBJCOPY = objcopy

CFLAGS = -m64 -std=c11 -Os -ffreestanding -fno-builtin \
         -fno-stack-protector -fno-stack-check -fno-pie -fno-pic \
         -fno-asynchronous-unwind-tables -fno-unwind-tables \
         -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
         -Wall -Wextra

.PHONY: all run clean
all: kernel.img

boot.o: boot.s
	$(AS) --64 $< -o $@

boot.bin: boot.o
	$(LD) -m elf_x86_64 --oformat binary -Ttext 0x7c00 $< -o $@
	@test "$$(wc -c < $@)" -eq 512 || (echo 'ERROR: boot sector must be exactly 512 bytes' && exit 1)

start.o: start.s
	$(AS) --64 $< -o $@

kernel.o: kernel.c
	$(CC) $(CFLAGS) -c $< -o $@

stage2.elf: start.o kernel.o linker.ld
	$(LD) -m elf_x86_64 -T linker.ld start.o kernel.o -o $@

stage2.bin: stage2.elf
	$(OBJCOPY) -O binary $< $@
	@test "$$(wc -c < $@)" -le 8192 || (echo 'ERROR: stage2 exceeds 16 sectors (8192 bytes)' && exit 1)

kernel.img: boot.bin stage2.bin
	dd if=/dev/zero of=$@ bs=512 count=2880 status=none
	dd if=boot.bin of=$@ conv=notrunc status=none
	dd if=stage2.bin of=$@ bs=512 seek=1 conv=notrunc status=none

run: kernel.img
	qemu-system-x86_64 -drive file=kernel.img,format=raw,if=floppy -boot a

clean:
	rm -f boot.o boot.bin start.o kernel.o stage2.elf stage2.bin kernel.img

