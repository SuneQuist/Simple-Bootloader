#include <stdint.h>
#include "cursor.h"
#include "framebuffer.h"

extern uint32_t fb_width;
extern uint32_t fb_height;
extern uint32_t fb_pitch;
extern uint32_t fb_virtual;

#define CURSOR_W 12
#define CURSOR_H 12

static const char *shape[CURSOR_H] = {
    "X...........",
    "XX..........",
    "XOX.........",
    "XOOX........",
    "XOOOX.......",
    "XOOOOX......",
    "XOOOOOX.....",
    "XOOOOOOX....",
    "XOOXXXXXX...",
    "XOX.........",
    "XX..........",
    "X..........."
};

/* The back buffer always holds a clean desktop without a cursor.
 * The cursor is composited only while presenting a frame. */
static uint32_t saved[CURSOR_W * CURSOR_H];
static int cursor_x, cursor_y;
static int previous_x, previous_y;
static int cursor_initialized;
static int cursor_composited;
static int cursor_dirty;

static int inside(int x, int y)
{
    return x >= 0 && y >= 0 &&
           (uint32_t)x < fb_width && (uint32_t)y < fb_height;
}

void cursor_init(int x, int y)
{
    cursor_x = previous_x = x;
    cursor_y = previous_y = y;
    cursor_initialized = 1;
    cursor_dirty = 1;
}

void cursor_move(int x, int y)
{
    if (!cursor_initialized || (x == cursor_x && y == cursor_y))
        return;
    cursor_x = x;
    cursor_y = y;
    cursor_dirty = 1;
}

void cursor_invalidate(void)
{
    /* No stale framebuffer background exists with back-buffer compositing. */
}

void cursor_redraw(void)
{
    /* The desktop calls cursor_composite() before presenting instead. */
}

void cursor_composite(void)
{
    if (!cursor_initialized || cursor_composited)
        return;

    for (int y = 0; y < CURSOR_H; ++y) {
        for (int x = 0; x < CURSOR_W; ++x) {
            int sx = cursor_x + x;
            int sy = cursor_y + y;
            if (!inside(sx, sy))
                continue;
            int index = y * CURSOR_W + x;
            saved[index] = fb_get_pixel(sx, sy);
            char c = shape[y][x];
            if (c == 'X') fb_pixel(sx, sy, 0x00000000);
            else if (c == 'O') fb_pixel(sx, sy, 0x00FFFFFF);
        }
    }
    cursor_composited = 1;
}

void cursor_uncomposite(void)
{
    if (!cursor_composited)
        return;
    for (int y = 0; y < CURSOR_H; ++y) {
        for (int x = 0; x < CURSOR_W; ++x) {
            int sx = cursor_x + x;
            int sy = cursor_y + y;
            if (inside(sx, sy))
                fb_pixel(sx, sy, saved[y * CURSOR_W + x]);
        }
    }
    cursor_composited = 0;
}

int cursor_take_damage(int *x, int *y, int *w, int *h)
{
    if (!cursor_initialized || !cursor_dirty)
        return 0;
    int x0 = previous_x < cursor_x ? previous_x : cursor_x;
    int y0 = previous_y < cursor_y ? previous_y : cursor_y;
    int x1 = (previous_x > cursor_x ? previous_x : cursor_x) + CURSOR_W;
    int y1 = (previous_y > cursor_y ? previous_y : cursor_y) + CURSOR_H;
    *x = x0; *y = y0; *w = x1 - x0; *h = y1 - y0;
    return 1;
}

void cursor_mark_presented(void)
{
    previous_x = cursor_x;
    previous_y = cursor_y;
    cursor_dirty = 0;
}
