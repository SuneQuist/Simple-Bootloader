/* The BIOS loader puts this code at 0x8000. It begins in 16-bit real mode. */
.intel_syntax noprefix     # Use Intel syntax; and no % on registers
.section .text.start       
.global stage2_start       # Make this symbol visible to the linked
.extern kernel_main        # defined in kernel.c

.code16                    # Encoding with 16-bit instruction set
stage2_start:
    cli                    

    /* Turn on A20 through the fast A20 gate. */
    in al, 0x92            # Read the value of current I/O port into al
    and al, 0xfe           /* Never trigger the reset bit, by clearing bit 0 so the write won't request a reset */
    or al, 0x02            # Set bit 1, which enables A20
    out 0x92, al           # Write the modified byte back to the port.

    lgdt [gdt_descriptor]  # Tell the CPU where gdt is and its size

    /* Enter 32-bit protected mode. */
    mov eax, cr0           # Copy CPU CR0 (control register) into eax
    or eax, 1              # Set bit 0 (PE protection enabled)
    mov cr0, eax           # Write it back to enter protected mode
    ljmp 0x08, protected_mode

.code32 # Set instruction set to 32-bit mode
/*
* Selector 0x10 picks a flat data segment, meaning its base is zero. So with ES set to that segment, 
* rep stosd starts writing at address 0x1000. It clears 3072 × 4 = 12,288 bytes, enough for three 4 KiB page tables.
* cld matters because otherwise stosd could walk backward through memory if the direction flag were set
*/
protected_mode:
    mov ax, 0x10            # data-segment selector 0x10
    mov ds, ax              # Load the default data segment.
    mov es, ax              # Load ES, the destination segment used by STOS.
    mov ss, ax              # Load the stack segment.
    mov fs, ax              # Initialize the extra FS segment register.
    mov gs, ax              # Initialize the extra GS segment register.
    mov esp, 0x90000        # Set the 32-bit stack pointer
    cld                     # Clear the direction flag so string ops go forward

    /* Empty PML4, PDPT, and page directory at 0x1000..0x3fff. */
    xor eax, eax            
    mov edi, 0x1000         # Start writing to address 0x1000
    mov ecx, 3072           # Write 3072 four-byte values (32-bit)
    rep stosd               # Store EAX at ES:EDI, advance EDI by 4, and repeat.

    /* Identity-map 0..2 MiB using one 2 MiB page.
    *  Entries are 64 bits; the upper halves are already zero.
    *  An identity map means the virual pages still refer back to the physical address.
    */
    mov dword ptr [0x1000], 0x2003 /* PML4[0] -> PDPT */      # PML4[0] points to the PDPT at 0x2000.
    mov dword ptr [0x2000], 0x3003 /* PDPT[0] -> PD */        # PDPT[0] points to the PD at 0x3000.
    mov dword ptr [0x3000], 0x0083 /* PD[0] -> 2 MiB page */  # PD[0] maps one 2 MiB page at physical 0.

    # Turn on paging and jump into 64-bit code
    /* Enable PAE and point CR3 at PML4. */
    mov eax, cr4           # Read CPU control register CR4.
    or eax, 0x20           # Set bit 5, PAE (Physical Address Extension).
    mov cr4, eax           # Write CR4 back with PAE enabled.
    mov eax, 0x1000        # Load the physical address of the PML4.
    mov cr3, eax           # Tell the CPU where the root page table is.

    /* Enable long mode (EFER.LME). */
    mov ecx, 0xc0000080    # Select the EFER model-specific register.
    rdmsr                  # Read it into EDX:EAX, high half and low half.
    or eax, 0x100          # Set bit 8, LME (Long Mode Enable).
    wrmsr                  # Write EDX:EAX back to EFER.

    /* Enable paging; then far-jump into a 64-bit code segment. */
    mov eax, cr0           # Read CR0 again, preserving the PE bit.
    or eax, 0x80000000     # Set bit 31, PG (Paging).
    mov cr0, eax           # Enable paging and activate long mode.
    ljmp 0x18, long_mode   # Load the 64-bit code selector and jump.

# Call C and halt if it returns
.code64                    # Encode the next instructions for 64-bit mode.
long_mode:                 # Target of the second far jump.
    mov ax, 0x10           # Select the same data descriptor as before.
    mov ds, ax             # Reload the data segment register.
    mov es, ax             # Reload the extra segment register.
    mov ss, ax             # Reload the stack segment register.
    mov fs, ax             # Reload the FS selector.
    mov gs, ax             # Reload the GS selector.
    mov rsp, 0x90000       # Set the full 64-bit stack pointer.
    and rsp, -16           # Clear the bottom four bits to align it to 16 bytes.
    call kernel_main       # Push the return address and enter the C function.

hang:                      # Execution comes here only if kernel_main returns.
    cli                    # Ensure maskable interrupts are disabled.
    hlt                    # Halt the CPU.
    jmp hang               # If execution resumes, go back and halt again.

/* GDT: null, 32-bit code, data, 64-bit code. */
.align 8                   # Pad so the GDT starts on an 8-byte boundary.
gdt:                       # Address of the first GDT entry.
    .quad 0                # Entry 0, selector 0x00: null descriptor.
    .quad 0x00cf9a000000ffff  # Entry 1, selector 0x08: 32-bit kernel code.
    .quad 0x00cf92000000ffff  # Entry 2, selector 0x10: writable kernel data.
    .quad 0x00af9a000000ffff  # Entry 3, selector 0x18: 64-bit kernel code.
gdt_end:                   # Address immediately after the last entry.

gdt_descriptor:            # The record that LGDT reads.
    .word gdt_end - gdt - 1  # 16-bit limit: table size minus one, or 31.
    .long gdt              # 32-bit field holding the table's base address.

.section .note.GNU-stack,"",@progbits  # Tell the linker this object doesn't request an executable stack.

