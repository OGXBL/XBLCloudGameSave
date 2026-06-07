#include "save_dates.h"

#include <stdio.h>
#include <string.h>

#define SAVE_DATES_MAX 256
#define SD_TITLE_LEN   64
#define SD_PROFILE_LEN 40
#define SD_FP_LEN      24

typedef struct {
    char titleId[SD_TITLE_LEN];
    char profile[SD_PROFILE_LEN];
    char fingerprint[SD_FP_LEN];
    unsigned long long modUnix;
} SaveDateEntry;

static SaveDateEntry g_entries[SAVE_DATES_MAX];
static int g_count = 0;

static void sdCopy(char *dst, size_t dstSz, const char *src)
{
    if (!dst || dstSz == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dstSz - 1);
    dst[dstSz - 1] = '\0';
}

static BOOL sdSameKey(const SaveDateEntry *e, const char *titleId, const char *profile)
{
    const char *prof = profile ? profile : "";
    return strcmp(e->titleId, titleId ? titleId : "") == 0 &&
           strcmp(e->profile, prof) == 0;
}

static SaveDateEntry *sdFind(const char *titleId, const char *profile)
{
    for (int i = 0; i < g_count; i++) {
        if (sdSameKey(&g_entries[i], titleId, profile)) {
            return &g_entries[i];
        }
    }
    return NULL;
}

/* Parse one line: titleId:profile=fingerprint|modUnix  (profile may be empty). */
static void sdParseLine(char *line)
{
    if (line[0] == '\0' || g_count >= SAVE_DATES_MAX) {
        return;
    }
    char *colon = strchr(line, ':');
    char *eq = colon ? strchr(colon + 1, '=') : NULL;
    char *bar = eq ? strchr(eq + 1, '|') : NULL;
    if (!colon || !eq || !bar) {
        return;
    }
    *colon = '\0';
    *eq = '\0';
    *bar = '\0';

    const char *titleId = line;
    const char *profile = colon + 1;
    const char *fingerprint = eq + 1;
    const char *modStr = bar + 1;

    unsigned long long mod = 0;
    for (const char *p = modStr; *p >= '0' && *p <= '9'; p++) {
        mod = mod * 10ULL + (unsigned long long)(*p - '0');
    }

    SaveDateEntry *e = &g_entries[g_count++];
    sdCopy(e->titleId, sizeof(e->titleId), titleId);
    sdCopy(e->profile, sizeof(e->profile), profile);
    sdCopy(e->fingerprint, sizeof(e->fingerprint), fingerprint);
    e->modUnix = mod;
}

void saveDatesLoad(const char *path)
{
    g_count = 0;
    if (!path || !path[0]) {
        return;
    }

    HANDLE h = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }

    /* SAVE_DATES_MAX lines of at most ~130 bytes; 48 KB is plenty. */
    static char buf[49152];
    DWORD total = 0;
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(h, buf + total, (DWORD)(sizeof(buf) - 1 - total), &read, NULL) || read == 0) {
            break;
        }
        total += read;
        if (total >= sizeof(buf) - 1) {
            break;
        }
    }
    CloseHandle(h);
    buf[total] = '\0';

    char *line = buf;
    while (line && *line) {
        char *nl = line;
        while (*nl && *nl != '\n' && *nl != '\r') {
            nl++;
        }
        BOOL more = (*nl != '\0');
        char *next = NULL;
        if (more) {
            char term = *nl;
            *nl = '\0';
            next = nl + 1;
            /* swallow a paired \r\n / \n\r */
            if ((term == '\r' && *next == '\n') || (term == '\n' && *next == '\r')) {
                next++;
            }
        }
        sdParseLine(line);
        line = more ? next : NULL;
    }
}

unsigned long long saveDatesLookup(const char *titleId, const char *profile,
                                   const char *fingerprint)
{
    if (!titleId || !titleId[0]) {
        return 0;
    }
    SaveDateEntry *e = sdFind(titleId, profile);
    if (!e) {
        return 0;
    }
    /* Only trust the stored date if the content is unchanged (fingerprint
     * matches). A different fingerprint means the save was really edited. */
    if (!fingerprint || strcmp(e->fingerprint, fingerprint) != 0) {
        return 0;
    }
    return e->modUnix;
}

void saveDatesRecord(const char *titleId, const char *profile, const char *fingerprint,
                     unsigned long long modUnix)
{
    if (!titleId || !titleId[0]) {
        return;
    }
    SaveDateEntry *e = sdFind(titleId, profile);
    if (!e) {
        if (g_count >= SAVE_DATES_MAX) {
            return;
        }
        e = &g_entries[g_count++];
        sdCopy(e->titleId, sizeof(e->titleId), titleId);
        sdCopy(e->profile, sizeof(e->profile), profile);
    }
    sdCopy(e->fingerprint, sizeof(e->fingerprint), fingerprint);
    e->modUnix = modUnix;
}

BOOL saveDatesFlush(const char *path)
{
    if (!path || !path[0]) {
        return FALSE;
    }

    HANDLE h = CreateFile(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    BOOL ok = TRUE;
    for (int i = 0; i < g_count && ok; i++) {
        const SaveDateEntry *e = &g_entries[i];
        char line[160];
        int n = snprintf(line, sizeof(line), "%s:%s=%s|%llu\r\n", e->titleId, e->profile,
                         e->fingerprint, e->modUnix);
        if (n <= 0) {
            continue;
        }
        if (n > (int)sizeof(line)) {
            n = (int)sizeof(line);
        }
        DWORD written = 0;
        ok = WriteFile(h, line, (DWORD)n, &written, NULL) && written == (DWORD)n;
    }
    CloseHandle(h);
    return ok;
}
