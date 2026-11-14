#ifndef MOUSE_H
#define MOUSE_H

#define VGA_WIDTH 80
#define VGA_HEIGHT 25


extern volatile unsigned short * const VGA_MEM;

void selected_range_highlight(signed idx1, signed idx2, unsigned char highlight);
void mouse_absolute_event(unsigned x, unsigned y, int left_down);
void mouse_reset_packet(void);
void handle_mouse(unsigned code);

#endif
