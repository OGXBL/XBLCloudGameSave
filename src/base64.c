#include "base64.h"

#include <stdlib.h>

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *base64Encode(const unsigned char *data, size_t len)
{
    size_t outLen = ((len + 2) / 3) * 4;
    char *out = (char *)malloc(outLen + 1);
    if (!out) {
        return NULL;
    }

    size_t o = 0;
    size_t i = 0;
    while (i + 3 <= len) {
        unsigned int n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out[o++] = B64[(n >> 18) & 0x3F];
        out[o++] = B64[(n >> 12) & 0x3F];
        out[o++] = B64[(n >> 6) & 0x3F];
        out[o++] = B64[n & 0x3F];
        i += 3;
    }

    size_t rem = len - i;
    if (rem == 1) {
        unsigned int n = data[i] << 16;
        out[o++] = B64[(n >> 18) & 0x3F];
        out[o++] = B64[(n >> 12) & 0x3F];
        out[o++] = '=';
        out[o++] = '=';
    } else if (rem == 2) {
        unsigned int n = (data[i] << 16) | (data[i + 1] << 8);
        out[o++] = B64[(n >> 18) & 0x3F];
        out[o++] = B64[(n >> 12) & 0x3F];
        out[o++] = B64[(n >> 6) & 0x3F];
        out[o++] = '=';
    }

    out[o] = '\0';
    return out;
}

static int b64Val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

int base64Decode(const char *text, unsigned char *out, size_t outCap)
{
    if (!text || !out) {
        return -1;
    }
    int quad[4];
    int q = 0;
    size_t o = 0;
    for (const char *p = text; *p; p++) {
        char c = *p;
        if (c == '=' || c == '\r' || c == '\n' || c == ' ' || c == '\t') {
            continue; /* padding/whitespace: handled by remaining-bit logic below */
        }
        int v = b64Val(c);
        if (v < 0) {
            continue; /* skip any stray characters */
        }
        quad[q++] = v;
        if (q == 4) {
            if (o + 3 > outCap) return -1;
            out[o++] = (unsigned char)((quad[0] << 2) | (quad[1] >> 4));
            out[o++] = (unsigned char)((quad[1] << 4) | (quad[2] >> 2));
            out[o++] = (unsigned char)((quad[2] << 6) | quad[3]);
            q = 0;
        }
    }
    if (q == 2) {
        if (o + 1 > outCap) return -1;
        out[o++] = (unsigned char)((quad[0] << 2) | (quad[1] >> 4));
    } else if (q == 3) {
        if (o + 2 > outCap) return -1;
        out[o++] = (unsigned char)((quad[0] << 2) | (quad[1] >> 4));
        out[o++] = (unsigned char)((quad[1] << 4) | (quad[2] >> 2));
    }
    return (int)o;
}
