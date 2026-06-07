#include "app_ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <hal/video.h>

#include "ogxbl_logo.h"
#include "ui_font.h"
#include "../third_party/qrcodegen.h"

#define UI_LOG_X     36
#define UI_LOG_Y     78
#define UI_LOG_W     568
#define UI_LOG_LINES 16
#define UI_LOG_LINEH 18

#define UI_BAR_X     36
#define UI_BAR_Y     430
#define UI_BAR_W     568
#define UI_BAR_H     18

static char s_logLines[UI_LOG_LINES][96];
static int s_logCount;
static float s_barFrac;
static char s_barLabel[80];
static int s_uploadDone;
static int s_uploadTotal;
static char s_uploadStat[32];

static void ui_putPixel(volatile uint32_t *fb, int W, int H, int x, int y, uint32_t c)
{
    if (x >= 0 && x < W && y >= 0 && y < H) {
        fb[(unsigned)y * (unsigned)W + (unsigned)x] = c;
    }
}

/* Linear blend of two 0x00RRGGBB colors. t is 0..255 (0 = a, 255 = b). */
static uint32_t ui_mix(uint32_t a, uint32_t b, int t)
{
    if (t < 0) {
        t = 0;
    }
    if (t > 255) {
        t = 255;
    }
    int ar = (int)((a >> 16) & 0xFF), ag = (int)((a >> 8) & 0xFF), ab = (int)(a & 0xFF);
    int br = (int)((b >> 16) & 0xFF), bg = (int)((b >> 8) & 0xFF), bb = (int)(b & 0xFF);
    int rr = ar + (br - ar) * t / 255;
    int rg = ag + (bg - ag) * t / 255;
    int rb = ab + (bb - ab) * t / 255;
    return ((uint32_t)rr << 16) | ((uint32_t)rg << 8) | (uint32_t)rb;
}

void ui_syncFb(void)
{
    XVideoFlushFB();
}

void ui_fill(uint32_t color)
{
    VIDEO_MODE vm = XVideoGetMode();
    volatile uint32_t *fb = (volatile uint32_t *)XVideoGetFB();
    unsigned W = (unsigned)vm.width;
    unsigned H = (unsigned)vm.height;
    for (unsigned y = 0; y < H; y++) {
        for (unsigned x = 0; x < W; x++) {
            fb[y * W + x] = color;
        }
    }
}

void ui_fillGradient(uint32_t top, uint32_t bottom)
{
    VIDEO_MODE vm = XVideoGetMode();
    volatile uint32_t *fb = (volatile uint32_t *)XVideoGetFB();
    unsigned W = (unsigned)vm.width;
    unsigned H = (unsigned)vm.height;
    for (unsigned y = 0; y < H; y++) {
        uint32_t row = ui_mix(top, bottom, H > 1 ? (int)(y * 255u / (H - 1)) : 0);
        for (unsigned x = 0; x < W; x++) {
            fb[y * W + x] = row;
        }
    }
}

void ui_fillBackground(void)
{
    ui_fillGradient(UI_COL_BG_TOP, UI_COL_BG_BOT);
}

void ui_drawDisc(int cx, int cy, int radius, uint32_t color)
{
    if (radius < 1) {
        radius = 1;
    }
    VIDEO_MODE vm = XVideoGetMode();
    volatile uint32_t *fb = (volatile uint32_t *)XVideoGetFB();
    int W = vm.width;
    int H = vm.height;
    int r2 = radius * radius;
    /* +1px soft edge: pixels just outside the radius blend toward the existing
     * framebuffer so the dots don't look jagged. */
    int ro2 = (radius + 1) * (radius + 1);
    for (int dy = -radius - 1; dy <= radius + 1; dy++) {
        int py = cy + dy;
        if (py < 0 || py >= H) {
            continue;
        }
        for (int dx = -radius - 1; dx <= radius + 1; dx++) {
            int px = cx + dx;
            if (px < 0 || px >= W) {
                continue;
            }
            int d2 = dx * dx + dy * dy;
            if (d2 <= r2) {
                fb[py * W + px] = color;
            } else if (d2 <= ro2) {
                fb[py * W + px] = ui_mix(fb[py * W + px], color, 110);
            }
        }
    }
}

void ui_drawRect(int x, int y, int w, int h, uint32_t color)
{
    VIDEO_MODE vm = XVideoGetMode();
    volatile uint32_t *fb = (volatile uint32_t *)XVideoGetFB();
    int W = vm.width;
    int H = vm.height;
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            ui_putPixel(fb, W, H, x + i, y + j, color);
        }
    }
}

void ui_drawHLine(int x, int y, int w, uint32_t color)
{
    ui_drawRect(x, y, w, 1, color);
}

static void ui_drawFrame(int x, int y, int w, int h)
{
    ui_drawRect(x, y, w, h, UI_COL_PANEL);
    ui_drawHLine(x, y, w, UI_COL_BORDER);
    ui_drawHLine(x, y + h - 1, w, UI_COL_BORDER);
    ui_drawRect(x, y, 1, h, UI_COL_BORDER);
    ui_drawRect(x + w - 1, y, 1, h, UI_COL_BORDER);
    ui_drawHLine(x, y, w, UI_COL_ORANGE2);
}

void ui_drawLogoScaled(int x, int y, int size)
{
    VIDEO_MODE vm = XVideoGetMode();
    volatile uint32_t *fb = (volatile uint32_t *)XVideoGetFB();
    int W = vm.width;
    int H = vm.height;

    for (int dy = 0; dy < size; dy++) {
        int sy = dy * OGXBL_LOGO_IMG_H / size;
        for (int dx = 0; dx < size; dx++) {
            int sx = dx * OGXBL_LOGO_W / size;
            uint32_t src = ogxbl_logo_rgba[sy * OGXBL_LOGO_W + sx];
            uint8_t a = (uint8_t)(src >> 24);
            if (a < 8) {
                continue;
            }
            uint8_t sr = (uint8_t)((src >> 16) & 0xFF);
            uint8_t sg = (uint8_t)((src >> 8) & 0xFF);
            uint8_t sb = (uint8_t)(src & 0xFF);
            int px = x + dx;
            int py = y + dy;
            if (a >= 250) {
                ui_putPixel(fb, W, H, px, py, ((uint32_t)sr << 16) | ((uint32_t)sg << 8) | sb);
            } else {
                uint32_t dst = fb[py * W + px];
                uint8_t dr = (uint8_t)((dst >> 16) & 0xFF);
                uint8_t dg = (uint8_t)((dst >> 8) & 0xFF);
                uint8_t db = (uint8_t)(dst & 0xFF);
                uint8_t nr = (uint8_t)((sr * a + dr * (255 - a)) / 255);
                uint8_t ng = (uint8_t)((sg * a + dg * (255 - a)) / 255);
                uint8_t nb = (uint8_t)((sb * a + db * (255 - a)) / 255);
                ui_putPixel(fb, W, H, px, py, ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | nb);
            }
        }
    }
}

/* 12-point unit circle (cos, sin) at 30-degree steps, starting from the top and
 * going clockwise. Precomputed so we don't pull in libm just for the spinner. */
#define UI_SPIN_SEGS 12
static const float ui_spinCos[UI_SPIN_SEGS] = {
    0.000f,  0.500f,  0.866f,  1.000f,  0.866f,  0.500f,
    0.000f, -0.500f, -0.866f, -1.000f, -0.866f, -0.500f
};
static const float ui_spinSin[UI_SPIN_SEGS] = {
    -1.000f, -0.866f, -0.500f, 0.000f, 0.500f, 0.866f,
    1.000f,  0.866f,  0.500f,  0.000f, -0.500f, -0.866f
};

void ui_drawSpinner(int cx, int cy, int radius, int frame)
{
    int head = ((frame % UI_SPIN_SEGS) + UI_SPIN_SEGS) % UI_SPIN_SEGS;
    int dot = radius / 5;
    if (dot < 3) {
        dot = 3;
    }
    for (int i = 0; i < UI_SPIN_SEGS; i++) {
        int px = cx + (int)(ui_spinCos[i] * (float)radius);
        int py = cy + (int)(ui_spinSin[i] * (float)radius);

        /* Distance behind the rotating head; the head is brightest and the
         * trail fades back toward the dim track colour. */
        int d = (head - i + UI_SPIN_SEGS) % UI_SPIN_SEGS;
        int t = 255 - (d * 255 / (UI_SPIN_SEGS - 1));
        uint32_t col = ui_mix(UI_COL_BAR_BG, UI_COL_ORANGE, t);
        /* Leading dot a touch larger for a comet-like head. */
        ui_drawDisc(px, py, d == 0 ? dot + 1 : dot, col);
    }
}

void ui_showBootScreen(const char *status)
{
    static int s_bootFrame = 0;
    VIDEO_MODE vm = XVideoGetMode();
    int W = vm.width;
    int cx = W / 2;

    ui_fillBackground();

    /* Logo sitting above the spinner. */
    int logoSize = 132;
    ui_drawLogoScaled(cx - logoSize / 2, 96, logoSize);

    ui_drawSpinner(cx, 300, 30, s_bootFrame);

    const char *line = (status && status[0]) ? status : "Loading...";
    int tw = ui_fontTextWidth(line, 1);
    ui_fontPrintShadow(cx - tw / 2, 372, UI_COL_DIM, UI_COL_SHADOW, line);

    const char *brand = "xb.live cloud sync";
    int bw = ui_fontTextWidth(brand, 1);
    ui_fontPrintAt(cx - bw / 2, 400, UI_COL_BORDER, brand);

    s_bootFrame++;
    ui_syncFb();
}

static void ui_redrawLogArea(void)
{
    ui_drawRect(UI_LOG_X - 4, UI_LOG_Y - 8, UI_LOG_W + 8, UI_LOG_LINES * UI_LOG_LINEH + 12, UI_COL_PANEL2);
    for (int i = 0; i < s_logCount; i++) {
        ui_fontPrintAt(UI_LOG_X, UI_LOG_Y + i * UI_LOG_LINEH, UI_COL_TEXT, s_logLines[i]);
    }
}

static void ui_redrawProgressBar(void)
{
    int fill = (int)(s_barFrac * (float)UI_BAR_W);
    if (fill < 0) {
        fill = 0;
    }
    if (fill > UI_BAR_W) {
        fill = UI_BAR_W;
    }

    /* Clear the label strip first; text is drawn transparently, so without this
     * a previous longer label would show through behind a shorter new one. */
    ui_drawRect(UI_BAR_X, UI_BAR_Y - 22, UI_BAR_W, 18, UI_COL_PANEL);
    ui_fontPrintAt(UI_BAR_X, UI_BAR_Y - 20, UI_COL_DIM, s_barLabel[0] ? s_barLabel : "Ready");
    if (s_uploadTotal > 0) {
        ui_fontPrintAt(UI_BAR_X + UI_BAR_W - 152, UI_BAR_Y - 20, UI_COL_TEXT, s_uploadStat);
    }

    ui_drawRect(UI_BAR_X, UI_BAR_Y, UI_BAR_W, UI_BAR_H, UI_COL_BAR_BG);
    if (fill > 0) {
        /* Glossy vertical gradient: lighter amber at the top fading to a deeper
         * orange at the bottom. */
        for (int j = 0; j < UI_BAR_H; j++) {
            uint32_t row = ui_mix(UI_COL_ORANGE, UI_COL_ORANGE2, j * 255 / (UI_BAR_H - 1));
            ui_drawRect(UI_BAR_X, UI_BAR_Y + j, fill, 1, row);
        }
    }
    ui_drawHLine(UI_BAR_X, UI_BAR_Y, UI_BAR_W, UI_COL_BORDER);
    ui_drawHLine(UI_BAR_X, UI_BAR_Y + UI_BAR_H - 1, UI_BAR_W, UI_COL_BORDER);
}

static void ui_drawProgressChrome(void)
{
    ui_fillBackground();
    ui_drawRect(8, 8, 624, 52, UI_COL_PANEL);
    ui_drawHLine(8, 8, 624, UI_COL_BORDER);
    /* Accent underline that fades out toward the right for a modern feel. */
    for (int i = 0; i < 624; i++) {
        ui_drawRect(8 + i, 60, 1, 2, ui_mix(UI_COL_ORANGE, UI_COL_PANEL, i * 255 / 624));
    }
    ui_drawLogoScaled(16, 12, 44);
    ui_fontPrintShadow(72, 20, UI_COL_TEXT, UI_COL_SHADOW, "OG XBL Save Backup");
    ui_fontPrintAt(72, 40, UI_COL_DIM, "xb.live cloud sync");
    ui_drawFrame(20, 68, 600, 388);
    s_logCount = 0;
    s_barFrac = 0.0f;
    s_barLabel[0] = '\0';
    s_uploadDone = 0;
    s_uploadTotal = 0;
    s_uploadStat[0] = '\0';
}

void ui_drawHeader(const char *title)
{
    /* The chrome already draws the app name and "xb.live cloud sync" subtitle.
     * Drawing the title again on the subtitle line (y=40) overlapped that text,
     * so we just render the standard chrome here. */
    (void)title;
    ui_drawProgressChrome();
}

void ui_beginProgress(void)
{
    ui_drawProgressChrome();
    ui_redrawProgressBar();
    ui_syncFb();
}

void ui_logf(const char *fmt, ...)
{
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (s_logCount >= UI_LOG_LINES) {
        for (int i = 0; i < UI_LOG_LINES - 1; i++) {
            strncpy(s_logLines[i], s_logLines[i + 1], sizeof(s_logLines[i]) - 1);
            s_logLines[i][sizeof(s_logLines[i]) - 1] = '\0';
        }
        s_logCount = UI_LOG_LINES - 1;
    }
    strncpy(s_logLines[s_logCount], buf, sizeof(s_logLines[0]) - 1);
    s_logLines[s_logCount][sizeof(s_logLines[0]) - 1] = '\0';
    s_logCount++;

    ui_redrawLogArea();
    ui_redrawProgressBar();
    ui_syncFb();
}

void ui_setProgress(float fraction, const char *label)
{
    if (fraction < 0.0f) {
        fraction = 0.0f;
    }
    if (fraction > 1.0f) {
        fraction = 1.0f;
    }
    s_barFrac = fraction;
    if (label) {
        strncpy(s_barLabel, label, sizeof(s_barLabel) - 1);
        s_barLabel[sizeof(s_barLabel) - 1] = '\0';
    }
    ui_redrawProgressBar();
    ui_syncFb();
}

void ui_setUploadProgress(float fraction, const char *label)
{
    ui_setProgress(fraction, label);
}

void ui_setUploadStats(int uploaded, int total)
{
    s_uploadDone = uploaded < 0 ? 0 : uploaded;
    s_uploadTotal = total < 0 ? 0 : total;
    if (s_uploadTotal > 0) {
        snprintf(s_uploadStat, sizeof(s_uploadStat), "%d/%d uploaded", s_uploadDone, s_uploadTotal);
    } else {
        s_uploadStat[0] = '\0';
    }
    ui_redrawProgressBar();
    ui_syncFb();
}

static void ui_drawQrAt(const uint8_t qrcode[], int size, int ox, int oy, int maxSide)
{
    VIDEO_MODE vm = XVideoGetMode();
    volatile uint32_t *fb = (volatile uint32_t *)XVideoGetFB();
    int W = vm.width;
    int H = vm.height;

    int modules = size + 2;
    int cell = maxSide / modules;
    if (cell < 4) {
        cell = 4;
    }
    if (cell > 10) {
        cell = 10;
    }

    int qr_px = modules * cell;
    int x0 = ox - (qr_px / 2);
    int y0 = oy - (qr_px / 2);

    ui_drawRect(x0 - 12, y0 - 12, qr_px + 24, qr_px + 24, UI_COL_PANEL2);
    ui_drawRect(x0 - 14, y0 - 14, qr_px + 28, qr_px + 28, UI_COL_BORDER);

    for (int qy = -1; qy <= size; qy++) {
        for (int qx = -1; qx <= size; qx++) {
            bool on = qrcodegen_getModule(qrcode, qx, qy);
            uint32_t pixel = on ? UI_COL_TEXT : UI_COL_PANEL;
            int px0 = x0 + (qx + 1) * cell;
            int py0 = y0 + (qy + 1) * cell;
            for (int dy = 0; dy < cell; dy++) {
                for (int dx = 0; dx < cell; dx++) {
                    ui_putPixel(fb, W, H, px0 + dx, py0 + dy, pixel);
                }
            }
        }
    }
}

void ui_showQrScreen(const uint8_t qrcode[], int qrModules)
{
    ui_fillBackground();
    ui_drawRect(0, 0, 640, 48, UI_COL_PANEL);
    ui_drawHLine(0, 48, 640, UI_COL_ORANGE);

    ui_drawLogoScaled((640 - 100) / 2, 58, 100);
    ui_drawQrAt(qrcode, qrModules, 320, 272, 150);

    const char *t1 = "Scan with your phone";
    const char *t2 = "Sign in to Insignia Live";
    ui_fontPrintScaledShadow(320 - ui_fontTextWidth(t1, 1) / 2, 398, UI_COL_TEXT, UI_COL_SHADOW, t1, 1);
    ui_fontPrintAt(320 - ui_fontTextWidth(t2, 1) / 2, 420, UI_COL_DIM, t2);
    ui_syncFb();
}

void ui_showLoggedIn(const char *username)
{
    ui_fillBackground();
    ui_drawRect(0, 0, 640, 48, UI_COL_PANEL);
    ui_drawHLine(0, 48, 640, UI_COL_GREEN);

    ui_drawLogoScaled((640 - 130) / 2, 70, 130);
    ui_drawFrame(120, 218, 400, 124);

    const char *signed_in = "Signed in";
    ui_fontPrintScaledShadow(320 - ui_fontTextWidth(signed_in, 2) / 2, 240, UI_COL_GREEN,
                             UI_COL_SHADOW, signed_in, 2);
    ui_fontPrintAt(160, 286, UI_COL_DIM, "Account:");
    ui_fontPrintShadow(160, 304, UI_COL_TEXT, UI_COL_SHADOW,
                       username && username[0] ? username : "(unknown)");
    const char *starting = "Starting backup and sync...";
    ui_fontPrintAt(320 - ui_fontTextWidth(starting, 1) / 2, 360, UI_COL_DIM, starting);
    ui_syncFb();
}

void ui_showComplete(const char *line1, const char *line2)
{
    ui_fillBackground();
    ui_drawRect(0, 0, 640, 48, UI_COL_PANEL);
    ui_drawHLine(0, 48, 640, UI_COL_GREEN);

    ui_drawLogoScaled((640 - 110) / 2, 60, 110);
    ui_drawFrame(100, 198, 440, 146);

    const char *done = "Backup complete";
    ui_fontPrintScaledShadow(320 - ui_fontTextWidth(done, 2) / 2, 220, UI_COL_GREEN, UI_COL_SHADOW,
                             done, 2);
    if (line1 && line1[0]) {
        ui_fontPrintAt(120, 272, UI_COL_TEXT, line1);
    }
    if (line2 && line2[0]) {
        ui_fontPrintAt(120, 294, UI_COL_DIM, line2);
    }
    const char *hint = "START - return to dashboard";
    ui_fontPrintAt(320 - ui_fontTextWidth(hint, 1) / 2, 380, UI_COL_DIM, hint);
    ui_syncFb();
}
