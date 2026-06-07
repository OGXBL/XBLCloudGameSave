#include "unzip_export.h"

#include <stdio.h>
#include <string.h>

#include "miniz.h"

#define XBL_SYNC_TIMES_NAME "_xbl_sync.txt"

static size_t unzipReadCallback(void *opaque, mz_uint64 ofs, void *buf, size_t n)
{
    HANDLE h = *(HANDLE *)opaque;
    LONG hi = (LONG)(ofs >> 32);
    LONG lo = (LONG)(ofs & 0xFFFFFFFFu);
    DWORD moved = SetFilePointer(h, lo, &hi, FILE_BEGIN);
    if (moved == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR) {
        return 0;
    }
    DWORD got = 0;
    if (!ReadFile(h, buf, (DWORD)n, &got, NULL)) {
        return 0;
    }
    return got;
}

static size_t unzipWriteCallback(void *opaque, mz_uint64 ofs, const void *buf, size_t n)
{
    (void)ofs; /* miniz extracts sequentially */
    HANDLE h = *(HANDLE *)opaque;
    DWORD written = 0;
    if (!WriteFile(h, buf, (DWORD)n, &written, NULL)) {
        return 0;
    }
    return written;
}

/* Creates every parent directory of fullPath (which uses '\' separators). */
static void ensureParentDirs(const char *fullPath)
{
    char tmp[MAX_PATH];
    strncpy(tmp, fullPath, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    for (char *p = tmp; *p; p++) {
        if (*p == '\\' && p != tmp) {
            *p = '\0';
            CreateDirectory(tmp, NULL); /* ignore "already exists" */
            *p = '\\';
        }
    }
}

BOOL unzipToDir(const char *dukexPath, const char *destDir)
{
    HANDLE in = CreateFile(dukexPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (in == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    DWORD sizeHigh = 0;
    DWORD sizeLow = GetFileSize(in, &sizeHigh);
    if (sizeLow == INVALID_FILE_SIZE && GetLastError() != NO_ERROR) {
        CloseHandle(in);
        return FALSE;
    }
    mz_uint64 archiveSize = ((mz_uint64)sizeHigh << 32) | sizeLow;

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    zip.m_pRead = unzipReadCallback;
    zip.m_pIO_opaque = &in;

    if (!mz_zip_reader_init(&zip, archiveSize, 0)) {
        CloseHandle(in);
        return FALSE;
    }

    CreateDirectory(destDir, NULL);

    mz_uint count = mz_zip_reader_get_num_files(&zip);
    BOOL ok = TRUE;
    for (mz_uint i = 0; i < count; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) {
            ok = FALSE;
            continue;
        }

        /* Build dest path: destDir + '\' + entry name (entry uses '/'). */
        char dest[MAX_PATH];
        snprintf(dest, sizeof(dest), "%s\\%s", destDir, st.m_filename);
        for (char *p = dest; *p; p++) {
            if (*p == '/') {
                *p = '\\';
            }
        }

        if (mz_zip_reader_is_file_a_directory(&zip, i)) {
            ensureParentDirs(dest);
            CreateDirectory(dest, NULL);
            continue;
        }

        ensureParentDirs(dest);

        HANDLE out = CreateFile(dest, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                NULL);
        if (out == INVALID_HANDLE_VALUE) {
            ok = FALSE;
            continue;
        }
        if (!mz_zip_reader_extract_to_callback(&zip, i, unzipWriteCallback, &out, 0)) {
            ok = FALSE;
        }
        CloseHandle(out);
    }

    mz_zip_reader_end(&zip);
    CloseHandle(in);
    return ok;
}

static void unixToFileTime(unsigned long long unixSec, FILETIME *ft)
{
    ULARGE_INTEGER u;
    u.QuadPart = unixSec * 10000000ULL + 116444736000000000ULL;
    ft->dwLowDateTime = u.LowPart;
    ft->dwHighDateTime = u.HighPart;
}

static void setPathTimes(const char *path, unsigned long long unixSec)
{
    if (!path || !path[0] || unixSec == 0) {
        return;
    }
    FILETIME ft;
    unixToFileTime(unixSec, &ft);
    HANDLE h = CreateFile(path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                          OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        SetFileTime(h, &ft, &ft, &ft);
        CloseHandle(h);
    }
}

void unzipApplySyncTimes(const char *destDir)
{
    if (!destDir || !destDir[0]) {
        return;
    }
    char sidecar[MAX_PATH];
    snprintf(sidecar, sizeof(sidecar), "%s\\%s", destDir, XBL_SYNC_TIMES_NAME);

    HANDLE h = CreateFile(sidecar, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }

    char buf[4096];
    DWORD got = 0;
    if (!ReadFile(h, buf, sizeof(buf) - 1, &got, NULL) || got == 0) {
        CloseHandle(h);
        return;
    }
    CloseHandle(h);
    buf[got] = '\0';

    unsigned long long titleTime = 0;
    char *line = buf;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) {
            *nl = '\0';
        }
        char *eq = strchr(line, '=');
        if (eq) {
            *eq = '\0';
            const char *folder = line;
            unsigned long long t = 0;
            for (const char *p = eq + 1; *p >= '0' && *p <= '9'; p++) {
                t = t * 10ULL + (unsigned long long)(*p - '0');
            }
            if (t > 0) {
                if (strcmp(folder, "_title") == 0) {
                    titleTime = t;
                } else {
                    char savePath[MAX_PATH];
                    snprintf(savePath, sizeof(savePath), "%s\\%s", destDir, folder);
                    setPathTimes(savePath, t);
                    if (t > titleTime) {
                        titleTime = t;
                    }
                }
            }
        }
        if (!nl) {
            break;
        }
        line = nl + 1;
    }

    if (titleTime > 0) {
        setPathTimes(destDir, titleTime);
    }

    DeleteFile(sidecar);
}
