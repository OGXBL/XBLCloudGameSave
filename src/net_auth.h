#ifndef NET_AUTH_H
#define NET_AUTH_H

#include <stddef.h>
#include <windows.h>

/*
 * Runs the Insignia OAuth2 device-authorization flow:
 *   1. POST /api/auth/device to obtain a user_code + verification URL.
 *   2. Renders the verification URL as a QR code on the framebuffer.
 *   3. Polls /api/auth/device/token until the user logs in on their phone/PC.
 *
 * On success returns TRUE and fills sessionKey and username. Networking must
 * already be initialised (see nxNetInit in main). Blocks until login completes
 * or the device code expires.
 */
BOOL deviceLogin(char *sessionKey, size_t skSz, char *username, size_t unSz);

/* Checks whether an existing session key is still valid by calling the Insignia
 * auth API (GET /api/auth/user with X-Session-Key). Returns TRUE and fills
 * username on success; FALSE if the key is invalid/expired or the network call
 * fails. Lets a saved login be reused without showing the QR code again. */
BOOL verifySession(const char *sessionKey, char *username, size_t unSz);

#endif
