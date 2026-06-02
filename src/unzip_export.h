#ifndef UNZIP_EXPORT_H
#define UNZIP_EXPORT_H

#include <windows.h>

/* Extracts every entry of the zip archive at dukexPath into destDir (created if
 * needed), reconstructing subfolders. Both the archive read and the per-entry
 * write are streamed, so large archives need not fit in RAM. Returns FALSE if
 * the archive cannot be opened or read as a zip. Used to restore game saves
 * pulled down from the website into E:\UDATA\<TitleID>\. */
BOOL unzipToDir(const char *dukexPath, const char *destDir);

#endif
