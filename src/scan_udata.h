#ifndef SCAN_UDATA_H
#define SCAN_UDATA_H

#include <windows.h>

#define META_NAME_MAX 256

typedef struct {
    char folderName[META_NAME_MAX];   /* save folder name, e.g. "001aecf7" */
    char saveName[META_NAME_MAX];     /* Name= from SaveMeta.xbx */
    BOOL hasSaveMeta;
    int fileCount;
    unsigned long long totalSize;
    FILETIME lastWrite;
} SaveInfo;

typedef struct {
    char titleId[META_NAME_MAX];      /* 8-char hex folder name */
    char titleName[META_NAME_MAX];    /* TitleName= from TitleMeta.xbx */
    BOOL hasTitleMeta;
    char path[MAX_PATH];              /* full path of the title folder */
    int fileCount;                    /* total files in the title tree */
    unsigned long long totalSize;     /* recursive size of the title tree */
    SaveInfo *saves;
    int saveCount;
    int saveCapacity;
} TitleInfo;

typedef struct {
    TitleInfo *titles;
    int titleCount;
    int titleCapacity;
    int totalSaveCount;
} ScanResult;

/* Walks udataPath (e.g. "E:\\UDATA"), filling result with one entry per title
 * folder and the saves nested within each. Returns FALSE if the folder cannot
 * be opened. The caller must call freeScanResult() when done. */
BOOL scanUdata(const char *udataPath, ScanResult *result);

void freeScanResult(ScanResult *result);

/* Computes a deterministic fingerprint of one title's saves from each save's
 * folder name, file count, total size and last-write time (plus the title
 * totals). The same content yields the same 16-hex-char string across runs;
 * any added, removed or modified save changes it. out needs >= 17 bytes. This
 * lets the uploader skip titles the server already has unchanged. */
void titleFingerprintHex(const TitleInfo *title, char *out, size_t outsz);

/* Human-readable game name for UI (TitleName, or title id if unknown). */
void titleDisplayName(const TitleInfo *title, char *out, size_t outsz);

/* Latest save last-write time on the console (FILETIME -> Unix seconds, UTC).
 * Returns 0 if the title has no saves. */
unsigned long long titleLatestSaveUnix(const TitleInfo *title);

/* Looks up a title in a scan result and returns its latest save mtime (0 if absent). */
unsigned long long scanTitleLatestUnix(const ScanResult *scan, const char *titleId);

/* Writes the content fingerprint of a title (by id) into out. Returns FALSE if
 * the title isn't in the scan. out needs >= 17 bytes. */
BOOL scanTitleFingerprint(const ScanResult *scan, const char *titleId, char *out, size_t outsz);

/* Builds a JSON manifest of per-save metadata for the dashboard (out buffer). */
BOOL titleManifestJson(const TitleInfo *title, char *out, size_t outsz);

/* Text sidecar embedded in each .dukex: "folder=unix_seconds" per line. Used to
 * restore FATX last-write times after a cloud download. */
BOOL titleSyncTimesText(const TitleInfo *title, char *out, size_t outsz);

#endif
