OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(loader_entry)
SECTIONS {
  . = 0x8000;
  .text : { *(.text.bootentry) *(.text.start) *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) }
  .data : { *(.data .data.*) }
  .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) }
  /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment*) }
}
ASSERT(. < 0xc000, "Loader exceeds 32 sectors")
