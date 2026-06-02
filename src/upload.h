#ifndef UPLOAD_H
#define UPLOAD_H

#include <stddef.h>
#include <windows.h>

/* Largest .dukex archive the console will attempt to upload. The whole file is
 * held in RAM once (no base64 inflation any more), so this is bounded by the
 * console's ~64 MB of memory minus what the TLS stack and scan structures use. */
#define UPLOAD_MAX_RAW_BYTES (32u * 1024u * 1024u)

/* POSTs the EEPROM dump (base64), decrypted HDD key (hex) and serial number to
 * /api/me/xbox-saves/console-data. Returns TRUE on a 2xx response. */
BOOL uploadConsoleData(const char *host, const char *port, const char *sessionKey,
                       const char *serial, const char *hddKeyHex,
                       const unsigned char *eeprom, size_t eepromLen,
                       char *consoleIdOut, size_t consoleIdOutSz);

/* Reads dukexPath and POSTs it as a raw binary body to /api/me/xbox-saves/game,
 * with metadata in the query string/headers (including the fingerprint used for
 * incremental uploads). Returns TRUE on a 2xx response. */
BOOL uploadGameDukex(const char *host, const char *port, const char *sessionKey,
                     const char *consoleId, const char *hddKeyHex,
                     const char *titleId, const char *titleName,
                     int saveCount, unsigned long long totalBytes, const char *fingerprint,
                     unsigned long long saveModifiedUnix, const char *manifestJson,
                     const char *dukexPath);

/* GETs /api/me/xbox-saves/manifest into out (a text body of "TITLEID=FINGERPRINT"
 * lines). Returns TRUE on a 2xx response. */
BOOL fetchSavesManifest(const char *host, const char *port, const char *sessionKey, char *out,
                        size_t outsz);

/* Returns TRUE if manifest contains "titleId=fingerprint" (i.e. the server
 * already has this exact version and the upload can be skipped). */
BOOL manifestTitleMatches(const char *manifest, const char *consoleId, const char *titleId,
                          const char *fingerprint);

/* Downloads a title's .dukex archive from /api/me/xbox-saves/download/<titleId>
 * to destPath. Returns TRUE on a 2xx response (file written). */
BOOL downloadGameDukex(const char *host, const char *port, const char *sessionKey,
                       const char *sourceConsoleId, const char *targetConsoleId,
                       const char *titleId, const char *destPath);

#endif
