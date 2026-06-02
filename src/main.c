#include <hal/debug.h>
#include <hal/video.h>
#include <lwip/netif.h>
#include <lwip/tcpip.h>
#include <nxdk/mount.h>
#include <nxdk/net.h>
#include <stdio.h>
#include <windows.h>

#include "app_input.h"
#include "app_ui.h"
#include "eeprom_export.h"
#include "net_auth.h"
#include "report.h"
#include "scan_udata.h"
#include "session_store.h"
#include "unzip_export.h"
#include "upload.h"
#include "zip_export.h"

#define UDATA_PATH "E:\\UDATA"
#define OUTPUT_DIR "E:\\GameSaves"

#define EEPROM_BIN_PATH OUTPUT_DIR "\\eeprom.bin"
#define HDD_KEY_PATH    OUTPUT_DIR "\\hdd_key.txt"
#define REPORT_PATH     OUTPUT_DIR "\\saves_report.txt"
#define SESSION_PATH    OUTPUT_DIR "\\insignia_session.txt"

/* The Insignia Stats website that receives the upload. Change these to match your
 * deployment (the public host of the "insignia stats" Node server). */
#define UPLOAD_HOST "xb.live"
#define UPLOAD_PORT "443"

extern struct netif *g_pnetif;

static BOOL waitForNetwork(void)
{
    /* Network status shown once progress UI is active. */
    nxNetInit(NULL);
    for (int wait = 0; wait < 90; wait++) {
        if (g_pnetif && netif_ip4_addr(g_pnetif)->addr != 0) {
            return TRUE;
        }
        Sleep(1000);
    }
    return FALSE;
}

int main(void)
{
    XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);
    ui_drawHeader("OG XBL Save Backup");

    /* 1. Mount E: and create the output folder (also holds the saved session). */
    if (!nxIsDriveMounted('E')) {
        if (!nxMountDrive('E', "\\Device\\Harddisk0\\Partition1\\")) {
            debugMoveCursor(40, 200);
            debugPrint("ERROR: could not mount E: drive\n");
            inputWaitExitToDashboard("Could not mount E: drive.", NULL);
            return 1;
        }
    }

    if (!CreateDirectory(OUTPUT_DIR, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        debugMoveCursor(40, 200);
        debugPrint("ERROR: could not create %s\n", OUTPUT_DIR);
        inputWaitExitToDashboard("Could not create output folder.", NULL);
        return 1;
    }

    /* 2. Network + Insignia login. Reuse a saved session if it is still valid;
     * only show the QR code when there is no valid stored login. */
    char sessionKey[160];
    char username[256];
    BOOL loggedIn = FALSE;
    sessionKey[0] = '\0';
    username[0] = '\0';

    if (waitForNetwork()) {
        if (loadSession(SESSION_PATH, sessionKey, sizeof(sessionKey), username, sizeof(username)) &&
            verifySession(sessionKey, username, sizeof(username))) {
            loggedIn = TRUE;
            ui_showLoggedIn(username);
            Sleep(2000);
        } else {
            loggedIn = deviceLogin(sessionKey, sizeof(sessionKey), username, sizeof(username));
            if (loggedIn) {
                saveSession(SESSION_PATH, sessionKey, username);
            }
        }
    }

    ui_beginProgress();
    ui_logf("Connecting to network...");
    if (!loggedIn) {
        ui_logf("No login - local backup only.");
    }

    /* 3. EEPROM + HDD key. */
    ui_logf("Reading EEPROM and HDD key...");
    unsigned char eeprom[EEPROM_SIZE];
    BOOL eepromOk = dumpEeprom(eeprom);
    char serial[32] = "";
    char hddKeyHex[64] = "";
    if (eepromOk) {
        if (!writeFileBytes(EEPROM_BIN_PATH, eeprom, EEPROM_SIZE)) {
            ui_logf("  WARNING: failed to write eeprom.bin");
        }
        if (!writeHddKeyFile(HDD_KEY_PATH, eeprom)) {
            ui_logf("  WARNING: failed to write hdd_key.txt");
        }
        getEepromSerial(eeprom, serial, sizeof(serial));
        getHddKeyHex(hddKeyHex, sizeof(hddKeyHex));
        ui_logf("  EEPROM and HDD key saved");
    } else {
        ui_logf("  WARNING: EEPROM read failed");
    }

    ui_setProgress(0.08f, "Scanning saves...");
    /* 4. Scan UDATA and write the report. */
    ui_logf("Scanning %s...", UDATA_PATH);
    ScanResult scan;
    BOOL haveScan = scanUdata(UDATA_PATH, &scan);
    if (haveScan) {
        ui_logf("  Found %d titles, %d saves", scan.titleCount, scan.totalSaveCount);
        if (writeReport(REPORT_PATH, &scan)) {
            ui_logf("  Report written to saves_report.txt");
        } else {
            ui_logf("  WARNING: failed to write report");
        }
    } else {
        ui_logf("  WARNING: could not open %s", UDATA_PATH);
    }

    /* 5. Console data upload (EEPROM + HDD key) — tiny, always sent when logged in. */
    char consoleId[40];
    consoleId[0] = '\0';
    if (loggedIn) {
        loadSessionConsoleId(SESSION_PATH, consoleId, sizeof(consoleId));
    }

    if (loggedIn && eepromOk) {
        ui_logf("Uploading to %s...", UPLOAD_HOST);
        if (uploadConsoleData(UPLOAD_HOST, UPLOAD_PORT, sessionKey, serial, hddKeyHex, eeprom,
                              EEPROM_SIZE, consoleId, sizeof(consoleId))) {
            ui_logf("  Uploaded EEPROM + HDD key (%s)", consoleId);
            saveSessionConsoleId(SESSION_PATH, consoleId);
        } else {
            ui_logf("  WARNING: console-data upload failed");
        }
    }

    /* 6. Per-game archives + incremental upload. Games whose saves match the
     * server's stored fingerprint are skipped entirely (no re-zip, no upload);
     * only new or changed saves are archived and sent. */
    static char manifest[16384];
    manifest[0] = '\0';
    if (loggedIn) {
        fetchSavesManifest(UPLOAD_HOST, UPLOAD_PORT, sessionKey, manifest, sizeof(manifest));
    }

    ui_logf("Backing up each game...");
    if (haveScan) {
        int created = 0, uploaded = 0, skipped = 0;
        int uploadTotal = scan.titleCount > 0 ? scan.titleCount : 1;
        if (loggedIn) {
            ui_setUploadStats(0, scan.titleCount);
        }
        for (int i = 0; i < scan.titleCount; i++) {
            const TitleInfo *title = &scan.titles[i];
            char gameName[META_NAME_MAX];
            char progLabel[80];
            titleDisplayName(title, gameName, sizeof(gameName));
            snprintf(progLabel, sizeof(progLabel), "Processing %s", gameName);
            ui_setProgress(0.15f + 0.65f * ((float)i / (float)uploadTotal), progLabel);

            char fp[24];
            titleFingerprintHex(title, fp, sizeof(fp));

            if (loggedIn && manifestTitleMatches(manifest, consoleId, title->titleId, fp)) {
                skipped++;
                ui_logf("  %s unchanged (skip)", gameName);
                continue; /* server already has this exact version */
            }

            char zipPath[MAX_PATH];
            char dukexPath[MAX_PATH];
            snprintf(zipPath, sizeof(zipPath), "%s\\%s.zip", OUTPUT_DIR, title->titleId);
            snprintf(dukexPath, sizeof(dukexPath), "%s\\%s.dukex", OUTPUT_DIR, title->titleId);

            if (!zipDirectory(title->path, zipPath)) {
                ui_logf("  WARNING: failed to zip %s", gameName);
                continue;
            }
            DeleteFile(dukexPath);
            if (!MoveFile(zipPath, dukexPath)) {
                ui_logf("  WARNING: could not rename %s", gameName);
                continue;
            }
            created++;

            if (loggedIn) {
                unsigned long long saveMod = titleLatestSaveUnix(title);
                char manifestJson[4096];
                manifestJson[0] = '\0';
                titleManifestJson(title, manifestJson, sizeof(manifestJson));
                if (uploadGameDukex(UPLOAD_HOST, UPLOAD_PORT, sessionKey, consoleId, hddKeyHex,
                                    title->titleId, title->titleName, title->saveCount,
                                    title->totalSize, fp, saveMod, manifestJson, dukexPath)) {
                    uploaded++;
                    ui_setUploadStats(uploaded, scan.titleCount);
                    ui_logf("  Uploaded %s", gameName);
                } else {
                    ui_logf("  WARNING: upload failed for %s", gameName);
                }
            }
        }
        ui_setProgress(0.85f, "Backup complete");
        ui_logf("  %d archived, %d uploaded, %d skipped", created, uploaded, skipped);
    } else {
        ui_logf("  WARNING: skipped (no scan results)");
    }

    if (loggedIn) {
        ui_logf("View saves on xb.live (Game Saves tab)");
    }

    /* 7. Pull down any game saves that exist on the server but not on this Xbox,
     * extracting them into E:\UDATA\<TitleID>\. */
    if (loggedIn && manifest[0]) {
        ui_setProgress(0.90f, "Checking downloads...");
        ui_logf("Checking for saves to download...");
        CreateDirectory(UDATA_PATH, NULL);
        int pulled = 0;
        const char *p = manifest;
        while (*p) {
            char lineKey[96];
            int k = 0;
            while (*p && *p != '=' && *p != '\r' && *p != '\n' && k < (int)sizeof(lineKey) - 1) {
                lineKey[k++] = *p++;
            }
            lineKey[k] = '\0';
            /* Capture the rest of the line (value) to detect the "|nosync" marker, which
             * means the website opted this save out of being synced back to the console. */
            char lineVal[128];
            int vk = 0;
            while (*p && *p != '\r' && *p != '\n') {
                if (vk < (int)sizeof(lineVal) - 1) {
                    lineVal[vk++] = *p;
                }
                p++;
            }
            lineVal[vk] = '\0';
            while (*p && *p != '\n') {
                p++;
            }
            if (*p == '\n') {
                p++;
            }
            if (k == 0) {
                continue;
            }
            if (strstr(lineVal, "|nosync") != NULL) {
                continue; /* website disabled sync-back for this title */
            }

            char srcConsole[40];
            char tid[64];
            srcConsole[0] = '\0';
            tid[0] = '\0';
            {
                const char *colon = strchr(lineKey, ':');
                if (colon) {
                    size_t clen = (size_t)(colon - lineKey);
                    if (clen < sizeof(srcConsole)) {
                        memcpy(srcConsole, lineKey, clen);
                        srcConsole[clen] = '\0';
                    }
                    strncpy(tid, colon + 1, sizeof(tid) - 1);
                    tid[sizeof(tid) - 1] = '\0';
                } else {
                    strncpy(tid, lineKey, sizeof(tid) - 1);
                    tid[sizeof(tid) - 1] = '\0';
                }
            }
            if (!tid[0]) {
                continue;
            }
            if (srcConsole[0] && consoleId[0] && strcmp(srcConsole, consoleId) == 0) {
                continue; /* already uploaded from this console */
            }

            BOOL localHasIt = FALSE;
            if (haveScan) {
                for (int i = 0; i < scan.titleCount; i++) {
                    if (strcmp(scan.titles[i].titleId, tid) == 0) {
                        localHasIt = TRUE;
                        break;
                    }
                }
            }
            if (localHasIt) {
                continue; /* UDATA folder already has this title */
            }

            char dukexPath[MAX_PATH];
            char destDir[MAX_PATH];
            snprintf(dukexPath, sizeof(dukexPath), "%s\\%s.dukex", OUTPUT_DIR, tid);
            snprintf(destDir, sizeof(destDir), "%s\\%s", UDATA_PATH, tid);

            if (!downloadGameDukex(UPLOAD_HOST, UPLOAD_PORT, sessionKey,
                                   srcConsole[0] ? srcConsole : "legacy",
                                   consoleId[0] ? consoleId : "", tid, dukexPath)) {
                ui_logf("  WARNING: download failed for %s", tid);
                continue;
            }
            if (unzipToDir(dukexPath, destDir)) {
                pulled++;
                ui_logf("  Restored %s", tid);
            } else {
                ui_logf("  WARNING: could not extract %s", tid);
            }
        }
        ui_setProgress(1.0f, "Done");
        ui_logf("  Downloaded %d save(s) from server", pulled);
    }

    if (haveScan) {
        freeScanResult(&scan);
    }

    {
        char line1[128];
        char line2[128];
        snprintf(line1, sizeof(line1), "Local output: %s", OUTPUT_DIR);
        snprintf(line2, sizeof(line2), "View saves on xb.live - Game Saves tab.");
        inputWaitExitToDashboard(line1, line2);
    }
    return 0;
}
