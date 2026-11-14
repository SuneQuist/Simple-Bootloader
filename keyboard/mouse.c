#include "mouse.h"
#include "../console/console.h"

volatile unsigned short * const VGA_MEM = (volatile unsigned short *)0xB8000;

// Mouse state
static signed mouse_x = 40, mouse_y = 12;
static signed sub_x = 0, sub_y = 0; // Sub-cell pixel accumulator
static unsigned char prev_left = 0;
static unsigned char has_selected = 0;
static signed selection_start = 0, selection_end = 0;

// Packet assembly buffer
static unsigned short mouse_cycle = 0;
static unsigned short mouse_packet[3];

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
    signed dx = (int)mouse_packet[1] - ((flags & 0x10) ? 256 : 0);
    signed dy = (int)mouse_packet[2] - ((flags & 0x20) ? 256 : 0);

    // Scale raw mouse mickes to 80x25 text coordinates (8x16 pixel per chunk)
    sub_x += dx;
    sub_y -= dy; // Inver Y axis
    
    mouse_x += sub_x / 8;
    sub_x %= 8;
    
    mouse_y += sub_y / 16;
    sub_y %= 16;

    // Clamp to 80x25 screen bounds
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_x >= VGA_WIDTH) mouse_x = VGA_WIDTH - 1;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_y >= VGA_HEIGHT) mouse_y = VGA_HEIGHT - 1;

    signed current_idx = (mouse_y * VGA_WIDTH) + mouse_x;

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
}
