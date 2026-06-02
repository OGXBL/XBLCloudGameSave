#ifndef ZIP_EXPORT_H
#define ZIP_EXPORT_H

#include <windows.h>

/* Recursively archives every file under srcDir into a .zip at zipPath. Archive
 * entry names are relative to srcDir (e.g. "4D530064/TitleMeta.xbx"). The zip is
 * streamed to disk so the whole tree need not fit in RAM. Returns FALSE on a
 * fatal error (cannot create output or finalize the archive). */
BOOL zipDirectory(const char *srcDir, const char *zipPath);

#endif
