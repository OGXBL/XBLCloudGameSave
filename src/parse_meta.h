#ifndef PARSE_META_H
#define PARSE_META_H

#include <stddef.h>
#include <windows.h>

/* Reads an Xbox .xbx metadata file (TitleMeta.xbx / SaveMeta.xbx) and extracts
 * the value for a key such as "TitleName" or "Name".
 *
 * The files are INI-style text and may be plain ASCII or UTF-16 LE with a BOM.
 * Localized files repeat the key under several "[XX]" sections; the value from
 * the unsectioned or "[default]" entry is preferred, otherwise the first match
 * is returned.
 *
 * Returns TRUE and fills out (NUL-terminated) on success, FALSE if the file is
 * missing/unreadable or the key is not found. */
BOOL readMetaValue(const char *filePath, const char *key, char *out, size_t outSize);

#endif
