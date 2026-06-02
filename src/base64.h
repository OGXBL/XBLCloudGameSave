#ifndef BASE64_H
#define BASE64_H

#include <stddef.h>

/* Encodes len bytes of data into a NUL-terminated standard base64 string.
 * Returns a malloc'd buffer the caller must free(), or NULL on allocation
 * failure. */
char *base64Encode(const unsigned char *data, size_t len);

/* Decodes a standard base64 string into out (up to outCap bytes). Returns the
 * number of decoded bytes, or -1 on malformed input / insufficient capacity.
 * Whitespace in the input is ignored. */
int base64Decode(const char *text, unsigned char *out, size_t outCap);

#endif
