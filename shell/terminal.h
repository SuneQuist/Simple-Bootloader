#ifndef TERMINAL_H
#define TERMINAL_H

void terminal_init(void);
void terminal_update(void);
void terminal_draw(void);

void terminal_flush(void);
int terminal_active(void);
void terminal_putc(char c);
void terminal_print(const char *text);
void terminal_clear(void);
void terminal_line_end(void);
void terminal_scroll(int lines);

#endif