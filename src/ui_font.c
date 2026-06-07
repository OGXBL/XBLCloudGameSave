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

void ui_fontDrawCharScaled(int x, int y, unsigned char c, uint32_t fg, int scale)
{
    if (scale < 1) {
        scale = 1;
    }
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
                int bx = x + w * scale;
                int by = y + h * scale;
                for (int sy = 0; sy < scale; sy++) {
                    int py = by + sy;
                    if (py < 0 || py >= H) {
                        continue;
                    }
                    for (int sx = 0; sx < scale; sx++) {
                        int px = bx + sx;
                        if (px >= 0 && px < W) {
                            fb[py * W + px] = fg;
                        }
                    }
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

void ui_fontDrawChar(int x, int y, unsigned char c, uint32_t fg)
{
    ui_fontDrawCharScaled(x, y, c, fg, 1);
}

int ui_fontTextWidth(const char *text, int scale)
{
    if (scale < 1) {
        scale = 1;
    }
    int n = 0;
    for (const char *s = text; s && *s; s++) {
        if (*s == '\n') {
            continue;
        }
        n++;
    }
    if (n == 0) {
        return 0;
    }
    return n * (UI_FONT_CHAR_W * scale + UI_FONT_TRACKING) - UI_FONT_TRACKING;
}

void ui_fontPrintScaled(int x, int y, uint32_t fg, const char *text, int scale)
{
    if (scale < 1) {
        scale = 1;
    }
    int cx = x;
    int cy = y;
    int advance = UI_FONT_CHAR_W * scale + UI_FONT_TRACKING;
    int lineH = UI_FONT_CHAR_H * scale + 2;
    for (const char *s = text; s && *s; s++) {
        if (*s == '\n') {
            cy += lineH;
            cx = x;
            continue;
        }
        ui_fontDrawCharScaled(cx, cy, (unsigned char)*s, fg, scale);
        cx += advance;
    }
}

void ui_fontPrintScaledShadow(int x, int y, uint32_t fg, uint32_t shadow, const char *text,
                              int scale)
{
    if (scale < 1) {
        scale = 1;
    }
    int off = scale; /* shadow offset scales with the glyph */
    ui_fontPrintScaled(x + off, y + off, shadow, text, scale);
    ui_fontPrintScaled(x, y, fg, text, scale);
}

void ui_fontPrintAt(int x, int y, uint32_t fg, const char *text)
{
    ui_fontPrintScaled(x, y, fg, text, 1);
}

void ui_fontPrintShadow(int x, int y, uint32_t fg, uint32_t shadow, const char *text)
{
    ui_fontPrintScaledShadow(x, y, fg, shadow, text, 1);
}
