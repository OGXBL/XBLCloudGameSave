#ifndef APP_INPUT_H
#define APP_INPUT_H

#include <windows.h>

#define XINPUT_GAMEPAD_A     0x1000
#define XINPUT_GAMEPAD_B     0x2000
#define XINPUT_GAMEPAD_START 0x0010
#define XINPUT_GAMEPAD_BACK  0x0020

/* Initialise USB host + XID so we can read controller 1. Safe to call once. */
void inputInit(void);

/* Poll USB; returns TRUE if A, B, START, or BACK was pressed on any gamepad. */
BOOL inputAnyExitPressed(void);

/* Shows the complete screen and waits for the user to press a button, then
 * returns to the Xbox dashboard. */
void inputWaitExitToDashboard(const char *summary1, const char *summary2);

#endif
