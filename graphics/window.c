#include <stddef.h>
#include "window.h"
#include "framebuffer.h"
#include "font.h"
#include "desktop.h"

extern uint32_t fb_width;
extern uint32_t fb_height;

#define TITLE_ACTIVE   0x0023344B
#define TITLE_INACTIVE 0x00596B80
#define TITLE_TEXT     0x00FFFFFF
#define CLOSE_RED      0x00D94A4A
#define MINIMIZE_BLUE  0x003F658F

#define TASKBAR_BG       0x00131D2B
#define TASKBAR_BUTTON   0x002D4562
#define TASKBAR_ACTIVE   0x002F80ED
#define TASKBAR_HIDDEN   0x00202F42
#define TASKBAR_TEXT     0x00FFFFFF

#define CLOSE_WIDTH 24
#define CLOSE_HEIGHT 20

#define TASKBAR_START_X 112
#define TASKBAR_BUTTON_W 150
#define TASKBAR_BUTTON_H 30
#define TASKBAR_BUTTON_GAP 8

static window_t windows[WINDOW_MAX];
static int window_count = 0;

static int dragging_window = -1;
static int drag_offset_x = 0;
static int drag_offset_y = 0;
static int previous_left_down = 0;

static int window_needs_redraw = 0;
static int damage_valid;
static int damage_x0, damage_y0, damage_x1, damage_y1;

/*
 * Full presentation is required for focus, stacking,
 * taskbar state, and window visibility changes.
 */
static int window_full_redraw = 0;

/**
 * Currently focused window.
 *
 * -1 means no window is focused.
 */
static int focused_window = -1;

/**
 * Incrementing stacking order.
 *
 * Higher layer values are drawn later, so they
 * appear in front of lower-layer windows.
 */
static int next_focus_layer = 100;

/**
 * A focus change can alter which window covers
 * another window.
 *
 * A full redraw is required when stacking changes.
 */
static int window_focus_redraw = 0;

static void window_request_full(void)
{
    window_needs_redraw = 1;
    window_full_redraw = 1;
}

int window_take_full_redraw(void)
{
    int result = window_full_redraw;
    window_full_redraw = 0;
    return result;
}

static int window_title_y(const window_t *w)
{
    if (w->title_above)
        return w->y - WINDOW_TITLE_HEIGHT;

    return w->y;
}

static void window_visual_bounds(
    const window_t *w,
    int *x, int *y, int *width, int *height)
{
    int top = w->y;

    if (w->decorated && w->title_above)
        top -= WINDOW_TITLE_HEIGHT;

    *x = w->x;
    *y = top;
    *width = w->width;
    *height = w->height + (w->y - top);
}

static void window_damage_rect(int x, int y, int w, int h)
{
    /* Include the browser shadow (5px), with a little safety margin. */
    int x0 = x - 2;
    int y0 = y - 2;
    int x1 = x + w + 8;
    int y1 = y + h + 8;

    if (!damage_valid) {
        damage_x0 = x0;
        damage_y0 = y0;
        damage_x1 = x1;
        damage_y1 = y1;
        damage_valid = 1;
    } else {
        if (x0 < damage_x0) damage_x0 = x0;
        if (y0 < damage_y0) damage_y0 = y0;
        if (x1 > damage_x1) damage_x1 = x1;
        if (y1 > damage_y1) damage_y1 = y1;
    }
}

static void window_damage_window(const window_t *w)
{
    int x, y, width, height;

    window_visual_bounds(w, &x, &y, &width, &height);
    window_damage_rect(x, y, width, height);
}

int window_take_damage(int *x, int *y, int *w, int *h)
{
    if (!damage_valid)
        return 0;

    if (x != NULL) *x = damage_x0;
    if (y != NULL) *y = damage_y0;
    if (w != NULL) *w = damage_x1 - damage_x0;
    if (h != NULL) *h = damage_y1 - damage_y0;

    damage_valid = 0;
    return 1;
}

static void window_copy_title(char *dst, const char *src)
{
    int i = 0;

    if (src == NULL)
        src = "Untitled";

    while (src[i] != '\0' && i < WINDOW_TITLE_MAX - 1) {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

void window_init(void)
{
    window_count = 0;
    dragging_window = -1;
    drag_offset_x = drag_offset_y = 0;
    previous_left_down = 0;

    window_needs_redraw = 0;
    window_full_redraw = 0;
    window_focus_redraw = 0;
    damage_valid = 0;

    focused_window = -1;
    next_focus_layer = 100;

    for (int i = 0; i < WINDOW_MAX; i++) {
        windows[i].visible = 0;
        windows[i].draw = NULL;
        windows[i].context = NULL;

        windows[i].decorated = 0;
        windows[i].title_above = 0;
        windows[i].closable = 0;
        windows[i].minimized = 0;
        windows[i].title[0] = '\0';
    }
}

int window_add(window_draw_fn draw, void *context,
               int x, int y, int width, int height, int layer)
{
    if (window_count >= WINDOW_MAX ||
        draw == NULL ||
        width <= 0 ||
        height <= 0)
        return -1;

    int id = window_count++;

    windows[id].visible = 1;
    windows[id].layer = layer;

    windows[id].x = x;
    windows[id].y = y;
    windows[id].width = width;
    windows[id].height = height;

    windows[id].draw = draw;
    windows[id].context = context;

    windows[id].decorated = 0;
    windows[id].title_above = 0;
    windows[id].closable = 0;
    windows[id].minimized = 0;

    window_copy_title(windows[id].title, "Untitled");

    window_request_full();
    return id;
}

/*
 * Find the highest visible window.
 *
 * This is used to restore focus after closing
 * or minimizing the currently active window.
 */
static int window_topmost(void)
{
    int best = -1;

    for (int i = 0; i < window_count; i++) {
        if (!windows[i].visible)
            continue;

        if (best < 0 ||
            windows[i].layer > windows[best].layer ||
            (windows[i].layer == windows[best].layer && i > best))
            best = i;
    }

    return best;
}

void window_set_visible(int id, int visible)
{
    if (id < 0 || id >= window_count)
        return;

    window_t *w = &windows[id];
    int new_visible = visible ? 1 : 0;

    /*
     * Explicit visibility changes clear minimized
     * state. A close button hides without minimizing.
     */
    int changed =
        w->visible != new_visible || w->minimized != 0;

    if (!changed)
        return;

    if (w->visible)
        window_damage_window(w);

    w->visible = new_visible;
    w->minimized = 0;

    /**
     * A hidden window cannot remain focused.
     */
    if (!w->visible && focused_window == id) {
        focused_window = window_topmost();
        window_focus_redraw = 1;
    }

    if (dragging_window == id && !w->visible)
        dragging_window = -1;

    if (w->visible)
        window_damage_window(w);

    window_request_full();
}

void window_set_layer(int id, int layer)
{
    if (id < 0 || id >= window_count)
        return;

    if (windows[id].layer == layer)
        return;

    windows[id].layer = layer;

    window_request_full();
}

void window_set_decorations(
    int id, int decorated, int title_above, int closable)
{
    if (id < 0 || id >= window_count)
        return;

    window_t *w = &windows[id];

    if (w->visible)
        window_damage_window(w);

    w->decorated = decorated ? 1 : 0;
    w->title_above = title_above ? 1 : 0;
    w->closable = closable ? 1 : 0;

    if (w->visible)
        window_damage_window(w);

    window_request_full();
}

void window_set_title(int id, const char *title)
{
    if (id < 0 || id >= window_count)
        return;

    window_copy_title(windows[id].title, title);

    /*
     * Both the title bar and taskbar may change.
     */
    window_request_full();
}

/*
 * Hide a window without destroying its contents.
 *
 * Unlike closing, this sets minimized = 1.
 */
void window_minimize(int id)
{
    if (id < 0 || id >= window_count)
        return;

    window_t *w = &windows[id];

    if (!w->visible)
        return;

    window_damage_window(w);

    w->visible = 0;
    w->minimized = 1;

    if (dragging_window == id)
        dragging_window = -1;

    if (focused_window == id) {
        focused_window = window_topmost();
        window_focus_redraw = 1;
    }

    window_request_full();
}

/*
 * Restore a minimized or closed window.
 *
 * The application is not recreated: its original
 * draw callback and context are reused.
 */
void window_restore(int id)
{
    if (id < 0 || id >= window_count)
        return;

    if (!windows[id].visible) {
        windows[id].visible = 1;
        windows[id].minimized = 0;
        window_damage_window(&windows[id]);
    }

    /*
     * Raise and focus the restored window.
     */
    window_focus(id);
    window_request_full();
}

int window_is_minimized(int id)
{
    return id >= 0 &&
           id < window_count &&
           windows[id].minimized;
}

/*
 * Draw shared window decorations after application
 * contents, keeping title bars above page rendering.
 */
static void window_draw_title(int id)
{
    window_t *w = &windows[id];

    if (!w->decorated)
        return;

    int title_y = window_title_y(w);

    uint32_t background =
        focused_window == id ? TITLE_ACTIVE : TITLE_INACTIVE;

    fb_rect(
        w->x,
        title_y,
        w->width,
        WINDOW_TITLE_HEIGHT,
        background
    );

    /*
     * Reserve space for both minimize and close.
     *
     * Our simple framebuffer font is approximately
     * 7 pixels wide per character.
     */
    char visible_title[WINDOW_TITLE_MAX];

    int available_width = w->width - 24 - 40;

    if (w->closable)
        available_width -= 30;

    int max_chars = available_width / 7;

    if (max_chars < 0)
        max_chars = 0;

    if (max_chars > WINDOW_TITLE_MAX - 1)
        max_chars = WINDOW_TITLE_MAX - 1;

    int i = 0;

    while (i < max_chars && w->title[i] != '\0') {
        visible_title[i] = w->title[i];
        i++;
    }

    visible_title[i] = '\0';

    fb_text(
        w->x + 12,
        title_y + 11,
        visible_title,
        TITLE_TEXT,
        1
    );

    /*
     * Every decorated window gets a minimize button.
     */
    int min_x = w->x + w->width - 64;
    int min_y = title_y + 8;

    fb_rect(
        min_x,
        min_y,
        CLOSE_WIDTH,
        CLOSE_HEIGHT,
        MINIMIZE_BLUE
    );

    fb_rect(
        min_x + 7,
        min_y + 14,
        10,
        2,
        TITLE_TEXT
    );

    /*
     * The close button remains optional.
     *
     * We keep the terminal non-closable, although it
     * can now be minimized and restored.
     */
    if (w->closable) {
        int close_x = w->x + w->width - 34;
        int close_y = title_y + 8;

        fb_rect(
            close_x,
            close_y,
            CLOSE_WIDTH,
            CLOSE_HEIGHT,
            CLOSE_RED
        );

        fb_text(
            close_x + 8,
            close_y + 3,
            "X",
            TITLE_TEXT,
            1
        );
    }
}

void window_draw_all(void)
{
    unsigned char drawn[WINDOW_MAX] = {0};

    for (int pass = 0; pass < window_count; pass++) {
        int best = -1;

        for (int i = 0; i < window_count; i++) {
            if (drawn[i] ||
                !windows[i].visible ||
                windows[i].draw == NULL)
                continue;

            if (best < 0 ||
                windows[i].layer < windows[best].layer ||
                (windows[i].layer == windows[best].layer &&
                 i < best))
                best = i;
        }

        if (best < 0)
            break;

        drawn[best] = 1;

        windows[best].draw(windows[best].context);
        window_draw_title(best);
    }
}

void window_get_position(int id, int *x, int *y)
{
    if (id < 0 || id >= window_count)
        return;

    if (x != NULL)
        *x = windows[id].x;

    if (y != NULL)
        *y = windows[id].y;
}

static int window_at(int x, int y)
{
    int best = -1;

    for (int i = 0; i < window_count; i++) {
        if (!windows[i].visible)
            continue;

        window_t *w = &windows[i];

        int bx, by, bw, bh;
        window_visual_bounds(w, &bx, &by, &bw, &bh);

        if (x < bx || y < by ||
            x >= bx + bw || y >= by + bh)
            continue;

        if (best < 0 ||
            w->layer > windows[best].layer ||
            (w->layer == windows[best].layer && i > best))
            best = i;
    }

    return best;
}

int window_at_point(int x, int y)
{
    if (y >= (int)fb_height - WINDOW_TASKBAR_HEIGHT)
        return -1;
    return window_at(x, y);
}

static int window_close_hit(const window_t *w, int x, int y)
{
    if (!w->decorated || !w->closable)
        return 0;

    int title_y = window_title_y(w);
    int close_x = w->x + w->width - 34;
    int close_y = title_y + 8;

    return x >= close_x &&
           x < close_x + CLOSE_WIDTH &&
           y >= close_y &&
           y < close_y + CLOSE_HEIGHT;
}

static int window_minimize_hit(const window_t *w, int x, int y)
{
    if (!w->decorated)
        return 0;

    int title_y = window_title_y(w);
    int min_x = w->x + w->width - 64;
    int min_y = title_y + 8;

    return x >= min_x &&
           x < min_x + CLOSE_WIDTH &&
           y >= min_y &&
           y < min_y + CLOSE_HEIGHT;
}

static int window_title_hit(const window_t *w, int x, int y)
{
    if (!w->decorated)
        return 0;

    int title_y = window_title_y(w);

    return x >= w->x &&
           x < w->x + w->width &&
           y >= title_y &&
           y < title_y + WINDOW_TITLE_HEIGHT;
}

/*
 * Return the screen rectangle occupied by one
 * window's taskbar button.
 *
 * The same layout is used for drawing and clicks.
 */
static void window_taskbar_button_bounds(
    int id, int *x, int *y, int *w, int *h)
{
    *x = TASKBAR_START_X +
         id * (TASKBAR_BUTTON_W + TASKBAR_BUTTON_GAP);

    *y = (int)fb_height -
         WINDOW_TASKBAR_HEIGHT + 9;

    *w = TASKBAR_BUTTON_W;
    *h = TASKBAR_BUTTON_H;
}

/*
 * Process a taskbar click.
 *
 * A focused window minimizes when clicked again.
 * Any other window is restored and brought forward.
 */
static void window_taskbar_click(int x, int y)
{
    /*
     * The Tiny64 start button is decorative for now.
     */
    if (x < TASKBAR_START_X)
        return;

    for (int i = 0; i < window_count; i++) {
        int bx, by, bw, bh;

        window_taskbar_button_bounds(
            i, &bx, &by, &bw, &bh
        );

        if (bx + bw > (int)fb_width)
            break;

        if (x < bx || x >= bx + bw ||
            y < by || y >= by + bh)
            continue;

        if (window_is_focused(i)) {
            window_minimize(i);
        } else {
            window_restore(i);
        }

        return;
    }
}

/*
 * Draw the taskbar after all application windows.
 *
 * Buttons remain visible for closed and minimized
 * windows so they can be restored later.
 */
void window_draw_taskbar(void)
{
    int screen_w = (int)fb_width;
    int screen_h = (int)fb_height;

    if (screen_w <= 0 || screen_h <= 0)
        return;

    int bar_y = screen_h - WINDOW_TASKBAR_HEIGHT;

    fb_rect(
        0,
        bar_y,
        screen_w,
        WINDOW_TASKBAR_HEIGHT,
        TASKBAR_BG
    );

    /*
     * Tiny64 start button.
     *
     * We'll give this a real application menu later.
     */
    fb_rect(
        10,
        bar_y + 9,
        90,
        30,
        TASKBAR_ACTIVE
    );

    fb_text(
        23,
        bar_y + 17,
        "Tiny64",
        TASKBAR_TEXT,
        1
    );

    /*
     * One taskbar button per registered window.
     */
    for (int i = 0; i < window_count; i++) {
        int bx, by, bw, bh;

        window_taskbar_button_bounds(
            i, &bx, &by, &bw, &bh
        );

        if (bx + bw > screen_w)
            break;

        uint32_t color;

        if (window_is_focused(i)) {
            color = TASKBAR_ACTIVE;
        } else if (!windows[i].visible) {
            color = TASKBAR_HIDDEN;
        } else {
            color = TASKBAR_BUTTON;
        }

        fb_rect(bx, by, bw, bh, color);

        /*
         * Keep titles within their taskbar buttons.
         *
         * Long HTML titles are shortened here but
         * remain complete in the window_t structure.
         */
        char label[20];
        int j = 0;

        while (j < 18 && windows[i].title[j] != '\0') {
            label[j] = windows[i].title[j];
            j++;
        }

        label[j] = '\0';

        fb_text(
            bx + 10,
            by + 8,
            label,
            TASKBAR_TEXT,
            1
        );

        /*
         * Minimized and closed windows have a small
         * indicator along the bottom of their button.
         */
        if (!windows[i].visible) {
            fb_rect(
                bx + 8,
                by + bh - 4,
                bw - 16,
                2,
                0x007F96AD
            );
        }
    }
}

/**
 * Handle mouse clicks, focus, and window dragging.
 *
 * A new left-button press selects the topmost window
 * under the mouse and brings it to the front.
 *
 * Dragging only begins inside the title bar.
 *
 * Rendering is deferred to the main loop.
 */
void window_mouse_event(int x, int y, int left_down)
{
    int pressed = left_down && !previous_left_down;
    int released = !left_down && previous_left_down;

    previous_left_down = left_down != 0;

    /*
     * The taskbar receives mouse clicks before
     * ordinary windows.
     *
     * This prevents clicking a taskbar button from
     * accidentally focusing or dragging a window.
     */
    if (pressed &&
        y >= (int)fb_height - WINDOW_TASKBAR_HEIGHT)
    {
        dragging_window = -1;
        window_taskbar_click(x, y);
        return;
    }

    /*
     * A new mouse press may select a window.
     */
    if (pressed) {
        int id = window_at(x, y);

        dragging_window = -1;

        if (id >= 0) {
            window_t *w = &windows[id];

            /*
             * Bring the clicked window to the front.
             */
            window_focus(id);

            /*
             * Close-button clicks must not start
             * dragging.
             *
             * Closing hides the window; it does not
             * destroy the application's contents.
             */
            if (window_close_hit(w, x, y)) {
                window_set_visible(id, 0);
                return;
            }

            /*
             * Minimize-button clicks also must not
             * start dragging.
             */
            if (window_minimize_hit(w, x, y)) {
                window_minimize(id);
                return;
            }

            /*
             * Only start dragging from the title bar.
             */
            if (window_title_hit(w, x, y)) {
                dragging_window = id;

                drag_offset_x = x - w->x;
                drag_offset_y = y - w->y;
            }
        }
    }

    /*
     * Releasing the button ends dragging.
     */
    if (released)
        dragging_window = -1;

    /*
     * Move the selected window while the left
     * mouse button remains pressed.
     */
    if (dragging_window >= 0 && left_down) {
        window_t *w = &windows[dragging_window];

        int new_x = x - drag_offset_x;
        int new_y = y - drag_offset_y;

        /*
         * Keep the window inside the framebuffer.
         *
         * Reserve the taskbar's bottom 48 pixels.
         */
        int min_y =
            w->decorated && w->title_above
                ? WINDOW_TITLE_HEIGHT
                : 0;

        int max_x = (int)fb_width - w->width;

        int max_y =
            (int)fb_height -
            WINDOW_TASKBAR_HEIGHT -
            w->height;

        if (max_x < 0)
            max_x = 0;

        if (max_y < min_y)
            max_y = min_y;

        if (new_x < 0)
            new_x = 0;

        if (new_y < min_y)
            new_y = min_y;

        if (new_x > max_x)
            new_x = max_x;

        if (new_y > max_y)
            new_y = max_y;

        /*
         * Only request a redraw when the window
         * actually changes position.
         */
        if (new_x != w->x || new_y != w->y) {

            /*
             * Record the old visual bounds before
             * moving the window.
             */
            window_damage_window(w);

            w->x = new_x;
            w->y = new_y;

            /*
             * Record the new visual bounds too.
             */
            window_damage_window(w);

            window_needs_redraw = 1;
        }
    }
}

int window_take_redraw(void)
{
    int result = window_needs_redraw;
    window_needs_redraw = 0;
    return result;
}

/* Request one coalesced repaint from the kernel's main loop. */
void window_request_redraw(void)
{
    window_needs_redraw = 1;
}

/**
 * Return the ID of the currently focused window.
 */
int window_focused(void)
{
    return focused_window;
}

/**
 * Check whether a particular window has focus.
 */
int window_is_focused(int id)
{
    return id >= 0 &&
           id < window_count &&
           windows[id].visible &&
           focused_window == id;
}

/**
 * Focus a window and bring it to the front.
 *
 * This function changes window-manager state only.
 * The main loop is responsible for repainting.
 */
void window_focus(int id)
{
    if (id < 0 || id >= window_count)
        return;

    if (!windows[id].visible)
        return;

    /*
     * Clicking an already focused window does not
     * need to change its stacking order.
     */
    if (focused_window == id)
        return;

    focused_window = id;

    /*
     * Higher layers appear above lower layers.
     */
    windows[id].layer = next_focus_layer++;

    /*
     * Request a redraw using the existing window
     * manager redraw mechanism.
     */
    window_request_full();

    /*
     * Repaint the entire desktop after a stacking change.
     *
     * Ordinary window movement continues to use the
     * existing dirty-region rendering path.
     */
    window_focus_redraw = 1;
}

int window_take_focus_redraw(void)
{
    int result = window_focus_redraw;
    window_focus_redraw = 0;
    return result;
}
