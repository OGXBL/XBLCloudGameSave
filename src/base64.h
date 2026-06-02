#ifndef BASE64_H
#define BASE64_H

#include <stddef.h>

/* Encodes len bytes of data into a NUL-terminated standard base64 string.
 * Returns a malloc'd buffer the caller must free(), or NULL on allocation
 * failure. */
char *base64Encode(const unsigned char *data, size_t len);

#endif
