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

static uint32_t saved[CURSOR_W * CURSOR_H];
static int cursor_x;
static int cursor_y;
static int cursor_visible;

static int inside(int x, int y)
{
    return x >= 0 && y >= 0 &&
           (uint32_t)x < fb_width &&
           (uint32_t)y < fb_height;
}

static volatile uint32_t *pixel_at(int x, int y)
{
    uintptr_t address =
        (uintptr_t)fb_virtual +
        (uintptr_t)y * fb_pitch +
        (uintptr_t)x * 4;

    return (volatile uint32_t *)address;
}

static void cursor_restore(void)
{
    if (!cursor_visible)
        return;

    for (int y = 0; y < CURSOR_H; y++) {
        for (int x = 0; x < CURSOR_W; x++) {
            int sx = cursor_x + x;
            int sy = cursor_y + y;

            if (inside(sx, sy))
                *pixel_at(sx, sy) = saved[y * CURSOR_W + x];
        }
    }
}

static void cursor_draw(void)
{
    for (int y = 0; y < CURSOR_H; y++) {
        for (int x = 0; x < CURSOR_W; x++) {
            int sx = cursor_x + x;
            int sy = cursor_y + y;

            if (!inside(sx, sy))
                continue;

            volatile uint32_t *pixel = pixel_at(sx, sy);

            saved[y * CURSOR_W + x] = *pixel;

            char c = shape[y][x];

            if (c == 'X')
                *pixel = 0x00000000;
            else if (c == 'O')
                *pixel = 0x00FFFFFF;
        }
    }

    cursor_visible = 1;
}

void cursor_init(int x, int y)
{
    cursor_visible = 0;
    cursor_x = x;
    cursor_y = y;
    cursor_draw();
}

void cursor_move(int x, int y)
{
    if (x == cursor_x && y == cursor_y)
        return;

    cursor_restore();

    cursor_x = x;
    cursor_y = y;

    cursor_draw();
}