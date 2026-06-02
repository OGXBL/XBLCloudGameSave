#include "xbmc_profiles.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Keep in sync with the server's xboxSanitizeProfile(): keep [A-Za-z0-9_-], drop
 * everything else, cap at 32 chars. The console must produce the same key the
 * server stores so manifest matching lines up. */
static void sanitizeProfile(const char *in, char *out, size_t outsz)
{
    size_t j = 0;
    if (outsz == 0) {
        return;
    }
    for (const char *p = in; p && *p && j < outsz - 1 && j < 32; p++) {
        char c = *p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '_' || c == '-') {
            out[j++] = c;
        }
    }
    out[j] = '\0';
}

static void trimEnds(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ' || s[n - 1] == '\t')) {
        s[--n] = '\0';
    }
    size_t start = 0;
    while (s[start] == ' ' || s[start] == '\t') {
        start++;
    }
    if (start > 0) {
        memmove(s, s + start, strlen(s + start) + 1);
    }
}

/* Extracts the text of an XML element, e.g. <last_profile>NAME</last_profile>.
 * Returns TRUE if a non-empty value was found. */
static BOOL xmlElementText(const char *xml, const char *tag, char *out, size_t outsz)
{
    char openTag[48];
    snprintf(openTag, sizeof(openTag), "<%s>", tag);
    const char *start = strstr(xml, openTag);
    if (!start) {
        return FALSE;
    }
    start += strlen(openTag);
    char closeTag[48];
    snprintf(closeTag, sizeof(closeTag), "</%s>", tag);
    const char *end = strstr(start, closeTag);
    if (!end || end <= start) {
        return FALSE;
    }
    size_t n = (size_t)(end - start);
    if (n >= outsz) {
        n = outsz - 1;
    }
    memcpy(out, start, n);
    out[n] = '\0';
    trimEnds(out);
    return out[0] != '\0';
}

/* Reads udata_settings.xml (written by the XBMC per_profile_saves script). When
 * individual_saves is enabled, copies <last_profile> into out. Returns TRUE only
 * if per-profile saves are enabled and a profile name was found. */
static BOOL parseSettingsFile(const char *filePath, char *out, size_t outsz)
{
    if (outsz == 0) {
        return FALSE;
    }
    out[0] = '\0';
    HANDLE h = CreateFile(filePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    char buf[2048];
    DWORD got = 0;
    BOOL ok = ReadFile(h, buf, sizeof(buf) - 1, &got, NULL);
    CloseHandle(h);
    if (!ok || got == 0) {
        return FALSE;
    }
    buf[got] = '\0';
    /* Must have <individual_saves enabled="true"/>; if disabled, UDATA is the
     * shared baseline (treat as non-XBMC). */
    const char *iv = strstr(buf, "individual_saves");
    if (!iv) {
        return FALSE;
    }
    const char *en = strstr(iv, "enabled=");
    if (!en || !strstr(en, "\"true\"")) {
        /* be tolerant of single quotes */
        if (!en || !strstr(en, "'true'")) {
            return FALSE;
        }
    }
    return xmlElementText(buf, "last_profile", out, outsz);
}

/* Reads XBMC's own profiles.xml (always present once profiles are used). Returns
 * the currently active profile name (the <name> at index <lastloaded>). Only
 * reports success when 2+ profiles exist, so a normal single-profile XBMC install
 * keeps the default (non-per-profile) behavior. */
static BOOL parseProfilesFile(const char *filePath, char *out, size_t outsz)
{
    if (outsz == 0) {
        return FALSE;
    }
    out[0] = '\0';
    HANDLE h = CreateFile(filePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    static char buf[8192];
    DWORD got = 0;
    BOOL ok = ReadFile(h, buf, sizeof(buf) - 1, &got, NULL);
    CloseHandle(h);
    if (!ok || got == 0) {
        return FALSE;
    }
    buf[got] = '\0';

    char lastStr[16];
    int lastLoaded = 0;
    if (xmlElementText(buf, "lastloaded", lastStr, sizeof(lastStr))) {
        lastLoaded = atoi(lastStr);
    }

    /* Walk each <profile>...<name>NAME</name>...</profile> block in order. */
    char names[16][64];
    int count = 0;
    const char *p = buf;
    while (count < 16) {
        const char *blk = strstr(p, "<profile>");
        if (!blk) {
            break;
        }
        const char *blkEnd = strstr(blk, "</profile>");
        const char *nameStart = strstr(blk, "<name>");
        if (nameStart && (!blkEnd || nameStart < blkEnd)) {
            char tmp[64];
            if (xmlElementText(blk, "name", tmp, sizeof(tmp))) {
                strncpy(names[count], tmp, sizeof(names[0]) - 1);
                names[count][sizeof(names[0]) - 1] = '\0';
                count++;
            }
        }
        p = blkEnd ? blkEnd + 10 : (nameStart ? nameStart + 6 : blk + 9);
    }

    if (count < 2) {
        return FALSE; /* single profile -> behave like a normal Xbox */
    }
    if (lastLoaded < 0 || lastLoaded >= count) {
        lastLoaded = 0;
    }
    if (!names[lastLoaded][0]) {
        return FALSE;
    }
    strncpy(out, names[lastLoaded], outsz - 1);
    out[outsz - 1] = '\0';
    return TRUE;
}

/* Checks one directory for the active profile, preferring the per_profile script's
 * udata_settings.xml and falling back to XBMC's profiles.xml. */
static BOOL tryDirForActive(const char *dir, char *out, size_t outsz)
{
    char candidate[MAX_PATH];
    snprintf(candidate, sizeof(candidate), "%s\\udata_settings.xml", dir);
    if (GetFileAttributes(candidate) != INVALID_FILE_ATTRIBUTES &&
        parseSettingsFile(candidate, out, outsz)) {
        return TRUE;
    }
    snprintf(candidate, sizeof(candidate), "%s\\profiles.xml", dir);
    if (GetFileAttributes(candidate) != INVALID_FILE_ATTRIBUTES &&
        parseProfilesFile(candidate, out, outsz)) {
        return TRUE;
    }
    return FALSE;
}

/* Depth-limited recursive search for an XBMC UserData folder (udata_settings.xml
 * or profiles.xml) under dir. Skips a few known-huge folders so this stays fast on
 * real FATX hardware. On success the active profile name is copied to out. */
static BOOL findSettingsRecursive(const char *dir, int depth, char *out, size_t outsz)
{
    if (depth < 0) {
        return FALSE;
    }

    /* Try this directory first. */
    if (tryDirForActive(dir, out, outsz)) {
        return TRUE;
    }
    if (depth == 0) {
        return FALSE;
    }

    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);
    WIN32_FIND_DATA fd;
    HANDLE fh = FindFirstFile(pattern, &fd);
    if (fh == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    BOOL found = FALSE;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            continue;
        }
        const char *name = fd.cFileName;
        if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
            continue;
        }
        /* Avoid wandering into large/irrelevant trees. */
        if (_strnicmp(name, "UDATA", 5) == 0 || _strnicmp(name, "TDATA", 5) == 0 ||
            _strnicmp(name, "GameSaves", 9) == 0 || _strnicmp(name, "Thumbnails", 10) == 0 ||
            _strnicmp(name, "Cache", 5) == 0) {
            continue;
        }
        char child[MAX_PATH];
        snprintf(child, sizeof(child), "%s\\%s", dir, name);
        if (findSettingsRecursive(child, depth - 1, out, outsz)) {
            found = TRUE;
            break;
        }
    } while (FindNextFile(fh, &fd));
    FindClose(fh);
    return found;
}

/* Finds the active XBMC profile name by locating and parsing udata_settings.xml.
 * Tries well-known XBMC4Gamers locations first (fast), then a depth-limited
 * recursive scan, across the UDATA drive plus E: and C:. dirPrefix is the UDATA
 * drive-root portion (e.g. "E:\\"). */
static BOOL findActiveProfile(const char *dirPrefix, char *out, size_t outsz)
{
    /* Build a de-duplicated list of candidate drive roots: UDATA drive, then E:, C:. */
    char drives[3][4];
    int driveCount = 0;
    if (dirPrefix && dirPrefix[0] && dirPrefix[1] == ':') {
        snprintf(drives[driveCount++], sizeof(drives[0]), "%c:", dirPrefix[0]);
    }
    const char *extra[] = { "E:", "C:" };
    for (int e = 0; e < 2 && driveCount < 3; e++) {
        BOOL dup = FALSE;
        for (int j = 0; j < driveCount; j++) {
            if (_strnicmp(drives[j], extra[e], 2) == 0) {
                dup = TRUE;
                break;
            }
        }
        if (!dup) {
            snprintf(drives[driveCount++], sizeof(drives[0]), "%s", extra[e]);
        }
    }

    /* Known XBMC4Gamers / XBMC UserData locations. masterprofile == the UserData
     * root, so udata_settings.xml / profiles.xml sit directly there. */
    const char *subs[] = {
        "\\XBMC4Gamers\\system\\UserData",
        "\\XBMC4Gamers\\UserData",
        "\\Apps\\XBMC4Gamers\\system\\UserData",
        "\\Apps\\XBMC4Gamers\\UserData",
        "\\XBMC\\system\\UserData",
        "\\XBMC\\UserData",
        "\\system\\UserData",
        "\\UserData"
    };
    for (int d = 0; d < driveCount; d++) {
        for (int s = 0; s < (int)(sizeof(subs) / sizeof(subs[0])); s++) {
            char dir[MAX_PATH];
            snprintf(dir, sizeof(dir), "%s%s", drives[d], subs[s]);
            if (tryDirForActive(dir, out, outsz)) {
                return TRUE;
            }
        }
    }

    /* Fallback: depth-limited recursive scan of each drive root. */
    for (int d = 0; d < driveCount; d++) {
        char root[MAX_PATH];
        snprintf(root, sizeof(root), "%s\\", drives[d]);
        size_t rlen = strlen(root);
        if (rlen > 0 && root[rlen - 1] == '\\') {
            root[rlen - 1] = '\0';
        }
        if (findSettingsRecursive(root, 4, out, outsz)) {
            return TRUE;
        }
    }
    return FALSE;
}

int xbmcEnumerateVariations(const char *baseUdataPath, XbmcVariation *out, int maxOut)
{
    if (!baseUdataPath || !out || maxOut <= 0) {
        return 0;
    }

    /* Split base path into "<dirPrefix>" + "<baseName>" (e.g. "E:\\" + "UDATA"). */
    const char *lastSlash = strrchr(baseUdataPath, '\\');
    char dirPrefix[MAX_PATH];
    char baseName[64];
    if (lastSlash) {
        size_t plen = (size_t)(lastSlash - baseUdataPath) + 1; /* include the slash */
        if (plen >= sizeof(dirPrefix)) {
            plen = sizeof(dirPrefix) - 1;
        }
        memcpy(dirPrefix, baseUdataPath, plen);
        dirPrefix[plen] = '\0';
        strncpy(baseName, lastSlash + 1, sizeof(baseName) - 1);
        baseName[sizeof(baseName) - 1] = '\0';
    } else {
        dirPrefix[0] = '\0';
        strncpy(baseName, baseUdataPath, sizeof(baseName) - 1);
        baseName[sizeof(baseName) - 1] = '\0';
    }

    char activeName[80];
    BOOL haveActive = findActiveProfile(dirPrefix, activeName, sizeof(activeName));

    /* Find sibling "UDATA.<profile>" folders (excludes "UDATA" itself and any
     * "UDATA.backup*" baseline). */
    char dotPrefix[80];
    snprintf(dotPrefix, sizeof(dotPrefix), "%s.", baseName); /* "UDATA." */
    size_t dotLen = strlen(dotPrefix);

    char siblingNames[XBMC_MAX_VARIATIONS][80];
    int siblingCount = 0;

    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s%s.*", dirPrefix, baseName); /* "E:\\UDATA.*" */
    WIN32_FIND_DATA fd;
    HANDLE fh = FindFirstFile(pattern, &fd);
    if (fh != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                continue;
            }
            const char *name = fd.cFileName;
            if (strncmp(name, dotPrefix, dotLen) != 0) {
                continue; /* not "UDATA.<something>" */
            }
            const char *suffix = name + dotLen;
            if (!suffix[0]) {
                continue;
            }
            if (_strnicmp(suffix, "backup", 6) == 0) {
                continue; /* baseline copy, not a profile */
            }
            if (siblingCount < XBMC_MAX_VARIATIONS) {
                strncpy(siblingNames[siblingCount], suffix, sizeof(siblingNames[0]) - 1);
                siblingNames[siblingCount][sizeof(siblingNames[0]) - 1] = '\0';
                siblingCount++;
            }
        } while (FindNextFile(fh, &fd));
        FindClose(fh);
    }

    BOOL featureEnabled = haveActive || siblingCount > 0;

    if (!featureEnabled) {
        /* Non-XBMC / single profile: behave exactly like before. */
        out[0].profileKey[0] = '\0';
        out[0].profileLabel[0] = '\0';
        strncpy(out[0].path, baseUdataPath, sizeof(out[0].path) - 1);
        out[0].path[sizeof(out[0].path) - 1] = '\0';
        return 1;
    }

    int count = 0;

    /* Active profile = the live UDATA folder. Name from udata_settings.xml when found. */
    const char *activeLabel = haveActive ? activeName : "active";
    sanitizeProfile(activeLabel, out[count].profileKey, sizeof(out[count].profileKey));
    if (!out[count].profileKey[0]) {
        strncpy(out[count].profileKey, "active", sizeof(out[count].profileKey) - 1);
        out[count].profileKey[sizeof(out[count].profileKey) - 1] = '\0';
    }
    strncpy(out[count].profileLabel, activeLabel, sizeof(out[count].profileLabel) - 1);
    out[count].profileLabel[sizeof(out[count].profileLabel) - 1] = '\0';
    strncpy(out[count].path, baseUdataPath, sizeof(out[count].path) - 1);
    out[count].path[sizeof(out[count].path) - 1] = '\0';
    count++;

    for (int i = 0; i < siblingCount && count < maxOut; i++) {
        char key[40];
        sanitizeProfile(siblingNames[i], key, sizeof(key));
        if (!key[0]) {
            continue;
        }
        /* Skip if it collides with the active profile key (shouldn't normally happen). */
        BOOL dup = FALSE;
        for (int j = 0; j < count; j++) {
            if (strcmp(out[j].profileKey, key) == 0) {
                dup = TRUE;
                break;
            }
        }
        if (dup) {
            continue;
        }
        strncpy(out[count].profileKey, key, sizeof(out[count].profileKey) - 1);
        out[count].profileKey[sizeof(out[count].profileKey) - 1] = '\0';
        strncpy(out[count].profileLabel, siblingNames[i], sizeof(out[count].profileLabel) - 1);
        out[count].profileLabel[sizeof(out[count].profileLabel) - 1] = '\0';
        snprintf(out[count].path, sizeof(out[count].path), "%s%s.%s", dirPrefix, baseName,
                 siblingNames[i]);
        count++;
    }

    return count;
}
