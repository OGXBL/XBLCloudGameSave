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
        ui_drawRect(UI_BAR_X, UI_BAR_Y, fill, UI_BAR_H, UI_COL_ORANGE);
    }
    ui_drawHLine(UI_BAR_X, UI_BAR_Y, UI_BAR_W, UI_COL_BORDER);
    ui_drawHLine(UI_BAR_X, UI_BAR_Y + UI_BAR_H - 1, UI_BAR_W, UI_COL_BORDER);
}

static void ui_drawProgressChrome(void)
{
    ui_fill(UI_COL_BG);
    ui_drawRect(8, 8, 624, 52, UI_COL_PANEL);
    ui_drawHLine(8, 60, 624, UI_COL_ORANGE);
    ui_drawLogoScaled(16, 12, 44);
    ui_fontPrintAt(72, 22, UI_COL_TEXT, "OG XBL Save Backup");
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
    ui_fill(UI_COL_BG);
    ui_drawRect(0, 0, 640, 48, UI_COL_PANEL);
    ui_drawHLine(0, 48, 640, UI_COL_ORANGE);

    ui_drawLogoScaled((640 - 100) / 2, 58, 100);
    ui_drawQrAt(qrcode, qrModules, 320, 272, 150);

    ui_fontPrintAt(155, 400, UI_COL_TEXT, "Scan with your phone");
    ui_fontPrintAt(130, 420, UI_COL_DIM, "Sign in to Insignia Live");
    ui_syncFb();
}

void ui_showLoggedIn(const char *username)
{
    ui_fill(UI_COL_BG);
    ui_drawRect(0, 0, 640, 48, UI_COL_PANEL);
    ui_drawHLine(0, 48, 640, UI_COL_GREEN);

    ui_drawLogoScaled((640 - 130) / 2, 70, 130);
    ui_drawFrame(120, 220, 400, 120);

    ui_fontPrintAt(200, 248, UI_COL_GREEN, "Signed in");
    ui_fontPrintAt(160, 276, UI_COL_DIM, "Account:");
    ui_fontPrintAt(160, 296, UI_COL_TEXT, username && username[0] ? username : "(unknown)");
    ui_fontPrintAt(155, 360, UI_COL_DIM, "Starting backup and sync...");
    ui_syncFb();
}

void ui_showComplete(const char *line1, const char *line2)
{
    ui_fill(UI_COL_BG);
    ui_drawRect(0, 0, 640, 48, UI_COL_PANEL);
    ui_drawHLine(0, 48, 640, UI_COL_GREEN);

    ui_drawLogoScaled((640 - 110) / 2, 64, 110);
    ui_drawFrame(100, 200, 440, 140);

    ui_fontPrintAt(220, 230, UI_COL_GREEN, "Backup complete");
    if (line1 && line1[0]) {
        ui_fontPrintAt(120, 270, UI_COL_TEXT, line1);
    }
    if (line2 && line2[0]) {
        ui_fontPrintAt(120, 292, UI_COL_DIM, line2);
    }
    ui_fontPrintAt(130, 380, UI_COL_DIM, "START - return to dashboard");
    ui_syncFb();
}
