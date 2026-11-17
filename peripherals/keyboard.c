#include "../console/console.h"
#include "keyboard.h"
#include "mouse.h"
#include "vmmouse.h"

/* Translated PS/2 set 1 scan codes, US keyboard layout. */
static const char plain[128] = {
    [0x02] = '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=',
    [0x0E] = '\b',
    [0x10] = 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    [0x1E] = 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    [0x2B] = '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
    [0x39] = ' '
};

static const char shifted[128] = {
    [0x02] = '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+',
    [0x1A] = '{', '}',
    [0x27] = ':', '"', '~',
    [0x2B] = '|',
    [0x33] = '<', '>', '?'
};

static const unsigned char extended_keys[128] = {
    [0x47] = KEY_HOME,
    [0x48] = KEY_UP,
    [0x49] = KEY_PAGE_UP,
    [0x4B] = KEY_LEFT,
    [0x4D] = KEY_RIGHT,
    [0x4F] = KEY_END,
    [0x50] = KEY_DOWN,
    [0x51] = KEY_PAGE_DOWN,
    [0x52] = KEY_INSERT,
    [0x53] = KEY_DELETE
};

static unsigned char inb(unsigned short port) {
    unsigned char value;
    __asm volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static signed ps2_write(unsigned short port, unsigned char value) {
    for (unsigned i = 0; i < 100000; ++i) {
        if (!(inb(0x64) & 0x02)) {
            __asm volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
            return 1;
        }
    }
    return 0;
}

#define KEYBOARD_QUEUE_SIZE 256u
#define KEYBOARD_QUEUE_MASK (KEYBOARD_QUEUE_SIZE - 1u)

static volatile unsigned char scancode_queue[KEYBOARD_QUEUE_SIZE];
static volatile unsigned scancode_head = 0;
static volatile unsigned scancode_tail = 0;
static volatile unsigned keyboard_overflows = 0;

void keyboard_irq_scancode(unsigned char code)
{
    unsigned next = (scancode_head + 1u) & KEYBOARD_QUEUE_MASK;

    if (next == scancode_tail) {
        keyboard_overflows++;
        return;
    }

    scancode_queue[scancode_head] = code;
    scancode_head = next;
}

static int keyboard_pop_scancode(void)
{
    unsigned tail = scancode_tail;

    if (tail == scancode_head)
        return -1;

    unsigned char code = scancode_queue[tail];

    scancode_tail = (tail + 1u) & KEYBOARD_QUEUE_MASK;

    return code;
}

static int absolute_mouse;
static unsigned shifts, caps, caps_down, e0_pending, skip;
void keyboard_init(void)
{
    absolute_mouse = 0;
    shifts = caps = caps_down = e0_pending = skip = 0;

    scancode_head = 0;
    scancode_tail = 0;
    keyboard_overflows = 0;
}

signed keyboard_enable_mouse(void) {
    mouse_reset_packet();
    
    if (!ps2_write(0x64, 0xA8) ||  /* Enable the second PS/2 port. */
        !ps2_write(0x64, 0xD4) ||  /* Route the next byte to the mouse. */
        !ps2_write(0x60, 0xF4))    /* Enable mouse reporting. */
        return 0;

    for (unsigned i = 0; i < 100000; ++i) {
        unsigned status = inb(0x64);
        if (!(status & 0x01))
            continue;

        unsigned char byte = inb(0x60);
        if ((status & 0xE0) == 0x20) {
            if (byte != 0xFA)
                return 0;

            /* Read the PS/2 controller configuration byte. */
            if (!ps2_write(0x64, 0x20))
                return 0;

            unsigned config = 0;
            int got_config = 0;

            for (unsigned i = 0; i < 100000; ++i) {
                if (inb(0x64) & 0x01) {
                    config = inb(0x60);
                    got_config = 1;
                    break;
                }
            }

            if (!got_config)
                return 0;

            /* Enable IRQ12 and enable the second PS/2 clock. */
            config |= 0x02;
            config &= ~0x20;

            /* Write the configuration byte back. */
            if (!ps2_write(0x64, 0x60))
                return 0;

            if (!ps2_write(0x60, config))
                return 0;

            absolute_mouse = vmmouse_init();
            console_print(absolute_mouse ? "VMMouse absolute ready\n"
                                         : "PS/2 relative fallback\n");
            return 1;
        }
    }
    return 0;
}

/*
* status & 1 (0000 0001) checks Bit 0. If Bit 0 is 1, a byte is waiting to be read, and all other bits are ignored.
* status & 0xe0 (1110 0000) checks Bits 5, 6, and 7.
* Bit 5 (0x20) mean a Byte came from PS/2 mouse, not the keyboard.
* Bit 6 (0x40) means a timeout error.
* Bit 7 (0x80) means a parity error.
* If any of these are 1, then we throw an error.
* Remember we read bits 0-7, so 7 is the last bit. (2110 0000) the last bit would be 2
* you'd never see 2, it is just to point out the obvious.
*
* code & 0x80 check the Release Flag if it is 1 then you Releassed a key, if its 0 you Pressed a key
* code & 0x7f strips off Bit 7 and keeps only bits 0-6 to read the actual key ID.
*   pressed A: (0001 1110), released A (keeps bit 7): (1001 1110);
*
* Full example:
* PRES Left Shift (0x2A) RELEASE Left Shift (0xAA) 
* PRESS a (0x1E)
* ====PRESS====
* Read key ID; [0x2A] 0010 1010 & 0111 1111 = 0010 1010 (0x2A)
* Press vs Release; [0x2A] 0010 1010 & 1000 0000 = 0000 0000 = PRESS
* Turn Left Shift Bit ON (shifts |= 1); 0000 0000 | 0000 0001 = 0000 0001 (shifts = 1 => Left Shift active)
* Read key ID for (a), press or release (a is PRESS), is left shift 1 or 0 (1 in this case),
*  so convert a -> A, since shifts Bit 0 is 1.
* ====RELEASE====
* Release Left Shift code (0xAA) & 0x7F = 0010 1010 (0x2A, so still identifies Left Shift)
* Press or Release; 0xAA & 0x80 = 1000 0000 (RELEASE)
* shifts &= ~1, ~(tilde) means NOT and flips all bits.
* So again shifts &= ~1; 0000 0001 & 1111 1110 = 0000 0000 (Shifts = 0 -> Left Shift cleared) 
*/
/*
 * Decode one scan code.
 *
 * Returns:
 *   -1 = no complete key event
 *  >=0 = decoded character or special key
 */
static int keyboard_decode(unsigned char code)
{
    if (skip) {
        --skip;
        return -1;
    }

    if (code == 0xE1) {
        e0_pending = 0;
        skip = 5;  /* Remaining Pause sequence bytes. */
        return -1;
    }

    if (code == 0xE0) {
        e0_pending = 1;
        return -1;
    }

    unsigned key = code & 0x7F;
    int released = (code & 0x80) != 0;

    if (e0_pending) {
        e0_pending = 0;

        if (!released && extended_keys[key])
            return extended_keys[key];

        return -1;
    }

    if (key == 0x2A || key == 0x36) {
        unsigned bit = key == 0x2A ? 1u : 2u;

        if (released)
            shifts &= ~bit;
        else
            shifts |= bit;

        return -1;
    }

    if (key == 0x3A) {
        if (!released && !caps_down)
            caps ^= 1u;

        caps_down = !released;
        return -1;
    }

    if (released)
        return -1;

    char c = plain[key];

    if (!c)
        return -1;

    if (c >= 'a' && c <= 'z')
        return ((shifts != 0) ^ caps)
            ? c - 'a' + 'A'
            : c;

    return (shifts && shifted[key])
        ? shifted[key]
        : c;
}

/*
 * Nonblocking: returns -1 if no decoded key is ready.
 */
int keyboard_try_getc(void)
{
    for (;;) {
        int code = keyboard_pop_scancode();

        if (code < 0)
            return -1;

        int key = keyboard_decode((unsigned char)code);

        if (key >= 0)
            return key;
    }
}

/*
 * Blocking keyboard input, compatible with your shell.
 */
signed keyboard_getc(void)
{
    for (;;) {
        int key = keyboard_try_getc();

        if (key >= 0)
            return key;

        __asm volatile ("pause");
    }
}