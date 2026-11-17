#ifndef MOUSE_H
#define MOUSE_H

#define VGA_WIDTH 80
#define VGA_HEIGHT 25


extern volatile unsigned short * const VGA_MEM;

void mouse_graphics_enable(void);
void selected_range_highlight(signed idx1, signed idx2, unsigned char highlight);
void mouse_absolute_event(unsigned x, unsigned y, int left_down);
void mouse_reset_packet(void);
void handle_mouse(unsigned code);
void mouse_graphics_refresh(void);
void mouse_set_packet_size(unsigned size);
int mouse_take_wheel(void);

#endif
