#include "console.h"
#include "../shell/terminal.h"

enum { WIDTH = 80, HEIGHT = 25, CELLS = WIDTH * HEIGHT };
static volatile unsigned short *const vga = (volatile unsigned short *)0xB8000;
static unsigned pos;

static void outb(unsigned short port, unsigned char value) {
    __asm volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

void console_set_cursor(unsigned cell) {
    outb(0x3D4, 0x0E);
    outb(0x3D5, (unsigned char)(cell >> 8));
    outb(0x3D4, 0x0F);
    outb(0x3D5, (unsigned char)cell);
}

static void cell(unsigned at, char c) {
    vga[at] = 0x0F00u | (unsigned char)c;
}

void console_clear(void) {
    if (terminal_active()) {
        terminal_clear();
        return;
    }

    pos = 0;
    for (unsigned i = 0; i < CELLS; ++i)
        cell(i, ' ');
}

void console_putc(char c) {
    if (terminal_active()) {
        terminal_putc(c);
        return;
    }

    if (c == '\b') {
        if (pos)
            cell(--pos, ' ');
        return;
    }
    if (c == '\n')
        pos += WIDTH - pos % WIDTH;
    else
        cell(pos++, c);

    if (pos >= CELLS) {
        for (unsigned i = 0; i < CELLS - WIDTH; ++i)
            vga[i] = vga[i + WIDTH];
        for (unsigned i = CELLS - WIDTH; i < CELLS; ++i)
            cell(i, ' ');
        pos = CELLS - WIDTH;
    }

    console_set_cursor(pos);
}

void console_line_end(void) {
    if (terminal_active()) {
        terminal_line_end();
        return;
    }

    if (pos % WIDTH)
        console_putc('\n');
}

void console_write(const char *text, unsigned len) {
    for (unsigned i = 0; i < len; ++i)
        console_putc(text[i]);
}

void console_print(const char *text) {
    while (*text)
        console_putc(*text++);
}

void console_print_uint(unsigned n) {
    char digits[10];  // A 32-bit unsigned int has at most 10 decimal digits.
    unsigned count = 0;

    do {
        digits[count++] = '0' + n % 10;
        n /= 10;
    } while (n != 0);

    while (count > 0)
        console_putc(digits[--count]);
}

void console_seek(unsigned cell) {
    pos = cell;               /* Caller supplies a valid screen cell. */
    console_set_cursor(cell);
}

unsigned console_cursor_pos() {
    return pos;
}

void terminal_window_draw(void *context)
{
    (void)context;

    /* Call your existing terminal redraw function. */
    terminal_draw();
}
