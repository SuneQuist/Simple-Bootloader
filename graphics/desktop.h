#ifndef DESKTOP_H
#define DESKTOP_H

void desktop_draw(void);
void desktop_redraw(void);
/* Present a moving cursor without repainting every window. */
void desktop_cursor_refresh(void);

#endif