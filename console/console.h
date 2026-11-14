#ifndef CONSOLE_H
#define CONSOLE_H

void console_set_cursor(unsigned cell);
void console_clear(void);
void console_line_end(void);
void console_putc(char c);
void console_write(const char *text, unsigned len);
void console_print(const char *text);
unsigned console_cursor_pos(void);
void console_seek(unsigned cell);

#endif
