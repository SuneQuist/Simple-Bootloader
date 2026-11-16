#include "mouse.h"
#include "../console/console.h"

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
static unsigned short mouse_packet[3];

static int clamp(int n, int max) {
    if (n < 0) return 0;
    if (n > max) return max;
    return n;
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
    unsigned col = vmmouse_cell(x, VGA_WIDTH);
    unsigned row = vmmouse_cell(y, VGA_HEIGHT);

    mouse_select_cell((int)(row * VGA_WIDTH + col), left_down);
}

void mouse_reset_packet(void) {
    mouse_cycle = 0;
    prev_left = 0;
}

void handle_mouse(unsigned code) {
    // Byte 0 sanity check: Bit 3 must always be 1 in standard PS/2 packets
    if (mouse_cycle == 0 && !(code & 0x08))
        return;

    mouse_packet[mouse_cycle++] = code;

    if (mouse_cycle < 3)
        return;

    mouse_cycle = 0;

    // Decode 3-byte packet
    unsigned char left_btn = (mouse_packet[0] & 0x01) != 0;
    unsigned flags = mouse_packet[0];
    // Checks if the ninth bit is 1 or 0, if its Set then -256 with current mouse position x, otherwise -0; Same for Y.
    signed dx = (int)mouse_packet[1] - ((flags & 0x10) ? 256 : 0);
    signed dy = (int)mouse_packet[2] - ((flags & 0x20) ? 256 : 0);

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
