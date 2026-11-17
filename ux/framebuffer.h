#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>

typedef struct {
    volatile uint8_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
} framebuffer_t;

int framebuffer_init(uintptr_t address,
                     uint32_t width,
                     uint32_t height,
                     uint32_t pitch,
                     uint32_t bpp);

void fb_pixel(int x, int y, uint32_t color);
void fb_rect(int x, int y, int w, int h, uint32_t color);
void fb_clear(uint32_t color);

#endif