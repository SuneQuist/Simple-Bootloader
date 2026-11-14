AS = as
CC = gcc
LD = ld
OBJCOPY = objcopy
QEMU = qemu-system-x86_64

CFLAGS = -m64 -std=c11 -Os -ffreestanding -fno-builtin \
         -fno-stack-protector -fno-stack-check -fno-pie -fno-pic \
         -fno-asynchronous-unwind-tables -fno-unwind-tables \
         -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -Wall -Wextra \
         -U_FORTIFY_SOURCE
C_OBJECTS = kernel.o filesystem/fs.o shell/shell.o \
            console/console.o peripherals/keyboard.o \
            peripherals/mouse.o console/tty.o shell/login.o \
            peripherals/vmmouse.o
include account/account.mk
CPPFLAGS += -Ifilesystem -Ishell -Iconsole -Ikeyboard -Imouse

.PHONY: all run test clean
all: kernel.img

%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

%.o: %.s
	$(AS) --64 $< -o $@

boot.bin: boot.o
	$(LD) -m elf_x86_64 --oformat binary -Ttext 0x7c00 $< -o $@
	test "$$(wc -c < $@)" -eq 512

stage2.elf: start.o $(C_OBJECTS) linker.ld
	$(LD) -m elf_x86_64 -T linker.ld start.o $(C_OBJECTS) -o $@

stage2.bin: stage2.elf
	$(OBJCOPY) -O binary $< $@
	@if [ "$$(wc -c < $@)" -gt 32768 ]; then \
		echo 'stage2 exceeds the bootloader limit of 32768 bytes'; \
		rm -f $@; exit 1; \
	fi

kernel.img: boot.bin stage2.bin
	dd if=/dev/zero of=$@ bs=512 count=2880 status=none
	dd if=boot.bin of=$@ conv=notrunc status=none
	dd if=stage2.bin of=$@ bs=512 seek=1 conv=notrunc status=none

run: kernel.img
	$(QEMU) -drive file=kernel.img,format=raw,if=floppy -boot a

test: tests/test_shell.c filesystem/fs.c shell/shell.c
	$(CC) $(CPPFLAGS) -std=c11 -Wall -Wextra -Werror tests/test_shell.c filesystem/fs.c shell/shell.c -o test_shell
	./test_shell

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -f *.o *.d boot.bin stage2.elf stage2.bin kernel.img test_shell

-include $(C_OBJECTS:.o=.d)

