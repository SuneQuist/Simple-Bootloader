#ifndef FONT_H
#define FONT_H

#include <stdint.h>

void fb_char(int x, int y, char c, uint32_t color, int scale);
void fb_text(int x, int y, const char *text, uint32_t color, int scale);

#endif