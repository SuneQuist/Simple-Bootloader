.intel_syntax noprefix
.section .text.bootentry,"ax",@progbits
.global loader_entry
.extern stage2_start
.code16
loader_entry:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7b00
    mov byte ptr [drive], dl
    mov word ptr [next_lba], 33
    mov word ptr [remaining], KERNEL_SECTORS
    mov dword ptr [destination], 0x100000
    in al, 0x92
    and al, 0xfe
    or al, 2
    out 0x92, al
next_chunk:
    cmp word ptr [remaining], 0
    je kernel_loaded
    mov ax, [remaining]
    cmp ax, 64
    jbe count_ok
    mov ax, 64
count_ok:
    mov [chunk_count], ax
    mov [chunk_left], ax
    mov ax, 0x2000
    mov es, ax
    xor bx, bx
read_one:
    mov ax, [next_lba]
    xor dx, dx
    mov cx, 36
    div cx
    mov [cylinder], al
    mov ax, dx
    xor dx, dx
    mov cx, 18
    div cx
    mov dh, al
    mov cl, dl
    inc cl
    mov ch, [cylinder]
    mov dl, [drive]
    push bx
    push es
    push ds
    mov ax, 0x0201
    int 0x13
    pop ds
    pop es
    pop bx
    jc disk_error
    add bx, 512
    inc word ptr [next_lba]
    dec word ptr [chunk_left]
    jnz read_one
    cli
    lgdt [copy_gdt_ptr]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    ljmp 0x08, copy_protected
.code32
copy_protected:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esi, 0x20000
    mov edi, dword ptr [destination]
    movzx ecx, word ptr [chunk_count]
    shl ecx, 7
    cld
    rep movsd
    mov dword ptr [destination], edi
    ljmp 0x18, copy_real_compat
.code16
copy_real_compat:
    mov eax, cr0
    and eax, 0xfffffffe
    mov cr0, eax
    ljmp 0x0000, copy_real
copy_real:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7b00
    mov ax, [chunk_count]
    sub [remaining], ax
    jmp next_chunk
kernel_loaded:
    mov dl, [drive]
    ljmp 0x0000, stage2_start
disk_error:
    cli
1:  hlt
    jmp 1b
.align 8
copy_gdt:
    .quad 0
    .quad 0x00cf9a000000ffff
    .quad 0x00cf92000000ffff
    .quad 0x00009a000000ffff
copy_gdt_end:
copy_gdt_ptr:
    .word copy_gdt_end - copy_gdt - 1
    .long copy_gdt
drive:       .byte 0
cylinder:    .byte 0
.align 2
next_lba:    .word 0
remaining:   .word 0
chunk_count: .word 0
chunk_left:  .word 0
.align 4
destination: .long 0
.section .note.GNU-stack,"",@progbits
