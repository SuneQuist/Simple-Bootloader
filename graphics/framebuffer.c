#include <stddef.h>
#include "framebuffer.h"

static framebuffer_t fb;

/*
 * Dedicated RAM back buffer. This region is identity-mapped by start.s.
 * Do not allocate it from Tiny64's small Lexbor/shared kernel heap.
 * Reserved physical range: 0x00800000..0x00FFFFFF (8 MiB).
 * Assumes the VM has at least 16 MiB of RAM; see start.s.
 */
#define FB_BACK_ADDRESS ((uintptr_t)0x00800000u)
#define FB_BACK_CAPACITY (8u * 1024u * 1024u)

static uint8_t *back_pixels;
static uint8_t *draw_pixels;
static int frame_active;
static int clip_enabled;
static int clip_x0, clip_y0, clip_x1, clip_y1;

void fb_clip_reset(void) { clip_enabled = 0; }
void fb_clip_set(int x, int y, int w, int h)
{
    clip_enabled = 1;
    clip_x0 = x; clip_y0 = y;
    clip_x1 = x + (w > 0 ? w : 0);
    clip_y1 = y + (h > 0 ? h : 0);
}



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

    /* Avoid integer overflow and writing beyond the reserved RAM. */
    uint64_t bytes = (uint64_t)pitch * height;
    back_pixels = (bytes <= FB_BACK_CAPACITY)
        ? (uint8_t *)FB_BACK_ADDRESS : 0;
    draw_pixels = (uint8_t *)(uintptr_t)fb.pixels;
    frame_active = 0;
    fb_clip_reset();

    return 1;
}

int fb_double_buffered(void)
{
    return back_pixels != 0;
}

void fb_begin_frame(void)
{
    /* All fb_pixel/fb_rect/fb_text calls now target off-screen RAM. */
    draw_pixels = back_pixels ? back_pixels : (uint8_t *)(uintptr_t)fb.pixels;
    frame_active = 1;
}

/*
 * Present a rectangular region of the completed back buffer.
 *
 * Instead of writing every pixel through a C loop, use the x86
 * REP MOVSL instruction to copy an entire scanline span.
 *
 * This reduces loop overhead when presenting dragged windows.
 *
 * Only the damaged rectangle is copied. Everything outside
 * the rectangle remains unchanged on the visible display.
 */
void fb_present_rect(int x, int y, int w, int h)
{
    if (!frame_active)
        return;

    /* Calculate the requested rectangle. */
    int64_t x0 = x;
    int64_t y0 = y;
    int64_t x1 = x0 + w;
    int64_t y1 = y0 + h;

    /* Clip the rectangle to the framebuffer boundaries. */
    if (x0 < 0)
        x0 = 0;

    if (y0 < 0)
        y0 = 0;

    if (x1 > (int64_t)fb.width)
        x1 = fb.width;

    if (y1 > (int64_t)fb.height)
        y1 = fb.height;

    if (back_pixels && x1 > x0 && y1 > y0) {

        /* Number of 32-bit pixels to copy per scanline. */
        size_t row_pixels = (size_t)(x1 - x0);

        for (int64_t py = y0; py < y1; ++py) {

            size_t offset =
                (size_t)py * fb.pitch +
                (size_t)x0 * 4;

            const uint32_t *src =
                (const uint32_t *)(back_pixels + offset);

            uint32_t *dst =
                (uint32_t *)(uintptr_t)(fb.pixels + offset);

            size_t count = row_pixels;

            /*
             * REP MOVSL copies COUNT 32-bit values from
             * RSI to RDI.
             *
             * This avoids executing a C loop for each pixel.
             *
             * CLD ensures that copying proceeds forward.
             */
            __asm volatile ("cld\n" "rep movsl" : "+S"(src),   "+D"(dst),   "+c"(count) : : "memory");
        }
    }

    /*
     * Restore direct framebuffer drawing as the default
     * when a frame is no longer being presented.
     */
    draw_pixels = (uint8_t *)(uintptr_t)fb.pixels;
    frame_active = 0;
}

void fb_present(void)
{
    fb_present_rect(0, 0, (int)fb.width, (int)fb.height);
}

void fb_end_frame(void)
{
    draw_pixels = (uint8_t *)(uintptr_t)fb.pixels;
    frame_active = 0;
}

uint32_t fb_get_pixel(int x, int y)
{
    if (!draw_pixels || x < 0 || y < 0 ||
        (uint32_t)x >= fb.width || (uint32_t)y >= fb.height)
        return 0;
    const uint32_t *pixel = (const uint32_t *)(draw_pixels + (size_t)y * fb.pitch + (size_t)x * 4);
    return *pixel;
}

void fb_pixel(int x, int y, uint32_t color) {
    if (x < 0 || y < 0)
        return;

    if (clip_enabled && (x < clip_x0 || x >= clip_x1 || y < clip_y0 || y >= clip_y1))
        return;

    if ((uint32_t)x >= fb.width ||
        (uint32_t)y >= fb.height)
        return;

    volatile uint32_t *pixel =
        (volatile uint32_t *)(draw_pixels +
            (uint32_t)y * fb.pitch +
            (uint32_t)x * 4);

    *pixel = color;
}

void fb_rect(int x, int y, int w, int h, uint32_t color) {
    if (!draw_pixels || w <= 0 || h <= 0)
        return;

    int64_t x0 = x;
    int64_t y0 = y;
    int64_t x1 = x0 + w;
    int64_t y1 = y0 + h;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > fb.width) x1 = fb.width;
    if (y1 > fb.height) y1 = fb.height;

    if (clip_enabled) {
        if (x0 < clip_x0) x0 = clip_x0;
        if (y0 < clip_y0) y0 = clip_y0;
        if (x1 > clip_x1) x1 = clip_x1;
        if (y1 > clip_y1) y1 = clip_y1;
    }
    if (x1 <= x0 || y1 <= y0)
        return;

    /* Fill directly into the active render target, avoiding one
     * bounds check and function call for each pixel. */
    for (int64_t py = y0; py < y1; ++py) {
        volatile uint32_t *row = (volatile uint32_t *)(
            draw_pixels + (size_t)py * fb.pitch);
        for (int64_t px = x0; px < x1; ++px)
            row[px] = color;
    }
}

// Clear rect
void fb_clear(uint32_t color) {
    if (!draw_pixels)
        return;

    fb_rect(0, 0, (int)fb.width, (int)fb.height, color);
}