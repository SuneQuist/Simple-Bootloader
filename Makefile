
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

LEXBOR_CFLAGS = $(filter-out -mno-sse -mno-sse2,$(CFLAGS)) \
                -msse -msse2

CPPFLAGS += -Ifilesystem -Ishell -Iconsole -Ikeyboard -Imouse -Igraphics \
            -Ivendor/lexbor/source \
            -Ivendor/lexbor/source/lexbor

# --------------------------------------------------
# Tiny64 C object files
# --------------------------------------------------

C_OBJECTS = kernel.o filesystem/fs.o shell/shell.o \
            console/console.o peripherals/keyboard.o \
            peripherals/mouse.o console/tty.o shell/login.o \
            peripherals/vmmouse.o bin/basic.o networking/netcard.o \
            networking/net.o networking/e1000.o networking/net_route.o \
            networking/timer.o networking/http.o graphics/framebuffer.o \
            graphics/desktop.o graphics/font.o graphics/cursor.o  graphics/window.o \
			graphics/frame_pacing.o \
			peripherals/interrupts.o peripherals/irq12.o shell/terminal.o shell/history.o \
            memory/heap.o memory/lexbor_memory.o memory/string.o memory/math.o \
			web/htmltest.o web/interpreter.o web/renderer.o web/css.o web/navigation.o

# --------------------------------------------------
# Lexbor HTML parser
# --------------------------------------------------

LEXBOR_DIR := vendor/lexbor/source/lexbor

# Modules required by Lexbor's HTML and CSS parsers
#
# css:
#     Parses CSS stylesheets, rules, and declarations.
#
# selectors:
#     Parses CSS selectors and supports DOM matching.
#
# Keep the existing HTML dependencies unchanged.
LEXBOR_MODULES := core dom ns tag html css selectors

# Discover C source files in the required modules
LEXBOR_SOURCES := $(foreach module,$(LEXBOR_MODULES),\
    $(shell find $(LEXBOR_DIR)/$(module) -name '*.c'))

# Keep paths relative; BUILD_DIR is added below
LEXBOR_OBJECTS := $(LEXBOR_SOURCES:.c=.o)

C_OBJECTS += $(LEXBOR_OBJECTS)

# --------------------------------------------------
# Account system and BearSSL dependencies
# --------------------------------------------------

include account/account.mk
include tls/tls.mk

# --------------------------------------------------
# Compiled object files
# --------------------------------------------------

OBJECTS = $(addprefix $(BUILD_DIR)/,$(C_OBJECTS))

BOOT_OBJ = $(BUILD_DIR)/boot.o
LOADER_OBJ = $(BUILD_DIR)/loader.o
START_OBJ = $(BUILD_DIR)/start.o
KERNEL_ENTRY_OBJ = $(BUILD_DIR)/kernel_entry.o
BOOT_BIN = $(BUILD_DIR)/boot.bin
LOADER_ELF = $(BUILD_DIR)/loader.elf
LOADER_BIN = $(BUILD_DIR)/loader.bin
KERNEL_ELF = $(BUILD_DIR)/kernel.elf
KERNEL_BIN = $(BUILD_DIR)/kernel.bin

.PHONY: all run test clean
all: kernel.img

$(BUILD_DIR)/memory/math.o: CFLAGS := $(filter-out -mno-sse -mno-sse2,$(CFLAGS)) -msse -msse2
$(BUILD_DIR)/vendor/lexbor/source/lexbor/%.o: vendor/lexbor/source/lexbor/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(LEXBOR_CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/%.o: %.s
	@mkdir -p $(dir $@)
	$(AS) --64 $< -o $@

$(BOOT_BIN): $(BOOT_OBJ)
	$(LD) -m elf_x86_64 --oformat binary -Ttext 0x7c00 $< -o $@
	test "$$(wc -c < $@)" -eq 512

$(KERNEL_ELF): $(KERNEL_ENTRY_OBJ) $(OBJECTS) linker.ld
	$(LD) -m elf_x86_64 -T linker.ld $(KERNEL_ENTRY_OBJ) $(OBJECTS) -o $@

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@
	@test "$$(wc -c < $@)" -le 1453568 || (echo 'Kernel exceeds floppy capacity'; exit 1)

$(BUILD_DIR)/loader.o: loader.s $(KERNEL_BIN)
	@mkdir -p $(dir $@)
	$(AS) --64 --defsym KERNEL_SECTORS=$$(( ($$(wc -c < $(KERNEL_BIN)) + 511) / 512 )) $< -o $@

$(LOADER_ELF): $(LOADER_OBJ) $(START_OBJ) loader.ld
	$(LD) -m elf_x86_64 -T loader.ld $(LOADER_OBJ) $(START_OBJ) -o $@

$(LOADER_BIN): $(LOADER_ELF)
	$(OBJCOPY) -O binary $< $@
	@test "$$(wc -c < $@)" -le 16384 || (echo 'Loader exceeds 32 sectors'; exit 1)

kernel.img: $(BOOT_BIN) $(LOADER_BIN) $(KERNEL_BIN)
	dd if=/dev/zero of=$@ bs=512 count=2880 status=none
	dd if=$(BOOT_BIN) of=$@ conv=notrunc status=none
	dd if=$(LOADER_BIN) of=$@ bs=512 seek=1 conv=notrunc status=none
	dd if=$(KERNEL_BIN) of=$@ bs=512 seek=33 conv=notrunc status=none

run: kernel.img
	$(QEMU) -drive file=kernel.img,format=raw,if=floppy -boot a

# --------------------------------------------------
# Run shell unit tests
# --------------------------------------------------

test: tests/test_shell.c filesystem/fs.c shell/shell.c
	$(CC) $(CPPFLAGS) -std=c11 -Wall -Wextra -Werror \
	    tests/test_shell.c filesystem/fs.c shell/shell.c \
	    -o test_shell
	./test_shell

# --------------------------------------------------
# Remove generated build files
# --------------------------------------------------

clean:
	rm -rf $(BUILD_DIR)
	rm -f kernel.img test_shell

# --------------------------------------------------
# Track header dependencies automatically
# --------------------------------------------------

-include $(OBJECTS:.o=.d)

# Native transport-state tests with synthetic Ethernet/ARP/DNS/TCP packets.
.PHONY: test-async
test-async:
	gcc -std=c11 -O1 -Wall -Wextra tests/test_async_http.c -o compiled/test_async_http
	./compiled/test_async_http

# Host-side smoke test of the selected BearSSL client modules. It does not
# establish a TLS connection or claim that HTTPS is secure/implemented.
.PHONY: test-tls
test-tls: $(BUILD_DIR)/tls/tls_diag.o \
          $(addprefix $(BUILD_DIR)/,$(TLS_SOURCES:.c=.o)) \
          $(BUILD_DIR)/vendor/bearssl/src/hash/sha2small.o \
          $(BUILD_DIR)/vendor/bearssl/src/mac/hmac.o \
          $(BUILD_DIR)/vendor/bearssl/src/codec/dec32be.o \
          $(BUILD_DIR)/vendor/bearssl/src/codec/enc32be.o
	$(CC) -no-pie -std=c11 -O1 -Wall -Wextra -Werror \
	    -Ivendor/bearssl/inc tests/test_tls_diag.c $^ -o $(BUILD_DIR)/test_tls_diag
	./$(BUILD_DIR)/test_tls_diag

# Deterministic native transport test. No TLS trust is asserted here.
.PHONY: test-tls-transport
test-tls-transport:
	@mkdir -p $(BUILD_DIR)
	$(CC) -std=c11 -O1 -Wall -Wextra -Werror tests/test_tls_transport.c -o $(BUILD_DIR)/test_tls_transport
	./$(BUILD_DIR)/test_tls_transport

# Host-side ClientHello/SNI test using the actual BearSSL implementation.
.PHONY: test-tls-clienthello
test-tls-clienthello: $(BUILD_DIR)/tls/tls_probe.o $(BUILD_DIR)/tls/trust_anchors.o \
          $(BUILD_DIR)/tls/https_preview.o \
          $(addprefix $(BUILD_DIR)/,$(TLS_SOURCES:.c=.o)) \
          $(BUILD_DIR)/vendor/bearssl/src/hash/sha2small.o \
          $(BUILD_DIR)/vendor/bearssl/src/mac/hmac.o \
          $(BUILD_DIR)/vendor/bearssl/src/codec/dec32be.o \
          $(BUILD_DIR)/vendor/bearssl/src/codec/enc32be.o
	$(CC) -no-pie -std=c11 -O1 -Wall -Wextra -Werror \
	    -Ivendor/bearssl/inc tests/test_tls_clienthello.c $^ -o $(BUILD_DIR)/test_tls_clienthello
	./$(BUILD_DIR)/test_tls_clienthello

# UTC parsing, Gregorian date arithmetic and operator-approval policy.
.PHONY: test-tls-clock
test-tls-clock:
	@mkdir -p $(BUILD_DIR)
	$(CC) -std=c11 -O1 -Wall -Wextra -Werror tests/test_tls_clock.c tls/tls_clock.c -o $(BUILD_DIR)/test_tls_clock
	./$(BUILD_DIR)/test_tls_clock

# Test BearSSL's actual X.509 verifier against an ephemeral local test CA.
# No private key is stored in the project or included in the kernel.
.PHONY: test-tls-x509
test-tls-x509: $(addprefix $(BUILD_DIR)/,$(TLS_SOURCES:.c=.o)) \
          $(BUILD_DIR)/vendor/bearssl/src/hash/sha2small.o \
          $(BUILD_DIR)/vendor/bearssl/src/mac/hmac.o \
          $(BUILD_DIR)/vendor/bearssl/src/codec/dec32be.o \
          $(BUILD_DIR)/vendor/bearssl/src/codec/enc32be.o
	python3 tests/make_test_x509_fixture.py
	$(CC) -no-pie -std=c11 -O1 -Wall -Wextra -Werror \
	    -Ivendor/bearssl/inc -I$(BUILD_DIR) tests/test_tls_x509.c $^ \
	    -o $(BUILD_DIR)/test_tls_x509
	./$(BUILD_DIR)/test_tls_x509

# Incremental HTTPS response parser: no network or OS dependencies.
.PHONY: test-https-preview
test-https-preview:
	@mkdir -p $(BUILD_DIR)
	$(CC) -std=c11 -O1 -Wall -Wextra -Werror tests/test_https_preview.c tls/https_preview.c -o $(BUILD_DIR)/test_https_preview
	./$(BUILD_DIR)/test_https_preview

# Real TLS 1.2 handshake and HTTPS GET against an ephemeral localhost CA.
# No private keys or test CA roots are linked into Tiny64.
.PHONY: test-https-integration
test-https-integration: $(BUILD_DIR)/tls/tls_probe.o $(BUILD_DIR)/tls/https_preview.o \
          $(addprefix $(BUILD_DIR)/,$(TLS_SOURCES:.c=.o)) \
          $(BUILD_DIR)/vendor/bearssl/src/hash/sha2small.o \
          $(BUILD_DIR)/vendor/bearssl/src/mac/hmac.o \
          $(BUILD_DIR)/vendor/bearssl/src/codec/dec32be.o \
          $(BUILD_DIR)/vendor/bearssl/src/codec/enc32be.o
	python3 tests/test_https_integration.py

.PHONY: test-browser-urls
test-browser-urls:
	@mkdir -p $(BUILD_DIR)
	$(CC) -std=c11 -O1 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
	    -Wl,--gc-sections -Ivendor/lexbor/source tests/test_browser_urls.c \
	    -o $(BUILD_DIR)/test_browser_urls
	./$(BUILD_DIR)/test_browser_urls
