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
