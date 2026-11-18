#include <stdint.h>
#include "mouse.h"
#include "vmmouse.h"
#include "../console/console.h"
#include "../graphics/cursor.h"
#include "../graphics/framebuffer.h"
#include "../graphics/window.h"

extern uint32_t fb_width;
extern uint32_t fb_height;

static int graphics_mouse = 0;
static int gui_mouse_x = 0;
static int gui_mouse_y = 0;
static int gui_left_down = 0;

volatile unsigned short * const VGA_MEM = (volatile unsigned short *)0xB8000;

enum { X_COUNTS_PER_CELL = 8, Y_COUNTS_PER_CELL = 16 };

// Mouse state
static signed mouse_px = 0;
static signed mouse_py = 0;
static unsigned char prev_left = 0;
static unsigned char has_selected = 0;
static signed selection_start = 0, selection_end = 0;

// Packet assembly buffer
static unsigned short mouse_cycle = 0;
static unsigned short mouse_packet[4];

static unsigned mouse_packet_size = 3;
static volatile int mouse_wheel_pending = 0;

static int clamp(int n, int max) {
    if (n < 0) return 0;
    if (n > max) return max;
    return n;
}

void mouse_graphics_enable(void)
{
    graphics_mouse = 1;

    gui_mouse_x = (int)fb_width / 2;
    gui_mouse_y = (int)fb_height / 2;

    cursor_init(gui_mouse_x, gui_mouse_y);
}

static volatile int mouse_position_changed = 0;

static void mouse_graphics_event(
    int dx,
    int dy,
    int left_down
) {
    gui_left_down = left_down;

    gui_mouse_x += dx;
    gui_mouse_y -= dy;

    if (gui_mouse_x < 0)
        gui_mouse_x = 0;

    if (gui_mouse_y < 0)
        gui_mouse_y = 0;

    if (gui_mouse_x >= (int)fb_width)
        gui_mouse_x = (int)fb_width - 1;

    if (gui_mouse_y >= (int)fb_height)
        gui_mouse_y = (int)fb_height - 1;

    mouse_position_changed = 1;
}

void mouse_graphics_refresh(void)
{
    if (!graphics_mouse)
        return;

    int x, y;
    int changed;

    /*
     * Copy the IRQ-updated position without allowing
     * the mouse interrupt to modify it halfway through.
     */
    uint64_t flags;

    __asm volatile ("pushfq\n" "popq %0\n" "cli" : "=r"(flags) : : "memory");

    changed = mouse_position_changed;
    x = gui_mouse_x;
    y = gui_mouse_y;
    mouse_position_changed = 0;

    if (flags & (1ULL << 9))
        __asm volatile ("sti" ::: "memory");

    if (changed)
        cursor_move(x, y);
}

// Helper: Invert foreground/background colors.
void selected_range_highlight(signed idx1, signed idx2, unsigned char highlight) {
    // Revese is dependent on if you go back or forward with selecting
    signed start = (idx1 < idx2) ? idx1 : idx2;
    signed end   = (idx1 > idx2) ? idx1 : idx2;

    if (start < 0) start = 0;
    if (end >= VGA_WIDTH * VGA_HEIGHT) 
        end = (VGA_WIDTH * VGA_HEIGHT) - 1;
    
    for (signed i = start; i <= end; i++) {
        unsigned short cell = VGA_MEM[i];
        unsigned char  ch = cell & 0xFF;
        //unsigned char attr = (cell >> 8) & 0xFF;

        // Swap upper 4 bits (bg) and lower 4 bits (fg) to highlight/unhighlight
        // Track state so we don't double-invert
        unsigned char new_attr = highlight ? 0x1F : 0x0F;
        VGA_MEM[i] = (unsigned short)ch | ((unsigned short)new_attr << 8);
    }
}

static void mouse_select_cell(int cell, int left_down) {
    /* Move the VGA blink without changing the console's output position. */
    //console_set_cursor((unsigned)cell);

    if (left_down && !prev_left) {
        if (has_selected)
            selected_range_highlight(selection_start, selection_end, 0);

        selection_start = cell;
        selection_end = cell;
        has_selected = 1;
        selected_range_highlight(selection_start, selection_end, 1);
    } else if (left_down && has_selected && cell != selection_end) {
        selected_range_highlight(selection_start, selection_end, 0);
        selection_end = cell;
        selected_range_highlight(selection_start, selection_end, 1);
    }

    prev_left = left_down != 0;
}

void mouse_absolute_event(unsigned x, unsigned y, int left_down) {
    if (graphics_mouse) {
        gui_mouse_x = (int)(((uint64_t)x * fb_width) >> 16);
        gui_mouse_y = (int)(((uint64_t)y * fb_height) >> 16);

        if (gui_mouse_x >= (int)fb_width)
            gui_mouse_x = (int)fb_width - 1;

        if (gui_mouse_y >= (int)fb_height)
            gui_mouse_y = (int)fb_height - 1;

        gui_left_down = left_down;

    
        mouse_position_changed = 1;
        return;
    }

    unsigned col = vmmouse_cell(x, VGA_WIDTH);
    unsigned row = vmmouse_cell(y, VGA_HEIGHT);

    mouse_select_cell((int)(row * VGA_WIDTH + col), left_down);
}

void mouse_reset_packet(void) {
    mouse_cycle = 0;
    prev_left = 0;
}

void mouse_set_packet_size(unsigned size)
{
    if (size != 3 && size != 4)
        return;

    mouse_packet_size = size;
    mouse_cycle = 0;
}

int mouse_take_wheel(void)
{
    uint64_t flags;

    __asm volatile ("pushfq\n" "popq %0\n" "cli" : "=r"(flags) : : "memory");

    int wheel = mouse_wheel_pending;
    mouse_wheel_pending = 0;

    if (flags & (1ULL << 9))
        __asm volatile ("sti" ::: "memory");

    return wheel;
}

void handle_mouse(unsigned code) {
    if (graphics_mouse) {
        //fb_rect(40, 10, 20, 20, 0x0000FF00);
    }

    // Byte 0 sanity check: Bit 3 must always be 1 in standard PS/2 packets
    /* The first byte must have bit 3 set. */
    if (mouse_cycle == 0 && !(code & 0x08))
        return;

    mouse_packet[mouse_cycle++] = (unsigned char)code;

    if (mouse_cycle < mouse_packet_size)
        return;

    mouse_cycle = 0;

    unsigned flags = mouse_packet[0];
    unsigned char left_btn = (flags & 0x01) != 0;

    int dx = (int)mouse_packet[1] -
            ((flags & 0x10) ? 256 : 0);

    int dy = (int)mouse_packet[2] -
            ((flags & 0x20) ? 256 : 0);

    /* Only four-byte packets contain wheel data. */
    if (mouse_packet_size == 4) {
        int wheel = mouse_packet[3] & 0x0F;

        if (wheel & 0x08)
            wheel -= 16;

        mouse_wheel_pending += wheel;
    }

    if (graphics_mouse) {
        mouse_graphics_event(dx, dy, left_btn);
        return;
    }

    /* Skip movement on an axis that overflowed, but still handle clicks. */
    if (!(flags & 0x40))
        mouse_px = clamp(mouse_px + dx, VGA_WIDTH * X_COUNTS_PER_CELL - 1);
    if (!(flags & 0x80))
        mouse_py = clamp(mouse_py + dy, VGA_HEIGHT * Y_COUNTS_PER_CELL - 1);

    int col = mouse_px / X_COUNTS_PER_CELL;
    int row = mouse_py / Y_COUNTS_PER_CELL;
    int current_idx = row * VGA_WIDTH + col;
    console_set_cursor((unsigned)current_idx);
    
    mouse_select_cell(current_idx, left_btn);
    /*
    // Select / Highlight State Machine
    if (left_btn && !prev_left) {
        if (has_selected) {
            selected_range_highlight(selection_start, selection_end, 0);
        }
        selection_start = current_idx;
        selection_end   = current_idx;
        has_selected = 1;
        selected_range_highlight(selection_start, selection_end, 1);
    }
    else if (left_btn && prev_left) {
        if (has_selected && current_idx != selection_end) {
            selected_range_highlight(selection_start, selection_end, 0);
            selection_end = current_idx;
            selected_range_highlight(selection_start, selection_end, 1);
        }    
    }

    prev_left = left_btn;
    */
}

void mouse_get_gui_state(int *x, int *y, int *left_down)
{
    uint64_t flags;

    __asm volatile ("pushfq\n" "popq %0\n" "cli" : "=r"(flags) : : "memory");

    *x = gui_mouse_x;
    *y = gui_mouse_y;
    *left_down = gui_left_down;

    if (flags & (1ULL << 9))
        __asm volatile ("sti" ::: "memory");
}