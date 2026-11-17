#include "framebuffer.h"

static framebuffer_t fb;

// init pixel mode instead of vga text mode, at address
int framebuffer_init(uintptr_t address,
                     uint32_t width,
                     uint32_t height,
                     uint32_t pitch,
                     uint32_t bpp) {
    if (!address || !width || !height)
        return 0;

    /* First version supports 32-bit pixels only. */
    if (bpp != 32)
        return 0;

    if (width > UINT32_MAX / 4 ||
        pitch < width * 4)
        return 0;

    fb.pixels = (volatile uint8_t *)address;
    fb.width = width;
    fb.height = height;
    fb.pitch = pitch;
    fb.bpp = bpp;

    return 1;
}

void fb_pixel(int x, int y, uint32_t color) {
    if (x < 0 || y < 0)
        return;

    if ((uint32_t)x >= fb.width ||
        (uint32_t)y >= fb.height)
        return;

    volatile uint32_t *pixel =
        (volatile uint32_t *)(fb.pixels +
            (uint32_t)y * fb.pitch +
            (uint32_t)x * 4);

    *pixel = color;
}

void fb_rect(int x, int y, int w, int h, uint32_t color) {
    if (!fb.pixels || w <= 0 || h <= 0)
        return;

    int64_t x0 = x;
    int64_t y0 = y;
    int64_t x1 = x0 + w;
    int64_t y1 = y0 + h;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > fb.width) x1 = fb.width;
    if (y1 > fb.height) y1 = fb.height;

    if (x1 <= x0 || y1 <= y0)
        return;

    for (int64_t py = y0; py < y1; ++py) {
        for (int64_t px = x0; px < x1; ++px) {
            fb_pixel((int)px, (int)py, color);
        }
    }
}

// Clear rect
void fb_clear(uint32_t color) {
    if (!fb.pixels)
        return;

    fb_rect(0, 0, (int)fb.width, (int)fb.height, color);
}