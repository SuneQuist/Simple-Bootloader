#include "console.h"
#include "../peripherals/keyboard.h"
#include "tty.h"

/* Return 1 for a complete line, or 0 if it was too long. */
signed tty_readline(char *buf, unsigned cap, int echo) {
    if (cap == 0)
        return 0;

    unsigned len = 0;
    signed too_long = 0;

    for (;;) {
        int key = keyboard_getc();

        if (key == '\n')
            break;

        /* If the line overflowed, consume the rest up to Enter. */
        if (too_long)
            continue;

        if (key == '\b') {
            if (len > 0) {
                --len;
                if (echo)
                    console_putc('\b');
            }
        } else if (key >= ' ' && key <= '~') {
            if (len == cap - 1) {
                too_long = 1;
                continue;
            }

            buf[len++] = (char)key;
            if (echo)
                console_putc((char)key);
        }
        /* Ignore arrow keys and other special key codes for now. */
    }

    buf[len] = '\0';
    console_putc('\n');
    return !too_long;
}
