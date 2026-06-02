#include "ui_font.h"

#include <hal/video.h>

static const unsigned char ui_systemFont[] = {
#include "font_unscii_16.h"
#include "font_unscii_16.h"
};

static const unsigned char *ui_fontGlyph(unsigned char c)
{
    return ui_systemFont + (c * ((FONT_WIDTH + 7) / 8) * FONT_HEIGHT);
}

void ui_fontDrawChar(int x, int y, unsigned char c, uint32_t fg)
{
    VIDEO_MODE vm = XVideoGetMode();
    volatile uint32_t *fb = (volatile uint32_t *)XVideoGetFB();
    int W = vm.width;
    int H = vm.height;
    const unsigned char *font = ui_fontGlyph(c);
    unsigned char mask;

    for (int h = 0; h < UI_FONT_CHAR_H; h++) {
        mask = 0x80;
        for (int w = 0; w < UI_FONT_CHAR_W; w++) {
            if ((*font) & mask) {
                int px = x + w;
                int py = y + h;
                if (px >= 0 && px < W && py >= 0 && py < H) {
                    fb[py * W + px] = fg;
                }
            }
#if FONT_VMIRROR
            mask <<= 1;
#else
            mask >>= 1;
#endif
        }
        font++;
    }
}

void ui_fontPrintAt(int x, int y, uint32_t fg, const char *text)
{
    int cx = x;
    int cy = y;
    for (const char *s = text; s && *s; s++) {
        if (*s == '\n') {
            cy += UI_FONT_CHAR_H + 2;
            cx = x;
            continue;
        }
        ui_fontDrawChar(cx, cy, (unsigned char)*s, fg);
        cx += UI_FONT_CHAR_W + 1;
    }
}
