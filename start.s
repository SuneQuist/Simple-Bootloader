/* The BIOS loader puts this code at 0x8000. It begins in 16-bit real mode. */
.intel_syntax noprefix
.section .text.start
.global stage2_start
.extern kernel_main

.code16
stage2_start:
    cli

    /* Turn on A20 through the fast A20 gate. */
    in al, 0x92
    and al, 0xfe           /* Never trigger the reset bit */
    or al, 0x02
    out 0x92, al

    lgdt [gdt_descriptor]

    /* Enter 32-bit protected mode. */
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    ljmp 0x08, protected_mode

.code32
protected_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov esp, 0x90000
    cld

    /* Empty PML4, PDPT, and page directory at 0x1000..0x3fff. */
    xor eax, eax
    mov edi, 0x1000
    mov ecx, 3072
    rep stosd

    /* Identity-map 0..2 MiB using one 2 MiB page.
       Entries are 64 bits; the upper halves are already zero. */
    mov dword ptr [0x1000], 0x2003 /* PML4[0] -> PDPT */
    mov dword ptr [0x2000], 0x3003 /* PDPT[0] -> PD */
    mov dword ptr [0x3000], 0x0083 /* PD[0] -> 2 MiB page */

    /* Enable PAE and point CR3 at PML4. */
    mov eax, cr4
    or eax, 0x20
    mov cr4, eax
    mov eax, 0x1000
    mov cr3, eax

    /* Enable long mode (EFER.LME). */
    mov ecx, 0xc0000080
    rdmsr
    or eax, 0x100
    wrmsr

    /* Enable paging; then far-jump into a 64-bit code segment. */
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax
    ljmp 0x18, long_mode

.code64
long_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov rsp, 0x90000
    and rsp, -16
    call kernel_main

hang:
    cli
    hlt
    jmp hang

/* GDT: null, 32-bit code, data, 64-bit code. */
.align 8
gdt:
    .quad 0
    .quad 0x00cf9a000000ffff
    .quad 0x00cf92000000ffff
    .quad 0x00af9a000000ffff
gdt_end:

gdt_descriptor:
    .word gdt_end - gdt - 1
    .long gdt

.section .note.GNU-stack,"",@progbits

