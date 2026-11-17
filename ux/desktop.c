#include <stdint.h>
#include "desktop.h"
#include "framebuffer.h"
#include "font.h"

extern uint32_t fb_width;
extern uint32_t fb_height;

static void rect(int x, int y, int w, int h, uint32_t color)
{
    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            fb_pixel(px, py, color);
        }
    }
}

void desktop_draw(void)
{
    int screen_w = (int)fb_width;
    int screen_h = (int)fb_height;

    if (screen_w <= 0 || screen_h <= 0)
        return;
    
    /* Desktop background */
    fb_clear(0x001B3655);

    rect(0, screen_h - 48, screen_w, 48, 0x00131D2B);
    rect(10, screen_h - 38, 90, 28, 0x002F80ED);

    /* Taskbar label */
    fb_text(18, (int)fb_height - 31, "Tiny64", 0x00FFFFFF, 2);

    /* Window shadow */
    rect(154, 124, 608, 408, 0x000D1725);

    /* Window body */
    rect(150, 120, 608, 408, 0x00E8EDF3);

    /* Window title bar */
    rect(150, 120, 608, 36, 0x0023344B);

    /* Close button */
    rect(716, 128, 28, 20, 0x00D94A4A);

    /* Window content area */
    rect(166, 172, 576, 338, 0x00FFFFFF);

    /* Window content */
    /* Window title */
    fb_text(166, 132, "Terminal", 0x00FFFFFF, 2);
    fb_text(186, 196, "Welcome to Tiny64!", 0x0023344B, 2);
    fb_text(186, 226, "Your graphical OS is running.", 0x00556677, 1);
    fb_text(186, 250, "Tiny64> _", 0x002F80ED, 2);
}