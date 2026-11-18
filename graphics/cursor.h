#ifndef CURSOR_H
#define CURSOR_H

void cursor_init(int x, int y);
void cursor_move(int x, int y);

/* Compatibility hooks used by the desktop compositor. */
void cursor_invalidate(void);
void cursor_redraw(void);

/* Overlay on active back buffer; restore its original pixels after present. */
void cursor_composite(void);
void cursor_uncomposite(void);
int cursor_take_damage(int *x, int *y, int *w, int *h);
void cursor_mark_presented(void);

#endif
