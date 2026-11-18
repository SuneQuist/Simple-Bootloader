#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>

typedef struct {
    volatile uint8_t *pixels; /* Visible framebuffer mapping. */
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

/* Begin/end one complete desktop frame. If the reserved back buffer is
 * unavailable, drawing safely falls back to the visible framebuffer. */
void fb_begin_frame(void);
void fb_present(void);
/* Present only a clipped rectangle; the rest of the visible image stays put. */
void fb_present_rect(int x, int y, int w, int h);
/* Finish an off-screen edit without copying it to the display. */
void fb_end_frame(void);
uint32_t fb_get_pixel(int x, int y);
int fb_double_buffered(void);

/* Optional rectangular clipping for all drawing primitives. */
void fb_clip_set(int x, int y, int w, int h);
void fb_clip_reset(void);

void fb_pixel(int x, int y, uint32_t color);
void fb_rect(int x, int y, int w, int h, uint32_t color);
void fb_clear(uint32_t color);

#endif