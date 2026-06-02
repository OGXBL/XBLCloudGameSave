#ifndef SESSION_STORE_H
#define SESSION_STORE_H

#include <stddef.h>
#include <windows.h>

/* Loads a previously saved Insignia session from path. Returns TRUE and fills
 * sessionKey (and username, if non-NULL) when a session file exists and parses. */
BOOL loadSession(const char *path, char *sessionKey, size_t skSz, char *username, size_t unSz);

/* Optional third line in the session file: Xbox console_id from last upload. */
BOOL loadSessionConsoleId(const char *path, char *consoleId, size_t cidSz);
BOOL saveSessionConsoleId(const char *path, const char *consoleId);

/* Persists the session key and username to path (overwriting any existing file)
 * so the next run does not have to show the QR code again. */
BOOL saveSession(const char *path, const char *sessionKey, const char *username);

/* Removes a stored session (e.g. after it fails validation). */
void clearSession(const char *path);

#endif
