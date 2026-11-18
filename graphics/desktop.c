#include <stdint.h>
#include "desktop.h"
#include "framebuffer.h"
#include "font.h"
#include "cursor.h"
#include "window.h"

extern uint32_t fb_width;
extern uint32_t fb_height;

static void rect(int x, int y, int w, int h, uint32_t color)
{
    /* Use the clipped framebuffer rectangle primitive rather than
     * calling fb_pixel() for every desktop background pixel. */
    fb_rect(x, y, w, h, color);
}

void desktop_draw(void)
{
    int screen_w = (int)fb_width;
    int screen_h = (int)fb_height;

    if (screen_w <= 0 || screen_h <= 0)
        return;

    /* Desktop background */
    fb_clear(0x001B3655);

    /*
     * The taskbar is now drawn by window_draw_taskbar()
     * after the application windows.
     *
     * Previously, desktop_draw() drew the taskbar
     * background here. Drawing it last ensures that
     * windows cannot cover it.
     */

    /* Taskbar label */
    //fb_text(18, (int)fb_height - 31, "Tiny64", 0x00FFFFFF, 2);

    /* Window shadow */
    //rect(154, 124, 608, 408, 0x000D1725);

    /* Window body */
    //rect(150, 120, 608, 408, 0x00E8EDF3);

    /* Window title bar */
    //rect(150, 120, 608, 36, 0x0023344B);

    /* Close button */
    //rect(716, 128, 28, 20, 0x00D94A4A);

    /* Window content area */
    //rect(166, 172, 576, 338, 0x00FFFFFF);

    /* Window content */
    /* Window title */
    //fb_text(166, 132, "Terminal", 0x00FFFFFF, 2);
}

/* Present a clean back-buffer scene with a temporary cursor overlay.
 * Afterwards restore the back buffer, so no old cursor pixels linger. */
static void desktop_present_region(int x, int y, int w, int h)
{
    cursor_composite();
    fb_present_rect(x, y, w, h);

    /* fb_present_rect() switches the target back to the visible buffer. */
    fb_begin_frame();
    cursor_uncomposite();
    fb_end_frame();
    cursor_mark_presented();
}

void desktop_cursor_refresh(void)
{
    int x, y, w, h;

    if (!cursor_take_damage(&x, &y, &w, &h))
        return;

    /* No window repaint is needed for cursor-only movement. */
    fb_begin_frame();
    desktop_present_region(x, y, w, h);
}

void desktop_redraw(void)
{
    /* The old cursor background is about to become invalid.
     * The back buffer is now cursor-free between presentations. */
    cursor_invalidate();

    /* Draw the complete scene off-screen. Window rendering is still
     * intentionally full-scene; only presentation is damage-limited. */
    fb_begin_frame();

    /* Draw desktop background */
    desktop_draw();

    /* Draw windows in layer order */
    window_draw_all();

    /*
     * Draw the taskbar above every application window.
     *
     * This includes taskbar buttons for closed and
     * minimized windows.
     */
    window_draw_taskbar();

    /*
     * Read the full-redraw request before consuming
     * the accumulated window damage.
     *
     * Focus changes, minimizing, restoring, and
     * taskbar updates need full-screen presentation.
     *
     * Ordinary dragging can still present only its
     * damaged region.
     */
    int full_redraw = window_take_full_redraw();

    int x, y, w, h;
    int has_damage = window_take_damage(&x, &y, &w, &h);

    if (full_redraw || !has_damage) {
        /* Terminal output, browser opening and other changes repaint all. */
        x = 0;
        y = 0;
        w = (int)fb_width;
        h = (int)fb_height;
    }

    /* Include both old and new cursor locations in the presented area. */
    int cx, cy, cw, ch;

    if (cursor_take_damage(&cx, &cy, &cw, &ch)) {
        int x0 = x < cx ? x : cx;
        int y0 = y < cy ? y : cy;
        int x1 = x + w > cx + cw ? x + w : cx + cw;
        int y1 = y + h > cy + ch ? y + h : cy + ch;

        x = x0;
        y = y0;
        w = x1 - x0;
        h = y1 - y0;
    }

    /* Present the completed frame to the visible framebuffer. */
    desktop_present_region(x, y, w, h);

    /* Draw cursor above everything (now composited in the back buffer). */
}
