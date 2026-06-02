#include "report.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void writeLine(HANDLE h, const char *fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len < 0) {
        return;
    }
    if (len > (int)sizeof(buf)) {
        len = (int)sizeof(buf);
    }

    DWORD written = 0;
    WriteFile(h, buf, (DWORD)len, &written, NULL);
}

BOOL writeReport(const char *path, const ScanResult *result)
{
    HANDLE h = CreateFile(path, GENERIC_WRITE, 0, NULL,
                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    SYSTEMTIME now;
    GetLocalTime(&now);

    writeLine(h, "=== Original Xbox Save Scan ===\r\n");
    writeLine(h, "Date: %04d-%02d-%02d %02d:%02d:%02d\r\n",
              now.wYear, now.wMonth, now.wDay,
              now.wHour, now.wMinute, now.wSecond);
    writeLine(h, "Title folders: %d\r\n", result->titleCount);
    writeLine(h, "Save folders: %d\r\n\r\n", result->totalSaveCount);

    for (int i = 0; i < result->titleCount; i++) {
        const TitleInfo *title = &result->titles[i];

        writeLine(h, "[TITLE] %s\r\n", title->titleId);
        writeLine(h, "  TitleName: %s%s\r\n", title->titleName,
                  title->hasTitleMeta ? "" : " (no TitleMeta.xbx)");
        writeLine(h, "  Path: %s\r\n", title->path);
        writeLine(h, "  Files: %d (%llu bytes)\r\n",
                  title->fileCount, title->totalSize);

        for (int j = 0; j < title->saveCount; j++) {
            const SaveInfo *save = &title->saves[j];
            writeLine(h, "  [SAVE] %s\r\n", save->folderName);
            writeLine(h, "    Name: %s%s\r\n", save->saveName,
                      save->hasSaveMeta ? "" : " (no SaveMeta.xbx)");
            writeLine(h, "    Files: %d (%llu bytes)\r\n",
                      save->fileCount, save->totalSize);
        }

        writeLine(h, "\r\n");
    }

    CloseHandle(h);
    return TRUE;
}
