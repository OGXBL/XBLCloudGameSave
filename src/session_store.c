#include "session_store.h"

#include <stdio.h>
#include <string.h>

/* On-disk format: line 1 = sessionKey, line 2 = username. */

BOOL loadSession(const char *path, char *sessionKey, size_t skSz, char *username, size_t unSz)
{
    if (sessionKey && skSz) {
        sessionKey[0] = '\0';
    }
    if (username && unSz) {
        username[0] = '\0';
    }

    HANDLE h = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    char buf[1024];
    DWORD read = 0;
    BOOL ok = ReadFile(h, buf, sizeof(buf) - 1, &read, NULL);
    CloseHandle(h);
    if (!ok || read == 0) {
        return FALSE;
    }
    buf[read] = '\0';

    /* First line -> sessionKey. */
    char *nl = strchr(buf, '\n');
    char *line2 = NULL;
    if (nl) {
        *nl = '\0';
        line2 = nl + 1;
    }
    /* Trim a trailing CR if the file used CRLF. */
    size_t l1 = strlen(buf);
    if (l1 && buf[l1 - 1] == '\r') {
        buf[--l1] = '\0';
    }
    if (l1 == 0) {
        return FALSE;
    }
    if (sessionKey && skSz) {
        strncpy(sessionKey, buf, skSz - 1);
        sessionKey[skSz - 1] = '\0';
    }

    if (line2 && username && unSz) {
        char *nl2 = strchr(line2, '\n');
        if (nl2) {
            *nl2 = '\0';
        }
        size_t l2 = strlen(line2);
        if (l2 && line2[l2 - 1] == '\r') {
            line2[l2 - 1] = '\0';
        }
        strncpy(username, line2, unSz - 1);
        username[unSz - 1] = '\0';
    }
    return TRUE;
}

BOOL saveSession(const char *path, const char *sessionKey, const char *username)
{
    HANDLE h = CreateFile(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    char buf[1024];
    int n = snprintf(buf, sizeof(buf), "%s\n%s\n", sessionKey ? sessionKey : "",
                     username ? username : "");
    if (n < 0) {
        CloseHandle(h);
        return FALSE;
    }

    DWORD written = 0;
    BOOL ok = WriteFile(h, buf, (DWORD)n, &written, NULL) && written == (DWORD)n;
    CloseHandle(h);
    return ok;
}

BOOL loadSessionConsoleId(const char *path, char *consoleId, size_t cidSz)
{
    if (!consoleId || cidSz == 0) {
        return FALSE;
    }
    consoleId[0] = '\0';

    HANDLE h = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    char buf[1024];
    DWORD read = 0;
    BOOL ok = ReadFile(h, buf, sizeof(buf) - 1, &read, NULL);
    CloseHandle(h);
    if (!ok || read == 0) {
        return FALSE;
    }
    buf[read] = '\0';

    int line = 0;
    const char *p = buf;
    while (*p && line < 2) {
        if (*p == '\n') {
            line++;
        }
        p++;
    }
    while (*p && (*p == '\r' || *p == '\n')) {
        p++;
    }
    if (!*p) {
        return FALSE;
    }

    size_t i = 0;
    while (*p && *p != '\r' && *p != '\n' && i < cidSz - 1) {
        consoleId[i++] = *p++;
    }
    consoleId[i] = '\0';
    return i > 0;
}

BOOL saveSessionConsoleId(const char *path, const char *consoleId)
{
    char sk[160];
    char un[256];
    sk[0] = '\0';
    un[0] = '\0';
    loadSession(path, sk, sizeof(sk), un, sizeof(un));

    HANDLE h = CreateFile(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    char buf[1024];
    int n = snprintf(buf, sizeof(buf), "%s\n%s\n%s\n", sk, un, consoleId ? consoleId : "");
    if (n < 0) {
        CloseHandle(h);
        return FALSE;
    }

    DWORD written = 0;
    BOOL ok = WriteFile(h, buf, (DWORD)n, &written, NULL) && written == (DWORD)n;
    CloseHandle(h);
    return ok;
}

void clearSession(const char *path)
{
    DeleteFile(path);
}
