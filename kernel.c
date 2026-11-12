/* Freestanding C; no libc or operating-system functions. */

void kernel_main(void) {
    volatile unsigned short *vga = (volatile unsigned short *)0xb8000;
    const char *message = "Hello from 64-bit C!";

    for (unsigned int i = 0; message[i] != '\0'; ++i) {
        vga[i] = (unsigned short)(0x0f00u | (unsigned char)message[i]);
    }

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

