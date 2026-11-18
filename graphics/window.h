#ifndef WINDOW_H
#define WINDOW_H

#include <stdint.h>

#define WINDOW_MAX 16
#define WINDOW_TITLE_MAX 128
#define WINDOW_TITLE_HEIGHT 36
#define WINDOW_TASKBAR_HEIGHT 48

typedef void (*window_draw_fn)(void *context);

typedef struct {
    int visible;
    int layer;

    int x;
    int y;
    int width;
    int height;

    window_draw_fn draw;
    void *context;

    /*
     * Window decorations.
     *
     * title_above = 0:
     *     Title bar occupies the top of the window.
     *
     * title_above = 1:
     *     Title bar sits above the window contents.
     *
     * The terminal uses title_above = 1 so its
     * existing text area is not covered.
     */
    int decorated;
    int title_above;
    int closable;

    char title[WINDOW_TITLE_MAX];

    /*
     * Minimized windows remain registered and retain
     * their position, title, and application state.
     *
     * A closed window is also retained, but minimized
     * distinguishes the two actions.
     */
    int minimized;
} window_t;

void window_init(void);

int window_add(window_draw_fn draw, void *context,
               int x, int y, int width, int height, int layer);

void window_set_visible(int id, int visible);
void window_set_layer(int id, int layer);
void window_draw_all(void);

/* Hit test topmost visible window, including its title bar. */
int window_at_point(int x, int y);

void window_mouse_event(int x, int y, int left_down);
void window_get_position(int id, int *x, int *y);
void window_request_redraw(void);
int window_take_redraw(void);

/* A dragged window damages both its previous and current bounds. */
int window_take_damage(int *x, int *y, int *w, int *h);

/*
 * Window focus and stacking.
 *
 * The focused window receives keyboard input.
 * Clicking a window raises it above other windows.
 */
int window_focused(void);
int window_is_focused(int id);
void window_focus(int id);
int window_take_focus_redraw(void);

/*
 * Shared window decorations.
 *
 * The window manager draws the title bar and handles
 * the close button independently of the application.
 */
void window_set_title(int id, const char *title);

void window_set_decorations(int id,
                            int decorated,
                            int title_above,
                            int closable);

/*
 * Taskbar and minimizing.
 *
 * The taskbar is drawn after all application windows.
 * Mouse handling is integrated into window_mouse_event().
 */
void window_minimize(int id);
void window_restore(int id);
int window_is_minimized(int id);
void window_draw_taskbar(void);

/*
 * A full redraw is needed when the taskbar, focus,
 * or stacking order changes.
 */
int window_take_full_redraw(void);

#endif
