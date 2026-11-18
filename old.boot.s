# no % before registers
.intel_syntax noprefix
.code16 # 16 bit encoding

.section .text
.global _start

_start:
	cli # Clear Interrupt Flag - means ignoring inputs like keyboard, etc.

	# Set segmetation for 16-bit to zeroed out
	xor ax, ax
	mov ds, ax
	mov es, ax
	mov ss, ax
	
	mov sp, 0x7c00 # stack-pointer to boot loader address [0x7c00 - 0x7DFF]
	
	sti # Start Interrupt Flag
    
    /*  # Deprecated for 32KB
        # Bootloader takes over here, ID from where you booted from
        # is stored in dl, 
        # byte ptr [boot_drive] means to load dl into the adress of boot_drive
        # byte ptr is simply the size of boot_drive to load
        mov byte ptr [boot_drive], dl # bits 0-7 from rdx (low)

        # Offset in RAM where BIOS will place the data read from disk
        mov bx, 0x8000	# ES:BX = 0000:8000

        # Tells the BIOS interrupt which function you want to run
        mov ah, 0x02	# Read Sectors from drive

        # How many sectors to read. A sector is 512 bytes each, so 16 is 8,192 (8 KB) 
        mov al, 16	# 16 sectors

        #Selects cylinder 0 on disk
        #The name cylinder comes from the traces on a disk
        mov ch, 0	# cylinder 0 (CH)
       
         # Our current bootloader (this script) is Sector 1 so we jump to sector 2 next
        mov cl, 2	# sector 2 (sectors start at 1)

        # Select head 0 (the first read/write of the drive)
        # The word head comes from again a HDD where the arm reading and writing
        # is referred to as the head.
        mov dh, 0	# head 0
        
        # Load the saved drive number back into dl
        mov dl, byte ptr [boot_drive]
        
        # Triggers BIOS interrupt, and aks the BIOS to execute the disk and read
        # using the parameters you just set.
        int 0x13
    
        # Jump if Carry (jc) means if the BIOS fails to read the disk,
        # it sets the CPU's Carry Flag (CF) t0 1.
        # In general computing CF 0 means success and 1 means error.
        jc disk_error
    */

    /*
    mov bx, 0x8000  # Destination in RAM: ES:BX = 0000:8000
    mov cx, 2       # CH = cylinder 0, CL = sector 2
    xor dh, dh      # Head 0. Leave DL holding the BIOS boot drive.
    */
    /* Phyiscal address = ES * 16 + BX */
    /* Stage 2 begins at physical address 0x8000. */
    xor ax, ax
    mov es, ax
    mov bx, 0x8000

    /* Start at cylinder 0, sector 2, head 0. */
    mov cx, 2
    xor dh, dh

    /* Load 256 sectors = 128 KiB. */
    mov si, 256


read_sector:
    push ds
    push es
    push bx
    push cx
    push dx
    push si

    /* BIOS: read one sector into ES:BX. */
    mov ax, 0x0201
    int 0x13

    pop si
    pop dx
    pop cx
    pop bx
    pop es
    pop ds

    jc disk_error

    /* One more sector successfully loaded. */
    dec si
    jz loaded

    /* Advance destination by 512 bytes. */
    add bx, 512
    jnc next_disk_sector

    /*
     * BX wrapped past 0xFFFF.
     * Move ES forward by 64 KiB.
     */
    mov ax, es
    add ax, 0x1000
    mov es, ax

next_disk_sector:
    /*
     * Advance CHS coordinates.
     * Floppy geometry: 18 sectors per track,
     * 2 heads per cylinder.
     */
    inc cl
    cmp cl, 19
    jb read_sector

    mov cl, 1
    xor dh, 1
    jnz read_sector

    inc ch
    jmp read_sector

/* long jump, sets the code segment to 0x000, and the instruciton pointer
*  to 0x8000
*/
loaded:
	ljmp 0x0000, 0x8000

disk_error:
	cli
# 1 is Just a label for CF 1 
1:
	hlt
	jmp 1b # Jump 1 backward, so infinite loop back to halt
/* Deprecated for a 32KB
boot_drive:
	.byte 0 # 1 byte of stoage, also where dl is stored!
*/

/* An assembler directive (origin) that pads the file with zeroes
*  up to byte offset 510
*/
.org 510
/* Writes the 2-byte Boot Signature (0x55 followed by 0xAA in little endian byte oder)
*  this is important because the BIOS scans at startup, and if this exact line 0xAA55
*  is not at the end of the sector (512 bytes), then the boot code is not valid.
*/
.word 0xaa55
	
