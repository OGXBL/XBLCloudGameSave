#include "upload.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hal/debug.h>

#include "../third_party/https_client.h"
#include "app_ui.h"
#include "base64.h"

typedef struct {
    char label[64];
    size_t total;
} UploadProgressCtx;

static void upload_progress_cb(size_t sent, size_t total, void *ctx)
{
    UploadProgressCtx *u = (UploadProgressCtx *)ctx;
    if (!u || total == 0) {
        return;
    }
    float f = (float)sent / (float)total;
    ui_setUploadProgress(f, u->label);
}

#define UPLOAD_RESP_SIZE 4096

/* Returns TRUE if the HTTP response status line is 2xx. */
static BOOL responseIsOk(const char *resp)
{
    /* Expect "HTTP/1.1 2xx ...". */
    const char *sp = strchr(resp, ' ');
    if (!sp) {
        return FALSE;
    }
    return sp[1] == '2';
}

/* Appends src to dst (a malloc'd buffer of capacity *cap starting at length *len),
 * growing as needed, with JSON-string escaping. Returns FALSE on allocation
 * failure. If escape is FALSE, src is copied verbatim (used for pre-built JSON
 * fragments and base64, which need no escaping). */
static BOOL appendStr(char **dst, size_t *len, size_t *cap, const char *src, BOOL escape)
{
    for (const char *p = src; *p; p++) {
        /* Worst case a single char expands to 6 (\u00XX); ensure headroom. */
        if (*len + 8 >= *cap) {
            size_t newCap = (*cap) * 2 + 256;
            char *grown = (char *)realloc(*dst, newCap);
            if (!grown) {
                return FALSE;
            }
            *dst = grown;
            *cap = newCap;
        }
        unsigned char c = (unsigned char)*p;
        if (!escape) {
            (*dst)[(*len)++] = (char)c;
            continue;
        }
        switch (c) {
            case '"':
                (*dst)[(*len)++] = '\\';
                (*dst)[(*len)++] = '"';
                break;
            case '\\':
                (*dst)[(*len)++] = '\\';
                (*dst)[(*len)++] = '\\';
                break;
            case '\n':
                (*dst)[(*len)++] = '\\';
                (*dst)[(*len)++] = 'n';
                break;
            case '\r':
                (*dst)[(*len)++] = '\\';
                (*dst)[(*len)++] = 'r';
                break;
            case '\t':
                (*dst)[(*len)++] = '\\';
                (*dst)[(*len)++] = 't';
                break;
            default:
                if (c < 0x20) {
                    *len += snprintf(*dst + *len, *cap - *len, "\\u%04x", c);
                } else {
                    (*dst)[(*len)++] = (char)c;
                }
                break;
        }
    }
    (*dst)[*len] = '\0';
    return TRUE;
}

static int json_extract_string_simple(const char *json, const char *key, char *out, size_t outsz)
{
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\":\"", key);
    const char *p = strstr(json, needle);
    if (!p) {
        return -1;
    }
    p += strlen(needle);
    size_t i = 0;
    while (*p && *p != '"' && i < outsz - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i > 0 ? 0 : -1;
}

BOOL uploadConsoleData(const char *host, const char *port, const char *sessionKey,
                       const char *serial, const char *hddKeyHex,
                       const unsigned char *eeprom, size_t eepromLen,
                       char *consoleIdOut, size_t consoleIdOutSz)
{
    char *eepromB64 = base64Encode(eeprom, eepromLen);
    if (!eepromB64) {
        return FALSE;
    }

    size_t cap = 1024;
    size_t len = 0;
    char *body = (char *)malloc(cap);
    if (!body) {
        free(eepromB64);
        return FALSE;
    }
    body[0] = '\0';

    BOOL ok = appendStr(&body, &len, &cap, "{\"sessionKey\":\"", FALSE) &&
              appendStr(&body, &len, &cap, sessionKey, TRUE) &&
              appendStr(&body, &len, &cap, "\",\"serial\":\"", FALSE) &&
              appendStr(&body, &len, &cap, serial ? serial : "", TRUE) &&
              appendStr(&body, &len, &cap, "\",\"hdd_key_hex\":\"", FALSE) &&
              appendStr(&body, &len, &cap, hddKeyHex ? hddKeyHex : "", TRUE) &&
              appendStr(&body, &len, &cap, "\",\"eeprom_base64\":\"", FALSE) &&
              appendStr(&body, &len, &cap, eepromB64, FALSE) &&
              appendStr(&body, &len, &cap, "\"}", FALSE);
    free(eepromB64);
    if (!ok) {
        free(body);
        return FALSE;
    }

    char resp[UPLOAD_RESP_SIZE];
    UploadProgressCtx consoleProg;
    consoleProg.total = len;
    snprintf(consoleProg.label, sizeof(consoleProg.label), "Uploading console data...");
    int r = https_request(host, port, "POST", "/api/me/xbox-saves/console-data", "application/json",
                          NULL, 0, body, len, resp, sizeof(resp), upload_progress_cb, &consoleProg);
    free(body);
    if (r != 0) {
        return FALSE;
    }
    if (!responseIsOk(resp)) {
        return FALSE;
    }
    if (consoleIdOut && consoleIdOutSz > 0) {
        consoleIdOut[0] = '\0';
        const char *body = strstr(resp, "\r\n\r\n");
        if (body) {
            json_extract_string_simple(body + 4, "console_id", consoleIdOut, consoleIdOutSz);
        }
    }
    return TRUE;
}

BOOL uploadGameDukex(const char *host, const char *port, const char *sessionKey,
                     const char *consoleId, const char *hddKeyHex,
                     const char *profile, const char *profileLabel,
                     const char *titleId, const char *titleName, int saveCount,
                     unsigned long long totalBytes, const char *fingerprint,
                     unsigned long long saveModifiedUnix, const char *manifestJson,
                     const char *dukexPath)
{
    HANDLE h = CreateFile(dukexPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    DWORD sizeHigh = 0;
    DWORD sizeLow = GetFileSize(h, &sizeHigh);
    const char *disp = titleName;
    if (!disp || !disp[0] || strcmp(disp, "<unknown title>") == 0) {
        disp = titleId ? titleId : "game";
    }
    if (sizeHigh != 0 || sizeLow == INVALID_FILE_SIZE || sizeLow > UPLOAD_MAX_RAW_BYTES) {
        ui_logf("  skip %s (too large: %lu MB)", disp, (unsigned long)(sizeLow / (1024u * 1024u)));
        CloseHandle(h);
        return FALSE;
    }

    unsigned char *raw = (unsigned char *)malloc(sizeLow ? sizeLow : 1);
    if (!raw) {
        CloseHandle(h);
        return FALSE;
    }
    DWORD readTotal = 0;
    while (readTotal < sizeLow) {
        DWORD got = 0;
        if (!ReadFile(h, raw + readTotal, sizeLow - readTotal, &got, NULL) || got == 0) {
            break;
        }
        readTotal += got;
    }
    CloseHandle(h);
    if (readTotal != sizeLow) {
        free(raw);
        return FALSE;
    }

    /* Metadata travels in the query string + headers; the body is the raw file. */
    /* Always send the HDD key so the server can derive a stable console_id even
     * when the session has no console_id yet (e.g. first run / console-data retry).
     * This keeps every backup from the same Xbox under one console_id. */
    char path[820];
    snprintf(path, sizeof(path),
             "/api/me/xbox-saves/game?title_id=%s&console_id=%s&hdd_key_hex=%s&profile=%s&save_count=%d&total_bytes=%llu&fingerprint=%s&save_modified=%llu",
             titleId, consoleId && consoleId[0] ? consoleId : "unknown",
             hddKeyHex && hddKeyHex[0] ? hddKeyHex : "", profile ? profile : "", saveCount,
             totalBytes, fingerprint ? fingerprint : "", saveModifiedUnix);

    char skHeader[256];
    snprintf(skHeader, sizeof(skHeader), "X-Session-Key: %s", sessionKey);

    char *nameB64 = base64Encode((const unsigned char *)(titleName ? titleName : ""),
                                 titleName ? strlen(titleName) : 0);
    char nameHeader[512];
    snprintf(nameHeader, sizeof(nameHeader), "X-Title-Name-B64: %s", nameB64 ? nameB64 : "");
    free(nameB64);

    /* Human-readable profile name for the website (base64 so spaces survive headers). */
    char profileHeader[256];
    profileHeader[0] = '\0';
    if (profileLabel && profileLabel[0]) {
        char *plB64 = base64Encode((const unsigned char *)profileLabel, strlen(profileLabel));
        if (plB64) {
            snprintf(profileHeader, sizeof(profileHeader), "X-Profile-Label-B64: %s", plB64);
            free(plB64);
        }
    }

    char *manifestB64 = NULL;
    char manifestHeader[7168];
    manifestHeader[0] = '\0';
    if (manifestJson && manifestJson[0]) {
        manifestB64 = base64Encode((const unsigned char *)manifestJson, strlen(manifestJson));
        if (manifestB64) {
            snprintf(manifestHeader, sizeof(manifestHeader), "X-Manifest-B64: %s", manifestB64);
        }
    }

    const char *headers[5];
    int nHeaders = 0;
    headers[nHeaders++] = skHeader;
    headers[nHeaders++] = nameHeader;
    if (profileHeader[0]) {
        headers[nHeaders++] = profileHeader;
    }
    if (manifestHeader[0]) {
        headers[nHeaders++] = manifestHeader;
    }

    UploadProgressCtx prog;
    prog.total = sizeLow;
    snprintf(prog.label, sizeof(prog.label), "Uploading %s", disp);

    char resp[UPLOAD_RESP_SIZE];
    int r = https_request(host, port, "POST", path, "application/octet-stream", headers, nHeaders,
                          (const char *)raw, sizeLow, resp, sizeof(resp), upload_progress_cb, &prog);
    free(manifestB64);
    free(raw);
    if (r != 0) {
        return FALSE;
    }
    return responseIsOk(resp);
}

BOOL fetchSavesManifest(const char *host, const char *port, const char *sessionKey, char *out,
                        size_t outsz)
{
    char skHeader[256];
    snprintf(skHeader, sizeof(skHeader), "X-Session-Key: %s", sessionKey);
    const char *headers[] = { skHeader };

    if (outsz == 0) {
        return FALSE;
    }
    out[0] = '\0';

    static char resp[16384];
    int r = https_request(host, port, "GET", "/api/me/xbox-saves/manifest", "application/json",
                          headers, 1, NULL, 0, resp, sizeof(resp), NULL, NULL);
    if (r != 0 || !responseIsOk(resp)) {
        return FALSE;
    }
    const char *p = strstr(resp, "\r\n\r\n");
    const char *bodyStart = p ? p + 4 : resp;
    strncpy(out, bodyStart, outsz - 1);
    out[outsz - 1] = '\0';
    return TRUE;
}

BOOL downloadGameDukex(const char *host, const char *port, const char *sessionKey,
                       const char *sourceConsoleId, const char *targetConsoleId,
                       const char *sourceProfile, const char *titleId, const char *destPath)
{
    char path[512];
    snprintf(path, sizeof(path),
             "/api/me/xbox-saves/download/%s?console_id=%s&target_console_id=%s&profile=%s", titleId,
             sourceConsoleId && sourceConsoleId[0] ? sourceConsoleId : "",
             targetConsoleId && targetConsoleId[0] ? targetConsoleId : "",
             sourceProfile ? sourceProfile : "");

    char skHeader[256];
    snprintf(skHeader, sizeof(skHeader), "X-Session-Key: %s", sessionKey);
    const char *headers[] = { skHeader };

    int status = 0;
    int r = https_get_to_file(host, port, path, headers, 1, destPath, &status);
    return (r == 0) ? TRUE : FALSE;
}

BOOL xblAccountSyncEnabled(const char *host, const char *port, const char *sessionKey,
                           BOOL *enabledOut)
{
    if (enabledOut) {
        *enabledOut = FALSE;
    }
    char skHeader[256];
    snprintf(skHeader, sizeof(skHeader), "X-Session-Key: %s", sessionKey);
    const char *headers[] = { skHeader };
    char path[256];
    snprintf(path, sizeof(path), "/api/me/xbox-account/settings?sessionKey=%s", sessionKey);

    char resp[UPLOAD_RESP_SIZE];
    int r = https_request(host, port, "GET", path, "application/json", headers, 1, NULL, 0, resp,
                          sizeof(resp), NULL, NULL);
    if (r != 0 || !responseIsOk(resp)) {
        return FALSE;
    }
    const char *body = strstr(resp, "\r\n\r\n");
    if (body && strstr(body, "\"enabled\":true")) {
        if (enabledOut) {
            *enabledOut = TRUE;
        }
    }
    return TRUE;
}

BOOL uploadXblAccounts(const char *host, const char *port, const char *sessionKey,
                       const char *consoleId, int partition, const XblAccountSet *set)
{
    if (!set || set->presentCount <= 0) {
        return TRUE; /* nothing to upload is not an error */
    }

    size_t cap = 2048;
    size_t len = 0;
    char *body = (char *)malloc(cap);
    if (!body) {
        return FALSE;
    }
    body[0] = '\0';

    char partStr[16];
    snprintf(partStr, sizeof(partStr), "%d", partition);

    BOOL ok = appendStr(&body, &len, &cap, "{\"sessionKey\":\"", FALSE) &&
              appendStr(&body, &len, &cap, sessionKey, TRUE) &&
              appendStr(&body, &len, &cap, "\",\"console_id\":\"", FALSE) &&
              appendStr(&body, &len, &cap, consoleId ? consoleId : "", TRUE) &&
              appendStr(&body, &len, &cap, "\",\"source_partition\":", FALSE) &&
              appendStr(&body, &len, &cap, partStr, FALSE) &&
              appendStr(&body, &len, &cap, ",\"accounts\":[", FALSE);

    BOOL first = TRUE;
    for (int i = 0; ok && i < XBL_ACCOUNT_MAX; i++) {
        if (!set->slots[i].present) {
            continue;
        }
        char *blobB64 = base64Encode(set->slots[i].raw, XBL_ACCOUNT_LEN);
        if (!blobB64) {
            ok = FALSE;
            break;
        }
        ok = appendStr(&body, &len, &cap, first ? "{" : ",{", FALSE) &&
             appendStr(&body, &len, &cap, "\"xuid\":\"", FALSE) &&
             appendStr(&body, &len, &cap, set->slots[i].xuidHex, TRUE) &&
             appendStr(&body, &len, &cap, "\",\"gamertag\":\"", FALSE) &&
             appendStr(&body, &len, &cap, set->slots[i].gamertag, TRUE) &&
             appendStr(&body, &len, &cap, "\",\"blob_base64\":\"", FALSE) &&
             appendStr(&body, &len, &cap, blobB64, FALSE) &&
             appendStr(&body, &len, &cap, "\"}", FALSE);
        free(blobB64);
        first = FALSE;
    }
    if (ok) {
        ok = appendStr(&body, &len, &cap, "]}", FALSE);
    }
    if (!ok) {
        free(body);
        return FALSE;
    }

    char resp[UPLOAD_RESP_SIZE];
    int r = https_request(host, port, "POST", "/api/me/xbox-account/upload", "application/json",
                          NULL, 0, body, len, resp, sizeof(resp), NULL, NULL);
    free(body);
    return (r == 0) && responseIsOk(resp);
}

BOOL fetchXblRestore(const char *host, const char *port, const char *sessionKey,
                     const char *consoleId, char *out, size_t outsz)
{
    if (outsz == 0) {
        return FALSE;
    }
    out[0] = '\0';
    char skHeader[256];
    snprintf(skHeader, sizeof(skHeader), "X-Session-Key: %s", sessionKey);
    const char *headers[] = { skHeader };
    char path[320];
    snprintf(path, sizeof(path), "/api/me/xbox-account/restore?console_id=%s&sessionKey=%s",
             consoleId ? consoleId : "", sessionKey);

    static char resp[8192];
    int r = https_request(host, port, "GET", path, "application/json", headers, 1, NULL, 0, resp,
                          sizeof(resp), NULL, NULL);
    if (r != 0 || !responseIsOk(resp)) {
        return FALSE;
    }
    const char *body = strstr(resp, "\r\n\r\n");
    const char *start = body ? body + 4 : resp;
    strncpy(out, start, outsz - 1);
    out[outsz - 1] = '\0';
    return TRUE;
}

BOOL confirmXblRestored(const char *host, const char *port, const char *sessionKey, const char *id)
{
    char path[256];
    snprintf(path, sizeof(path), "/api/me/xbox-account/%s/restored", id ? id : "");
    char skHeader[256];
    snprintf(skHeader, sizeof(skHeader), "X-Session-Key: %s", sessionKey);
    const char *headers[] = { skHeader };

    char body[256];
    int blen = snprintf(body, sizeof(body), "{\"sessionKey\":\"%s\"}", sessionKey);
    char resp[UPLOAD_RESP_SIZE];
    int r = https_request(host, port, "POST", path, "application/json", headers, 1, body, blen, resp,
                          sizeof(resp), NULL, NULL);
    return (r == 0) && responseIsOk(resp);
}

BOOL manifestTitleMatches(const char *manifest, const char *consoleId, const char *profile,
                          const char *titleId, const char *fingerprint)
{
    if (!manifest || !titleId || !fingerprint || !fingerprint[0]) {
        return FALSE;
    }
    char needle[128];
    if (consoleId && consoleId[0]) {
        /* Server emits "console_id:profile:title_id=" (profile may be empty). */
        snprintf(needle, sizeof(needle), "%s:%s:%s=", consoleId, profile ? profile : "", titleId);
    } else {
        snprintf(needle, sizeof(needle), "%s=", titleId);
    }
    const char *p = manifest;
    size_t nlen = strlen(needle);
    while ((p = strstr(p, needle)) != NULL) {
        if (p == manifest || p[-1] == '\n') {
            const char *val = p + nlen;
            size_t flen = strlen(fingerprint);
            /* '|' terminates the fingerprint too (e.g. "...=<fp>|nosync"). */
            if (strncmp(val, fingerprint, flen) == 0 &&
                (val[flen] == '\0' || val[flen] == '\r' || val[flen] == '\n' ||
                 val[flen] == '|')) {
                return TRUE;
            }
            return FALSE;
        }
        p += nlen;
    }
    return FALSE;
}

unsigned long long manifestCloudModUnix(const char *manifest, const char *consoleId,
                                        const char *profile, const char *titleId)
{
    if (!manifest || !titleId || !titleId[0]) {
        return 0;
    }
    char needle[128];
    if (consoleId && consoleId[0]) {
        snprintf(needle, sizeof(needle), "%s:%s:%s=", consoleId, profile ? profile : "", titleId);
    } else {
        snprintf(needle, sizeof(needle), "%s=", titleId);
    }
    const char *p = manifest;
    size_t nlen = strlen(needle);
    while ((p = strstr(p, needle)) != NULL) {
        if (p != manifest && p[-1] != '\n') {
            p++;
            continue;
        }
        const char *val = p + nlen;
        const char *mp = strchr(val, '|');
        if (!mp) {
            return 0;
        }
        mp++;
        unsigned long long cloudMod = 0;
        while (*mp >= '0' && *mp <= '9') {
            cloudMod = cloudMod * 10ULL + (unsigned long long)(*mp - '0');
            mp++;
        }
        return cloudMod;
    }
    return 0;
}
