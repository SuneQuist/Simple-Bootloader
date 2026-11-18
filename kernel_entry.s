.intel_syntax noprefix
.section .text.kernelentry,"ax",@progbits
.code64
.global kernel_entry
.extern kernel_main
.extern __bss_start
.extern __bss_end
kernel_entry:
    cld
    lea rdi, [rip + __bss_start]
    lea rcx, [rip + __bss_end]
    sub rcx, rdi
    xor eax, eax
    rep stosb
    jmp kernel_main
.section .note.GNU-stack,"",@progbits
