#include "unzip_export.h"

#include <stdio.h>
#include <string.h>

#include "miniz.h"

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
