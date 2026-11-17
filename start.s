/* The BIOS loader puts this code at 0x8000. It begins in 16-bit real mode. */
.intel_syntax noprefix     # Use Intel syntax; and no % on registers
.section .text.start       
.global stage2_start       # Make this symbol visible to the linked
.extern kernel_main        # defined in kernel.c

.code16                    # Encoding with 16-bit instruction set
stage2_start:
    cli                    

    /* Set to 1 for the graphical test; 0 boots the original text shell. */
    .set GRAPHICS_TEST, 1

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov byte ptr [fb_active], 0 /* diagnostics bit: 0 = not enabled; 1 = enabled; */
    mov byte ptr [vbe_status], 1

.if GRAPHICS_TEST

    /*
     * Get VBE controller information.
     *
     * 0x5000: controller information (512 bytes)
     * 0x5200: mode information (256 bytes)
     *
     * Stage 2 is loaded at 0x8000, so these buffers
     * do not overlap its code.
     */

    mov dword ptr [0x5000], 0x32454256   /* "VBE2" */

    xor ax, ax
    mov es, ax
    mov di, 0x5000
    mov ax, 0x4F00

    sti
    int 0x10
    cli

    push ax
    xor ax, ax
    mov ds, ax
    mov es, ax
    pop ax

    cmp ax, 0x004F
    jne vbe_use_fallback

    /*
     * VideoModePtr is a far pointer at controller
     * information offset 0x0E:
     *
     * offset  = [0x500E]
     * segment = [0x5010]
     *
     * Convert segment:offset to physical address.
     */

    movzx esi, word ptr [0x5010]
    shl esi, 4
    movzx eax, word ptr [0x500E]
    add esi, eax

    mov dword ptr [vbe_list_ptr], esi

    /*
     * Search the mode list.
     * Each mode number is a 16-bit value.
     * 0xFFFF terminates the list.
     */

    mov word ptr [vbe_modes_checked], 0

vbe_search_loop:

    /* Avoid an unbounded scan if BIOS data is corrupt. */
    cmp word ptr [vbe_modes_checked], 256
    jae vbe_use_fallback

    mov esi, dword ptr [vbe_list_ptr]

    /*
     * In real mode, DS=0 and SI must be 16-bit.
     * The mode-list pointer can lie above 64 KiB,
     * so convert the physical address to ES:DI.
     */

    mov eax, esi
    mov edx, eax
    and dx, 0x000F
    shr eax, 4

    mov es, ax
    mov di, dx

    mov cx, word ptr es:[di]

    cmp cx, 0xFFFF
    je vbe_use_fallback

    /* Save the candidate mode number. */
    mov word ptr [vbe_candidate], cx

    /*
     * Query this mode.
     *
     * BIOS writes its information into 0000:5200.
     */

    xor ax, ax
    mov es, ax
    mov di, 0x5200
    mov ax, 0x4F01

    sti
    int 0x10
    cli

    push ax
    xor ax, ax
    mov ds, ax
    mov es, ax
    pop ax

    cmp ax, 0x004F
    jne vbe_next_mode

    /* Mode must be supported and graphics-capable. */
    mov ax, word ptr [0x5200]
    test ax, 0x0001
    jz vbe_next_mode

    test ax, 0x0010
    jz vbe_next_mode

    /* Linear framebuffer support. */
    test ax, 0x0080
    jz vbe_next_mode

    /* Require 1024x768. */
    cmp word ptr [0x5200 + 18], 1024
    jne vbe_next_mode

    cmp word ptr [0x5200 + 20], 768
    jne vbe_next_mode

    /* Require 32-bit direct color. */
    cmp byte ptr [0x5200 + 25], 32
    jne vbe_next_mode

    cmp byte ptr [0x5200 + 27], 6
    jne vbe_next_mode

    /* RGB888 layout in 32-bit pixels. */
    cmp byte ptr [0x5200 + 31], 8
    jne vbe_next_mode

    cmp byte ptr [0x5200 + 32], 16
    jne vbe_next_mode

    cmp byte ptr [0x5200 + 33], 8
    jne vbe_next_mode

    cmp byte ptr [0x5200 + 34], 8
    jne vbe_next_mode

    cmp byte ptr [0x5200 + 35], 8
    jne vbe_next_mode

    cmp byte ptr [0x5200 + 36], 0
    jne vbe_next_mode

    /* Physical framebuffer address must be nonzero. */
    cmp dword ptr [0x5200 + 40], 0
    je vbe_next_mode

    /* Found a compatible mode. */
    mov bx, word ptr [vbe_candidate]
    jmp vbe_activate_mode

vbe_next_mode:

    /*
     * Advance to the next entry in the mode list.
     * Preserve the pointer across BIOS calls.
     */
    add dword ptr [vbe_list_ptr], 2
    inc word ptr [vbe_modes_checked]
    jmp vbe_search_loop

vbe_use_fallback:

    /*
     * Fall back to the working 0x140 mode.
     * Query it so the framebuffer metadata is valid.
     */

    mov word ptr [vbe_candidate], 0x0140

    xor ax, ax
    mov es, ax
    mov di, 0x5200
    mov cx, 0x0140
    mov ax, 0x4F01

    sti
    int 0x10
    cli

    push ax
    xor ax, ax
    mov ds, ax
    mov es, ax
    pop ax

    cmp ax, 0x004F
    jne vbe_failed

    cmp byte ptr [0x5200 + 25], 32
    jne vbe_failed

    test word ptr [0x5200], 0x0080
    jz vbe_failed

    cmp dword ptr [0x5200 + 40], 0
    je vbe_failed

    mov bx, 0x0140

vbe_activate_mode:

    /*
     * Save framebuffer information before switching modes.
     */

    mov eax, dword ptr [0x5200 + 40]
    mov dword ptr [fb_phys], eax

    movzx eax, word ptr [0x5200 + 18]
    mov dword ptr [fb_width], eax

    movzx eax, word ptr [0x5200 + 20]
    mov dword ptr [fb_height], eax

    movzx eax, word ptr [0x5200 + 16]
    mov dword ptr [fb_pitch], eax

    mov dword ptr [fb_bpp], 32

    /*
     * Set the selected VBE mode.
     * Bit 14 requests the linear framebuffer.
     */

    or bx, 0x4000
    mov ax, 0x4F02

    sti
    int 0x10
    cli

    push ax
    xor ax, ax
    mov ds, ax
    pop ax

    cmp ax, 0x004F
    jne vbe_failed

    mov byte ptr [fb_active], 1
    mov byte ptr [vbe_status], 3
    jmp vbe_done

vbe_failed:
    mov byte ptr [fb_active], 0
    mov byte ptr [vbe_status], 2

vbe_done:
    .endif

    /* Turn on A20 through the fast A20 gate. */
    in al, 0x92
    and al, 0xfe
    or al, 0x02
    out 0x92, al

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

    /* Optional graphics: map four 2 MiB pages at virtual 0x04000000.
       Align the physical framebuffer down and retain its page offset.
       2 MiB large pages, uncacheable (PCD/PWT), writable. */
    cmp byte ptr [fb_active], 1
    jne fb_map_done
    mov eax, dword ptr [fb_phys]
    mov edx, eax
    and eax, 0xffe00000
    and edx, 0x001fffff
    add edx, 0x04000000
    mov dword ptr [fb_virtual], edx
    mov edi, 0x3000 + 32 * 8
    mov ecx, 4
fb_map_loop:
    mov edx, eax
    or edx, 0x9b             /* P, RW, PWT, PCD, PS */
    mov dword ptr [edi], edx
    mov dword ptr [edi + 4], 0
    add eax, 0x200000
    add edi, 8
    loop fb_map_loop
fb_map_done:

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

.section .data

/* C-visible boot graphics state. All addresses are below the 2 MiB identity map. */
.global fb_active
.global fb_virtual
.global fb_width
.global fb_height
.global fb_pitch
.global fb_bpp
.global vbe_status
.global vbe_attributes
.global vbe_bpp_detected
.global vbe_width_detected
.global vbe_height_detected

.align 4
fb_active:  .byte 0

.align 4
fb_phys:    .long 0
fb_virtual: .long 0
fb_width:   .long 0
fb_height:  .long 0
fb_pitch:   .long 0
fb_bpp:     .long 0

vbe_status: .byte 0
vbe_attributes: .byte 0
vbe_bpp_detected: .byte 0

.align 2
vbe_width_detected:  .word 0
vbe_height_detected: .word 0

.align 4
vbe_candidate: .word 0
vbe_modes_checked: .word 0
vbe_list_ptr: .long 0

.section .note.GNU-stack,"",@progbits  # Tell the linker this object doesn't request an executable stack.