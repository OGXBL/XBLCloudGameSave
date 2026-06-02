#include "parse_meta.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define META_FILE_MAX 8192
#define META_VALUE_MAX 256

static char *readWholeFile(const char *path, DWORD *outSize)
{
    HANDLE h = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return NULL;
    }

    DWORD size = GetFileSize(h, NULL);
    if (size == INVALID_FILE_SIZE) {
        CloseHandle(h);
        return NULL;
    }
    if (size > META_FILE_MAX) {
        size = META_FILE_MAX;
    }

    char *buf = malloc(size + 1);
    if (!buf) {
        CloseHandle(h);
        return NULL;
    }

    DWORD total = 0;
    while (total < size) {
        DWORD got = 0;
        if (!ReadFile(h, buf + total, size - total, &got, NULL) || got == 0) {
            break;
        }
        total += got;
    }
    CloseHandle(h);

    buf[total] = '\0';
    if (outSize) {
        *outSize = total;
    }
    return buf;
}

/* Converts a UTF-16 LE buffer (BOM already detected) to a freshly allocated
 * ASCII string. Non-ASCII code units are replaced with '?'. */
static char *utf16ToAscii(const char *buf, DWORD size)
{
    char *out = malloc(size); /* always larger than the result */
    if (!out) {
        return NULL;
    }

    size_t j = 0;
    for (DWORD i = 2; i + 1 < size; i += 2) {
        unsigned char lo = (unsigned char)buf[i];
        unsigned char hi = (unsigned char)buf[i + 1];
        out[j++] = (hi == 0) ? (char)lo : '?';
    }
    out[j] = '\0';
    return out;
}

static BOOL equalsIgnoreCase(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return FALSE;
        }
        a++;
        b++;
    }
    return *a == *b;
}

static void trimTrailing(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) {
        s[--len] = '\0';
    }
}

static BOOL findKey(const char *text, const char *key, char *out, size_t outSize)
{
    size_t keyLen = strlen(key);
    char section[64] = "";
    char fallback[META_VALUE_MAX];
    BOOL haveFallback = FALSE;

    const char *p = text;
    while (*p) {
        const char *lineEnd = p;
        while (*lineEnd && *lineEnd != '\n' && *lineEnd != '\r') {
            lineEnd++;
        }

        const char *s = p;
        while (s < lineEnd && (*s == ' ' || *s == '\t')) {
            s++;
        }

        if (s < lineEnd && *s == '[') {
            const char *close = s + 1;
            while (close < lineEnd && *close != ']') {
                close++;
            }
            size_t n = (size_t)(close - (s + 1));
            if (n >= sizeof(section)) {
                n = sizeof(section) - 1;
            }
            memcpy(section, s + 1, n);
            section[n] = '\0';
        } else if ((size_t)(lineEnd - s) > keyLen && strncmp(s, key, keyLen) == 0) {
            const char *q = s + keyLen;
            while (q < lineEnd && (*q == ' ' || *q == '\t')) {
                q++;
            }
            if (q < lineEnd && *q == '=') {
                q++;
                while (q < lineEnd && (*q == ' ' || *q == '\t')) {
                    q++;
                }

                BOOL preferred = (section[0] == '\0' || equalsIgnoreCase(section, "default"));
                char *dst = preferred ? out : fallback;
                size_t cap = preferred ? outSize : sizeof(fallback);

                size_t vlen = (size_t)(lineEnd - q);
                if (vlen >= cap) {
                    vlen = cap - 1;
                }
                memcpy(dst, q, vlen);
                dst[vlen] = '\0';
                trimTrailing(dst);

                if (preferred) {
                    return TRUE;
                }
                haveFallback = TRUE;
            }
        }

        p = lineEnd;
        while (*p == '\r' || *p == '\n') {
            p++;
        }
    }

    if (haveFallback) {
        strncpy(out, fallback, outSize - 1);
        out[outSize - 1] = '\0';
        return TRUE;
    }
    return FALSE;
}

BOOL readMetaValue(const char *filePath, const char *key, char *out, size_t outSize)
{
    if (outSize == 0) {
        return FALSE;
    }
    out[0] = '\0';

    DWORD size = 0;
    char *raw = readWholeFile(filePath, &size);
    if (!raw) {
        return FALSE;
    }

    char *text = raw;
    char *converted = NULL;
    if (size >= 2 && (unsigned char)raw[0] == 0xFF && (unsigned char)raw[1] == 0xFE) {
        converted = utf16ToAscii(raw, size);
        if (converted) {
            text = converted;
        }
    }

    BOOL found = findKey(text, key, out, outSize);

    free(converted);
    free(raw);
    return found;
}
