.intel_syntax noprefix
.code16
.section .text
.global _start
_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    mov [boot_drive], dl
    mov bx, 0x8000
    mov cx, 2
    xor dh, dh
    mov si, 32
read_sector:
    push bx
    push cx
    push dx
    push si
    mov dl, [boot_drive]
    mov ax, 0x0201
    int 0x13
    pop si
    pop dx
    pop cx
    pop bx
    jc disk_error
    add bx, 512
    dec si
    jz loaded
    inc cl
    cmp cl, 19
    jb read_sector
    mov cl, 1
    xor dh, 1
    jnz read_sector
    inc ch
    jmp read_sector
loaded:
    mov dl, [boot_drive]
    ljmp 0x0000, 0x8000
disk_error:
    cli
1:  hlt
    jmp 1b
boot_drive: .byte 0
.org 510
.word 0xaa55
