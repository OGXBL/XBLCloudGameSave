#ifndef UI_FONT_RENDER_H
#define UI_FONT_RENDER_H

#include <stdint.h>

#define UI_FONT_CHAR_W 8
#define UI_FONT_CHAR_H 16

/* Tighter tracking than the old +1 gap reads cleaner / more modern. */
#define UI_FONT_TRACKING 1

void ui_fontDrawChar(int x, int y, unsigned char c, uint32_t fg);
void ui_fontPrintAt(int x, int y, uint32_t fg, const char *text);

/* Same as ui_fontPrintAt but lays a soft shadow underneath the glyphs first,
 * which gives the flat bitmap font a bit of depth (modern XBMC-style look). */
void ui_fontPrintShadow(int x, int y, uint32_t fg, uint32_t shadow, const char *text);

/* Integer-scaled glyphs for headings/titles. scale >= 1. */
void ui_fontDrawCharScaled(int x, int y, unsigned char c, uint32_t fg, int scale);
void ui_fontPrintScaled(int x, int y, uint32_t fg, const char *text, int scale);
void ui_fontPrintScaledShadow(int x, int y, uint32_t fg, uint32_t shadow, const char *text,
                              int scale);

/* Pixel width a string would occupy at the given scale (scale 1 = normal). */
int ui_fontTextWidth(const char *text, int scale);

#endif
