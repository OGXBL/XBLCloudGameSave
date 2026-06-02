#ifndef APP_UI_H
#define APP_UI_H

#include <stdint.h>
#include <stdbool.h>

/* xbmc4gamers-inspired palette */
#define UI_COL_BG        0x00081018u
#define UI_COL_PANEL     0x00142030u
#define UI_COL_PANEL2    0x001C2C42u
#define UI_COL_BORDER    0x00304058u
#define UI_COL_ORANGE    0x00FF8C00u
#define UI_COL_ORANGE2   0x00E07010u
#define UI_COL_GREEN     0x0048D070u
#define UI_COL_TEXT      0x00E8ECF4u
#define UI_COL_DIM       0x0090A0B8u
#define UI_COL_WHITE     0x00FFFFFFu
#define UI_COL_BLACK     0x00000000u
#define UI_COL_BAR_BG    0x00203048u

void ui_syncFb(void);
void ui_fill(uint32_t color);
void ui_drawRect(int x, int y, int w, int h, uint32_t color);
void ui_drawHLine(int x, int y, int w, uint32_t color);
void ui_drawLogoScaled(int x, int y, int size);

void ui_drawHeader(const char *title);
void ui_showQrScreen(const uint8_t qrcode[], int qrModules);
void ui_showLoggedIn(const char *username);
void ui_showComplete(const char *line1, const char *line2);

/* Progress / backup screen with log + upload bar */
void ui_beginProgress(void);
void ui_logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void ui_setProgress(float fraction, const char *label);
void ui_setUploadProgress(float fraction, const char *label);
/* Shows "N/M uploaded" beside the progress bar (total 0 hides it). */
void ui_setUploadStats(int uploaded, int total);

#endif
