#include <stdint.h>
#include "console/console.h"
#include "filesystem/fs.h"
#include "peripherals/keyboard.h"
#include "shell/shell.h"
#include "shell/login.h"
#include "account/account.h"
#include "networking/timer.h"
#include "ux/framebuffer.h"
#include "ux/desktop.h"
#include "peripherals/mouse.h"
#include "peripherals/vmmouse.h"
#include "ux/cursor.h"
#include "peripherals/interrupts.h"
#include "ux/font.h"

extern volatile uint32_t irq12_count;
extern volatile uint32_t mouse_byte_count;

// Framebuffer .globals found in start.s
extern uint8_t fb_active;

extern uint32_t fb_virtual;
extern uint32_t fb_width;
extern uint32_t fb_height;
extern uint32_t fb_pitch;
extern uint32_t fb_bpp;

extern uint8_t vbe_status; // diagnostic status
extern uint8_t vbe_attributes;
extern uint8_t vbe_bpp_detected;
extern uint32_t vbe_width_detected;
extern uint32_t vbe_height_detected;

static void activate_framebuffer(void) {
    if (!fb_active) {
        volatile uint16_t *vga =
            (volatile uint16_t *)0xB8000;

        const char hex[] = "0123456789ABCDEF";

        vga[0] = 0x4F56; /* V */
        vga[1] = 0x4F30 + vbe_status;
        vga[2] = 0x4F20; /* space */

        vga[3] = 0x4F00 | hex[vbe_attributes >> 4];
        vga[4] = 0x4F00 | hex[vbe_attributes & 0x0F];

        vga[6] = 0x4F00 | hex[vbe_bpp_detected >> 4];
        vga[7] = 0x4F00 | hex[vbe_bpp_detected & 0x0F];

        for (;;) {
            __asm volatile ("hlt");
        }
    }

    if (framebuffer_init(
            (uintptr_t)fb_virtual,
            fb_width,
            fb_height,
            fb_pitch,
            fb_bpp)) {

        desktop_draw();

        return;
    }

    fb_clear(0x00202020);

    /* Blue bar: framebuffer width */
    fb_rect(0, 20, (int)fb_width, 20, 0x000000FF);

    /* Green bar: framebuffer height */
    fb_rect(0, 60, (int)fb_height, 20, 0x0000FF00);

    /* Yellow background, black I */
    volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
    vga[0] = 0xE049;

    for (;;) {
        __asm volatile ("hlt");
    }
}

static int cpu_interrupts_enabled(void)
{
    uint64_t flags;

    __asm volatile ("pushfq\n" "popq %0" : "=r"(flags));

    return (flags & (1ULL << 9)) != 0;
}

static inline uint8_t ps2_status(void)
{
    uint8_t status;

    __asm volatile ("inb %1, %0" : "=a"(status) : "Nd"((uint16_t)0x64));

    return status;
}

void kernel_main(void)
{
    activate_framebuffer();

    keyboard_init();

    /*
     * Install the IDT and configure the PIC.
     * CPU interrupts remain disabled.
     */
    interrupts_init();

    int mouse_ok = keyboard_enable_mouse();
if (fb_active) {
    mouse_graphics_enable();

    if (mouse_ok)
        interrupts_enable();

    #define TEXT_X          180
    #define TEXT_Y          395
    #define TEXT_SCALE      2
    #define TEXT_COLS       38
    #define TEXT_ROWS       5
    #define TEXT_ADVANCE    14
    #define TEXT_LINE_HEIGHT 30

    static char lines[TEXT_ROWS][TEXT_COLS + 1];
    static unsigned row = 0;
    static unsigned col = 0;

    for (;;) {
        mouse_graphics_refresh();

        int key = keyboard_try_getc();

        if (key >= 0) {
            int changed = 0;

            if (key == '\b') {
                if (col > 0) {
                    lines[row][--col] = '\0';
                    changed = 1;
                }
            }
            else if (key == '\n') {
                if (row + 1 < TEXT_ROWS) {
                    row++;
                    col = 0;
                    changed = 1;
                }
            }
            else if (key >= 32 && key <= 126) {
                if (col >= TEXT_COLS) {
                    if (row + 1 < TEXT_ROWS) {
                        row++;
                        col = 0;
                    } else {
                        continue;
                    }
                }

                lines[row][col++] = (char)key;
                lines[row][col] = '\0';
                changed = 1;
            }

            if (changed) {
                /*
                 * Clear only the keyboard test area.
                 */
                fb_rect(
                    TEXT_X,
                    TEXT_Y,
                    TEXT_COLS * TEXT_ADVANCE,
                    TEXT_ROWS * TEXT_LINE_HEIGHT,
                    0x00FFFFFF
                );

                /*
                 * Redraw every line.
                 */
                for (unsigned i = 0; i < TEXT_ROWS; i++) {
                    fb_text(
                        TEXT_X,
                        TEXT_Y + i * TEXT_LINE_HEIGHT,
                        lines[i],
                        0x0023344B,
                        TEXT_SCALE
                    );
                }
            }
        }

        __asm volatile ("pause");
    }
}
}