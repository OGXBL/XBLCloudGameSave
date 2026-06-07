#include "scan_udata.h"
#include "parse_meta.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fileTimeCompare(const FILETIME *a, const FILETIME *b)
{
    ULARGE_INTEGER ua, ub;
    ua.LowPart = a->dwLowDateTime;
    ua.HighPart = a->dwHighDateTime;
    ub.LowPart = b->dwLowDateTime;
    ub.HighPart = b->dwHighDateTime;
    if (ua.QuadPart > ub.QuadPart) {
        return 1;
    }
    if (ua.QuadPart < ub.QuadPart) {
        return -1;
    }
    return 0;
}

static void fileTimeMax(FILETIME *latest, const FILETIME *candidate)
{
    if (fileTimeCompare(candidate, latest) > 0) {
        *latest = *candidate;
    }
}

static void saveLatestWriteTime(const char *path, FILETIME *latest)
{
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);

    WIN32_FIND_DATA fd;
    HANDLE h = FindFirstFile(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) {
            continue;
        }
        char child[MAX_PATH];
        snprintf(child, sizeof(child), "%s\\%s", path, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            saveLatestWriteTime(child, latest);
        } else {
            fileTimeMax(latest, &fd.ftLastWriteTime);
        }
    } while (FindNextFile(h, &fd));

    FindClose(h);
}

static unsigned long long dirSize(const char *path, int *fileCount)
{
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);

    WIN32_FIND_DATA fd;
    HANDLE h = FindFirstFile(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }

    unsigned long long total = 0;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) {
            continue;
        }

        char child[MAX_PATH];
        snprintf(child, sizeof(child), "%s\\%s", path, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            total += dirSize(child, fileCount);
        } else {
            total += ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            if (fileCount) {
                (*fileCount)++;
            }
        }
    } while (FindNextFile(h, &fd));

    FindClose(h);
    return total;
}

static void scanSaves(TitleInfo *title)
{
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", title->path);

    WIN32_FIND_DATA fd;
    HANDLE h = FindFirstFile(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            continue;
        }
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) {
            continue;
        }

        if (title->saveCount == title->saveCapacity) {
            int newCap = title->saveCapacity ? title->saveCapacity * 2 : 8;
            SaveInfo *grown = realloc(title->saves, (size_t)newCap * sizeof(SaveInfo));
            if (!grown) {
                break;
            }
            title->saves = grown;
            title->saveCapacity = newCap;
        }

        SaveInfo *save = &title->saves[title->saveCount];
        memset(save, 0, sizeof(*save));
        strncpy(save->folderName, fd.cFileName, sizeof(save->folderName) - 1);

        char savePath[MAX_PATH];
        snprintf(savePath, sizeof(savePath), "%s\\%s", title->path, fd.cFileName);

        save->lastWrite = fd.ftLastWriteTime;
        saveLatestWriteTime(savePath, &save->lastWrite);

        char metaPath[MAX_PATH];
        snprintf(metaPath, sizeof(metaPath), "%s\\SaveMeta.xbx", savePath);
        if (readMetaValue(metaPath, "Name", save->saveName, sizeof(save->saveName))) {
            save->hasSaveMeta = TRUE;
        } else {
            strncpy(save->saveName, "<corrupted save>", sizeof(save->saveName) - 1);
        }

        int fileCount = 0;
        save->totalSize = dirSize(savePath, &fileCount);
        save->fileCount = fileCount;

        title->saveCount++;
    } while (FindNextFile(h, &fd));

    FindClose(h);
}

BOOL scanUdata(const char *udataPath, ScanResult *result)
{
    memset(result, 0, sizeof(*result));

    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", udataPath);

    WIN32_FIND_DATA fd;
    HANDLE h = FindFirstFile(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            continue;
        }
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) {
            continue;
        }

        if (result->titleCount == result->titleCapacity) {
            int newCap = result->titleCapacity ? result->titleCapacity * 2 : 16;
            TitleInfo *grown = realloc(result->titles, (size_t)newCap * sizeof(TitleInfo));
            if (!grown) {
                break;
            }
            result->titles = grown;
            result->titleCapacity = newCap;
        }

        TitleInfo *title = &result->titles[result->titleCount];
        memset(title, 0, sizeof(*title));
        strncpy(title->titleId, fd.cFileName, sizeof(title->titleId) - 1);
        snprintf(title->path, sizeof(title->path), "%s\\%s", udataPath, fd.cFileName);

        char metaPath[MAX_PATH];
        snprintf(metaPath, sizeof(metaPath), "%s\\TitleMeta.xbx", title->path);
        if (readMetaValue(metaPath, "TitleName", title->titleName, sizeof(title->titleName))) {
            title->hasTitleMeta = TRUE;
        } else {
            strncpy(title->titleName, "<unknown title>", sizeof(title->titleName) - 1);
        }

        int fileCount = 0;
        title->totalSize = dirSize(title->path, &fileCount);
        title->fileCount = fileCount;

        scanSaves(title);
        result->totalSaveCount += title->saveCount;

        result->titleCount++;
    } while (FindNextFile(h, &fd));

    FindClose(h);
    return TRUE;
}

static void fnvUpdate(unsigned long long *h, const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < len; i++) {
        *h ^= p[i];
        *h *= 1099511628211ULL; /* FNV-1a 64-bit prime */
    }
}

static unsigned long long fileTimeToUnix(const FILETIME *ft)
{
    ULARGE_INTEGER u;
    u.LowPart = ft->dwLowDateTime;
    u.HighPart = ft->dwHighDateTime;
    const unsigned long long epoch = 116444736000000000ULL;
    if (u.QuadPart < epoch) {
        return 0;
    }
    return (u.QuadPart - epoch) / 10000000ULL;
}

unsigned long long titleLatestSaveUnix(const TitleInfo *title)
{
    unsigned long long latest = 0;
    for (int i = 0; i < title->saveCount; i++) {
        unsigned long long t = fileTimeToUnix(&title->saves[i].lastWrite);
        if (t > latest) {
            latest = t;
        }
    }
    return latest;
}

static BOOL titleIdEqual(const char *a, const char *b)
{
    if (!a || !b) {
        return FALSE;
    }
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return FALSE;
        }
        a++;
        b++;
    }
    return *a == *b;
}

unsigned long long scanTitleLatestUnix(const ScanResult *scan, const char *titleId)
{
    if (!scan || !titleId || !titleId[0]) {
        return 0;
    }
    for (int i = 0; i < scan->titleCount; i++) {
        if (titleIdEqual(scan->titles[i].titleId, titleId)) {
            return titleLatestSaveUnix(&scan->titles[i]);
        }
    }
    return 0;
}

BOOL scanTitleFingerprint(const ScanResult *scan, const char *titleId, char *out, size_t outsz)
{
    if (out && outsz > 0) {
        out[0] = '\0';
    }
    if (!scan || !titleId || !titleId[0] || !out || outsz == 0) {
        return FALSE;
    }
    for (int i = 0; i < scan->titleCount; i++) {
        if (titleIdEqual(scan->titles[i].titleId, titleId)) {
            titleFingerprintHex(&scan->titles[i], out, outsz);
            return out[0] != '\0';
        }
    }
    return FALSE;
}

BOOL titleSyncTimesText(const TitleInfo *title, char *out, size_t outsz)
{
    if (!out || outsz < 8 || !title) {
        return FALSE;
    }
    size_t pos = 0;
    for (int i = 0; i < title->saveCount; i++) {
        const SaveInfo *s = &title->saves[i];
        unsigned long long t = fileTimeToUnix(&s->lastWrite);
        if (t == 0) {
            continue;
        }
        int n = snprintf(out + pos, outsz - pos, "%s=%llu\n", s->folderName, t);
        if (n < 0 || (size_t)n >= outsz - pos) {
            return FALSE;
        }
        pos += (size_t)n;
    }
    if (pos == 0) {
        unsigned long long t = titleLatestSaveUnix(title);
        if (t == 0) {
            out[0] = '\0';
            return FALSE;
        }
        snprintf(out, outsz, "_title=%llu\n", t);
    }
    return TRUE;
}

static void jsonAppendChar(char *out, size_t *pos, size_t cap, char c)
{
    if (*pos + 1 >= cap) {
        return;
    }
    out[(*pos)++] = c;
}

static void jsonAppendStr(char *out, size_t *pos, size_t cap, const char *s)
{
    jsonAppendChar(out, pos, cap, '"');
    for (const char *p = s ? s : ""; *p && *pos + 2 < cap; p++) {
        char c = *p;
        if (c == '"' || c == '\\') {
            jsonAppendChar(out, pos, cap, '\\');
        }
        if (c == '\r' || c == '\n') {
            c = ' ';
        }
        jsonAppendChar(out, pos, cap, c);
    }
    jsonAppendChar(out, pos, cap, '"');
}

static void jsonAppendNum(char *out, size_t *pos, size_t cap, const char *s)
{
    for (const char *p = s ? s : "0"; *p && *pos < cap - 1; p++) {
        jsonAppendChar(out, pos, cap, *p);
    }
}

BOOL titleManifestJson(const TitleInfo *title, char *out, size_t outsz)
{
    if (!out || outsz < 32 || !title) {
        return FALSE;
    }
    size_t pos = 0;
    jsonAppendChar(out, &pos, outsz, '{');
    jsonAppendStr(out, &pos, outsz, "title_id");
    jsonAppendChar(out, &pos, outsz, ':');
    jsonAppendStr(out, &pos, outsz, title->titleId);
    jsonAppendChar(out, &pos, outsz, ',');
    jsonAppendStr(out, &pos, outsz, "title_name");
    jsonAppendChar(out, &pos, outsz, ':');
    jsonAppendStr(out, &pos, outsz, title->titleName);
    jsonAppendChar(out, &pos, outsz, ',');
    jsonAppendStr(out, &pos, outsz, "saves");
    jsonAppendChar(out, &pos, outsz, ':');
    jsonAppendChar(out, &pos, outsz, '[');

    for (int i = 0; i < title->saveCount; i++) {
        const SaveInfo *s = &title->saves[i];
        char num[24];
        if (i > 0) {
            jsonAppendChar(out, &pos, outsz, ',');
        }
        jsonAppendChar(out, &pos, outsz, '{');
        jsonAppendStr(out, &pos, outsz, "folder");
        jsonAppendChar(out, &pos, outsz, ':');
        jsonAppendStr(out, &pos, outsz, s->folderName);
        jsonAppendChar(out, &pos, outsz, ',');
        jsonAppendStr(out, &pos, outsz, "name");
        jsonAppendChar(out, &pos, outsz, ':');
        jsonAppendStr(out, &pos, outsz, s->saveName);
        jsonAppendChar(out, &pos, outsz, ',');
        jsonAppendStr(out, &pos, outsz, "files");
        jsonAppendChar(out, &pos, outsz, ':');
        snprintf(num, sizeof(num), "%d", s->fileCount);
        jsonAppendNum(out, &pos, outsz, num);
        jsonAppendChar(out, &pos, outsz, ',');
        jsonAppendStr(out, &pos, outsz, "bytes");
        jsonAppendChar(out, &pos, outsz, ':');
        snprintf(num, sizeof(num), "%llu", s->totalSize);
        jsonAppendNum(out, &pos, outsz, num);
        jsonAppendChar(out, &pos, outsz, ',');
        jsonAppendStr(out, &pos, outsz, "modified");
        jsonAppendChar(out, &pos, outsz, ':');
        snprintf(num, sizeof(num), "%llu", fileTimeToUnix(&s->lastWrite));
        jsonAppendNum(out, &pos, outsz, num);
        jsonAppendChar(out, &pos, outsz, '}');
        if (pos >= outsz - 4) {
            return FALSE;
        }
    }

    jsonAppendChar(out, &pos, outsz, ']');
    jsonAppendChar(out, &pos, outsz, '}');
    jsonAppendChar(out, &pos, outsz, '\0');
    return TRUE;
}

void titleDisplayName(const TitleInfo *title, char *out, size_t outsz)
{
    if (!out || outsz == 0) {
        return;
    }
    out[0] = '\0';
    if (!title) {
        return;
    }
    if (title->titleName[0] && strcmp(title->titleName, "<unknown title>") != 0) {
        strncpy(out, title->titleName, outsz - 1);
    } else {
        strncpy(out, title->titleId, outsz - 1);
    }
    out[outsz - 1] = '\0';
}

void titleFingerprintHex(const TitleInfo *title, char *out, size_t outsz)
{
    unsigned long long h = 1469598103934665603ULL; /* FNV-1a 64-bit offset basis */
    fnvUpdate(&h, &title->fileCount, sizeof(title->fileCount));
    fnvUpdate(&h, &title->totalSize, sizeof(title->totalSize));
    for (int i = 0; i < title->saveCount; i++) {
        const SaveInfo *s = &title->saves[i];
        fnvUpdate(&h, s->folderName, strlen(s->folderName));
        fnvUpdate(&h, &s->fileCount, sizeof(s->fileCount));
        fnvUpdate(&h, &s->totalSize, sizeof(s->totalSize));
        fnvUpdate(&h, &s->lastWrite, sizeof(s->lastWrite));
    }
    snprintf(out, outsz, "%016llx", h);
}

void freeScanResult(ScanResult *result)
{
    if (!result) {
        return;
    }
    for (int i = 0; i < result->titleCount; i++) {
        free(result->titles[i].saves);
    }
    free(result->titles);
    memset(result, 0, sizeof(*result));
}
