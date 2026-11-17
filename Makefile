
AS = as
CC = gcc
LD = ld
OBJCOPY = objcopy
QEMU = qemu-system-x86_64

BUILD_DIR = compiled

CFLAGS = -m64 -std=c11 -Os -ffreestanding -fno-builtin \
         -fno-stack-protector -fno-stack-check -fno-pie -fno-pic \
         -fno-asynchronous-unwind-tables -fno-unwind-tables \
         -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -Wall -Wextra \
         -U_FORTIFY_SOURCE

CPPFLAGS += -Ifilesystem -Ishell -Iconsole -Ikeyboard -Imouse

C_OBJECTS = kernel.o filesystem/fs.o shell/shell.o \
            console/console.o peripherals/keyboard.o \
            peripherals/mouse.o console/tty.o shell/login.o \
            peripherals/vmmouse.o bin/basic.o networking/netcard.o \
            networking/net.o networking/e1000.o networking/net_route.o \
            networking/timer.o networking/http.o ux/framebuffer.o \
            ux/desktop.o ux/font.o ux/cursor.o peripherals/interrupts.o \
            peripherals/irq12.o shell/terminal.o shell/history.o

# Account system and BearSSL dependencies
include account/account.mk

# Compiled object files
OBJECTS = $(addprefix $(BUILD_DIR)/,$(C_OBJECTS))

BOOT_OBJ = $(BUILD_DIR)/boot.o
START_OBJ = $(BUILD_DIR)/start.o

BOOT_BIN = $(BUILD_DIR)/boot.bin
STAGE2_ELF = $(BUILD_DIR)/stage2.elf
STAGE2_BIN = $(BUILD_DIR)/stage2.bin

.PHONY: all run test clean

all: kernel.img

# Compile C source files
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

# Assemble assembly source files
$(BUILD_DIR)/%.o: %.s
	@mkdir -p $(dir $@)
	$(AS) --64 $< -o $@

# Build the 512-byte boot sector
$(BOOT_BIN): $(BOOT_OBJ)
	$(LD) -m elf_x86_64 --oformat binary -Ttext 0x7c00 $< -o $@
	test "$$(wc -c < $@)" -eq 512

# Link the second-stage kernel
$(STAGE2_ELF): $(START_OBJ) $(OBJECTS) linker.ld
	$(LD) -m elf_x86_64 -T linker.ld $(START_OBJ) $(OBJECTS) -o $@

# Convert ELF to raw binary
$(STAGE2_BIN): $(STAGE2_ELF)
	$(OBJCOPY) -O binary $< $@
	@if [ "$$(wc -c < $@)" -gt 32768 ]; then \
		echo 'stage2 exceeds the bootloader limit of 32768 bytes'; \
		rm -f $@; exit 1; \
	fi

# Create bootable floppy image
kernel.img: $(BOOT_BIN) $(STAGE2_BIN)
	dd if=/dev/zero of=$@ bs=512 count=2880 status=none
	dd if=$(BOOT_BIN) of=$@ conv=notrunc status=none
	dd if=$(STAGE2_BIN) of=$@ bs=512 seek=1 conv=notrunc status=none

# Run Tiny64 in QEMU
run: kernel.img
	$(QEMU) -drive file=kernel.img,format=raw,if=floppy -boot a

# Run shell unit tests
test: tests/test_shell.c filesystem/fs.c shell/shell.c
	$(CC) $(CPPFLAGS) -std=c11 -Wall -Wextra -Werror \
		tests/test_shell.c filesystem/fs.c shell/shell.c \
		-o test_shell
	./test_shell

# Remove generated build files
clean:
	rm -rf $(BUILD_DIR)
	rm -f kernel.img test_shell

# Track header dependencies automatically
-include $(OBJECTS:.o=.d)
