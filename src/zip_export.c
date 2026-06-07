#include "zip_export.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "miniz.h"
#include "scan_udata.h"

/* Largest single file we will read into RAM to compress. Save files are tiny;
 * anything larger is skipped (and noted by the caller's overall result) so a
 * runaway file cannot exhaust the console's memory. */
#define ZIP_MAX_FILE_BYTES (16 * 1024 * 1024)

static size_t zipWriteCallback(void *opaque, mz_uint64 fileOffset,
                               const void *buf, size_t n)
{
    (void)fileOffset; /* miniz writes sequentially in this mode */
    HANDLE h = (HANDLE)opaque;
    DWORD written = 0;
    if (!WriteFile(h, buf, (DWORD)n, &written, NULL)) {
        return 0;
    }
    return written;
}

static BOOL addFile(mz_zip_archive *zip, const char *fullPath, const char *archiveName)
{
    HANDLE h = CreateFile(fullPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    DWORD size = GetFileSize(h, NULL);
    if (size == INVALID_FILE_SIZE || size > ZIP_MAX_FILE_BYTES) {
        CloseHandle(h);
        return FALSE;
    }

    void *data = malloc(size ? size : 1);
    if (!data) {
        CloseHandle(h);
        return FALSE;
    }

    DWORD total = 0;
    while (total < size) {
        DWORD got = 0;
        if (!ReadFile(h, (char *)data + total, size - total, &got, NULL) || got == 0) {
            break;
        }
        total += got;
    }
    CloseHandle(h);

    mz_bool ok = mz_zip_writer_add_mem(zip, archiveName, data, total,
                                       MZ_DEFAULT_COMPRESSION);
    free(data);
    return ok ? TRUE : FALSE;
}

/* Recurses into fullPath. archivePrefix is the slash-terminated path used for
 * archive entry names (empty for the root). */
static void addDirectory(mz_zip_archive *zip, const char *fullPath,
                         const char *archivePrefix)
{
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", fullPath);

    WIN32_FIND_DATA fd;
    HANDLE find = FindFirstFile(pattern, &fd);
    if (find == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) {
            continue;
        }

        char childPath[MAX_PATH];
        snprintf(childPath, sizeof(childPath), "%s\\%s", fullPath, fd.cFileName);

        char childArchive[MAX_PATH];
        snprintf(childArchive, sizeof(childArchive), "%s%s", archivePrefix, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            char nextPrefix[MAX_PATH];
            snprintf(nextPrefix, sizeof(nextPrefix), "%s/", childArchive);
            addDirectory(zip, childPath, nextPrefix);
        } else {
            addFile(zip, childPath, childArchive);
        }
    } while (FindNextFile(find, &fd));

    FindClose(find);
}

#define XBL_SYNC_TIMES_NAME "_xbl_sync.txt"

BOOL zipDirectory(const char *srcDir, const char *zipPath)
{
    HANDLE out = CreateFile(zipPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                            NULL);
    if (out == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    zip.m_pWrite = zipWriteCallback;
    zip.m_pIO_opaque = out;

    if (!mz_zip_writer_init_v2(&zip, 0, MZ_ZIP_FLAG_WRITE_ZIP64)) {
        CloseHandle(out);
        return FALSE;
    }

    addDirectory(&zip, srcDir, "");

    BOOL ok = mz_zip_writer_finalize_archive(&zip) ? TRUE : FALSE;
    mz_zip_writer_end(&zip);
    CloseHandle(out);
    return ok;
}

BOOL zipTitleDirectory(const TitleInfo *title, const char *zipPath)
{
    if (!title || !title->path[0]) {
        return FALSE;
    }

    HANDLE out = CreateFile(zipPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                            NULL);
    if (out == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    zip.m_pWrite = zipWriteCallback;
    zip.m_pIO_opaque = out;

    if (!mz_zip_writer_init_v2(&zip, 0, MZ_ZIP_FLAG_WRITE_ZIP64)) {
        CloseHandle(out);
        return FALSE;
    }

    addDirectory(&zip, title->path, "");

    char times[2048];
    if (titleSyncTimesText(title, times, sizeof(times))) {
        mz_zip_writer_add_mem(&zip, XBL_SYNC_TIMES_NAME, times, strlen(times),
                              MZ_DEFAULT_COMPRESSION);
    }

    BOOL ok = mz_zip_writer_finalize_archive(&zip) ? TRUE : FALSE;
    mz_zip_writer_end(&zip);
    CloseHandle(out);
    return ok;
}
