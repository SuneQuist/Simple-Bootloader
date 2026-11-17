#include <stdint.h>
#include "font.h"
#include "framebuffer.h"
#include "cozette_data.h"

/*
 * Cozette bitmap dimensions.
 *
 * Each glyph is stored in 7 columns and 13 rows.
 * Character advance remains 6 pixels.
 */
#define FONT_BITMAP_WIDTH 7
#define FONT_ADVANCE      7
#define FONT_HEIGHT       13
#define FONT_LINE_HEIGHT  15

static const uint8_t *find_glyph(char c)
{
    unsigned char code = (unsigned char)c;

    if (code < 32 || code > 126)
        code = '?';

    return cozette_data[code - 32];
}

void fb_char(int x, int y, char c, uint32_t color, int scale)
{
    if (scale <= 0)
        return;

    const uint8_t *glyph = find_glyph(c);

    for (int row = 0; row < FONT_HEIGHT; row++) {
        uint8_t bits = glyph[row];

        for (int col = 0; col < FONT_BITMAP_WIDTH; col++) {

            /*
             * The converter stores the leftmost pixel
             * in bit 7 (0x80).
             */
            if (!(bits & (0x80u >> col)))
                continue;

            for (int sy = 0; sy < scale; sy++) {
                for (int sx = 0; sx < scale; sx++) {
                    fb_pixel(
                        x + col * scale + sx,
                        y + row * scale + sy,
                        color
                    );
                }
            }
        }
    }
}

void fb_text(int x, int y, const char *text,
             uint32_t color, int scale)
{
    if (!text || scale <= 0)
        return;

    int start_x = x;

    while (*text) {
        if (*text == '\n') {
            x = start_x;
            y += FONT_LINE_HEIGHT * scale;
        } else {
            fb_char(x, y, *text, color, scale);
            x += FONT_ADVANCE * scale;
        }

        text++;
    }
}