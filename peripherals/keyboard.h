#ifndef KEYBOARD_H
#define KEYBOARD_H

enum {
    KEY_UP        = 0x80,
    KEY_DOWN      = 0x81,
    KEY_LEFT      = 0x82,
    KEY_RIGHT     = 0x83,
    KEY_HOME      = 0x84,
    KEY_END       = 0x85,
    KEY_PAGE_UP   = 0x86,
    KEY_PAGE_DOWN = 0x87,
    KEY_INSERT    = 0x88,
    KEY_DELETE    = 0x89
};

enum {
    KEY_MOD_SHIFT = 1,
    KEY_MOD_CTRL  = 2
};

void keyboard_init(void);
signed keyboard_enable_mouse(void);
void keyboard_irq_scancode(unsigned char code);
int keyboard_try_getc_mod(unsigned *mods);
int keyboard_try_getc(void);
int keyboard_getc(void);

#endif
