OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(kernel_entry)
SECTIONS {
  . = 0x100000;
  .text : { *(.text.kernelentry) *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) }
  .data : { *(.data .data.*) }
  __bss_start = .;
  .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) }
  __bss_end = .;
  /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment*) }
}
fb_active = 0x6000;
fb_virtual = 0x6008;
fb_width = 0x600c;
fb_height = 0x6010;
fb_pitch = 0x6014;
fb_bpp = 0x6018;
vbe_status = 0x601c;
vbe_attributes = 0x601d;
vbe_bpp_detected = 0x601e;
vbe_width_detected = 0x6020;
vbe_height_detected = 0x6022;
ASSERT(. < 0x380000, "Kernel too large for mapped memory and stack")
