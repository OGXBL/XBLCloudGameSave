#ifndef UI_FONT_RENDER_H
#define UI_FONT_RENDER_H

#include <stdint.h>

#define UI_FONT_CHAR_W 8
#define UI_FONT_CHAR_H 16

void ui_fontDrawChar(int x, int y, unsigned char c, uint32_t fg);
void ui_fontPrintAt(int x, int y, uint32_t fg, const char *text);

#endif
