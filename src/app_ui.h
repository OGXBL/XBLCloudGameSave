#ifndef APP_UI_H
#define APP_UI_H

#include <stdint.h>
#include <stdbool.h>

/* Modern xbmc4gamers-inspired dark palette. Background is a soft vertical
 * gradient (BG_TOP -> BG_BOT); panels sit slightly lifted above it. */
#define UI_COL_BG        0x00070D16u
#define UI_COL_BG_TOP    0x000B1322u
#define UI_COL_BG_BOT    0x00040810u
#define UI_COL_PANEL     0x00121E2Eu
#define UI_COL_PANEL2    0x00182943u
#define UI_COL_BORDER    0x002B3C55u
#define UI_COL_ORANGE    0x00FF9A1Fu
#define UI_COL_ORANGE2   0x00C96E10u
#define UI_COL_GREEN     0x004FD17Au
#define UI_COL_TEXT      0x00EEF2F8u
#define UI_COL_DIM       0x008A9BB4u
#define UI_COL_WHITE     0x00FFFFFFu
#define UI_COL_BLACK     0x00000000u
#define UI_COL_SHADOW    0x00040810u
#define UI_COL_BAR_BG    0x001A283Cu

void ui_syncFb(void);
void ui_fill(uint32_t color);
void ui_fillGradient(uint32_t top, uint32_t bottom);
/* Full-screen background using the standard app gradient. */
void ui_fillBackground(void);
void ui_drawRect(int x, int y, int w, int h, uint32_t color);
void ui_drawHLine(int x, int y, int w, uint32_t color);
/* Filled (optionally soft-edged) disc, used by the loading spinner. */
void ui_drawDisc(int cx, int cy, int radius, uint32_t color);
void ui_drawLogoScaled(int x, int y, int size);

/* Animated circular loading spinner (XBMC4Gamers style). frame increments by 1
 * per redraw to rotate the highlighted segment around the ring. */
void ui_drawSpinner(int cx, int cy, int radius, int frame);
/* Centered logo + spinner boot screen; each call advances the spinner one frame
 * so calling it repeatedly in a wait loop animates it. */
void ui_showBootScreen(const char *status);

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
