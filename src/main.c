#include <hal/debug.h>
#include <hal/video.h>
#include <lwip/netif.h>
#include <lwip/tcpip.h>
#include <nxdk/mount.h>
#include <nxdk/net.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "app_input.h"
#include "app_ui.h"
#include "base64.h"
#include "eeprom_export.h"
#include "net_auth.h"
#include "report.h"
#include "scan_udata.h"
#include "session_store.h"
#include "unzip_export.h"
#include "upload.h"
#include "xbmc_profiles.h"
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

    /* Best-effort mount of C: as well — XBMC4Gamers (and its UserData/profiles)
     * commonly lives on C: or E:. Failure is non-fatal; detection just skips it. */
    if (!nxIsDriveMounted('C')) {
        nxMountDrive('C', "\\Device\\Harddisk0\\Partition2\\");
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
    /* 4. Detect XBMC per-profile saves, then scan each profile's UDATA. Without
     * XBMC this is a single variation with an empty profile, so the scan/upload
     * behaves exactly like before. */
    XbmcVariation vars[XBMC_MAX_VARIATIONS];
    int varCount = xbmcEnumerateVariations(UDATA_PATH, vars, XBMC_MAX_VARIATIONS);
    if (varCount <= 0) {
        varCount = 1;
        vars[0].profileKey[0] = '\0';
        vars[0].profileLabel[0] = '\0';
        strncpy(vars[0].path, UDATA_PATH, sizeof(vars[0].path) - 1);
        vars[0].path[sizeof(vars[0].path) - 1] = '\0';
    }
    if (varCount > 1 || vars[0].profileKey[0]) {
        ui_logf("XBMC profiles detected: %d variation(s)", varCount);
        for (int v = 0; v < varCount; v++) {
            ui_logf("  profile[%d]: '%s' <- %s", v,
                    vars[v].profileLabel[0] ? vars[v].profileLabel : "(default)", vars[v].path);
        }
    } else {
        ui_logf("No XBMC profiles found - single UDATA");
    }

    ScanResult scans[XBMC_MAX_VARIATIONS];
    BOOL haveScans[XBMC_MAX_VARIATIONS];
    int grandTotalTitles = 0;
    for (int v = 0; v < varCount; v++) {
        if (vars[v].profileLabel[0]) {
            ui_logf("Scanning %s [%s]...", vars[v].path, vars[v].profileLabel);
        } else {
            ui_logf("Scanning %s...", vars[v].path);
        }
        haveScans[v] = scanUdata(vars[v].path, &scans[v]);
        if (haveScans[v]) {
            ui_logf("  Found %d titles, %d saves", scans[v].titleCount, scans[v].totalSaveCount);
            grandTotalTitles += scans[v].titleCount;
        } else {
            ui_logf("  WARNING: could not open %s", vars[v].path);
        }
    }
    /* Report covers the active profile's UDATA (vars[0]). */
    if (haveScans[0]) {
        if (writeReport(REPORT_PATH, &scans[0])) {
            ui_logf("  Report written to saves_report.txt");
        } else {
            ui_logf("  WARNING: failed to write report");
        }
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

    /* 5b. Optional Xbox LIVE (Insignia) account backup. On hard drives the account
     * records live in the raw-disk config area (sectors 12-19, one 0x6C record per
     * sector at offset 0x0C), NOT in a file. Nothing is uploaded unless the user
     * turned this on at xb.live (off by default). */
    if (loggedIn && consoleId[0]) {
        XblAccountSet accts;
        if (xblReadAccounts(&accts)) {
            ui_logf("Xbox LIVE accounts found: %d", accts.presentCount);
            for (int i = 0; i < XBL_ACCOUNT_MAX; i++) {
                if (accts.slots[i].present) {
                    ui_logf("  slot %d: %s", i, accts.slots[i].gamertag);
                }
            }
            BOOL acctEnabled = FALSE;
            if (xblAccountSyncEnabled(UPLOAD_HOST, UPLOAD_PORT, sessionKey, &acctEnabled) &&
                acctEnabled) {
                if (accts.presentCount > 0) {
                    if (uploadXblAccounts(UPLOAD_HOST, UPLOAD_PORT, sessionKey, consoleId,
                                          accts.partition, &accts)) {
                        ui_logf("  Uploaded %d account(s) to xb.live", accts.presentCount);
                    } else {
                        ui_logf("  WARNING: account upload failed");
                    }
                }
            }
        } else {
            ui_logf("Xbox LIVE accounts: none readable");
        }

        /* Restore any accounts the user queued for THIS Xbox on the website (mimics
         * copying an account from a memory unit). */
        static char restoreText[8192];
        if (fetchXblRestore(UPLOAD_HOST, UPLOAD_PORT, sessionKey, consoleId, restoreText,
                            sizeof(restoreText)) && restoreText[0]) {
            XblAccountSet cur;
            if (!xblReadAccounts(&cur)) {
                memset(&cur, 0, sizeof(cur));
            }
            int restored = 0;
            char *line = restoreText;
            while (line && *line) {
                char *nl = strchr(line, '\n');
                if (nl) {
                    *nl = '\0';
                }
                /* Line: "id:slot:xuid:blobBase64" */
                char *c1 = strchr(line, ':');
                char *c2 = c1 ? strchr(c1 + 1, ':') : NULL;
                char *c3 = c2 ? strchr(c2 + 1, ':') : NULL;
                if (c1 && c2 && c3) {
                    *c1 = *c2 = *c3 = '\0';
                    const char *idStr = line;
                    int wantSlot = atoi(c1 + 1);
                    const char *xuid = c2 + 1;
                    const char *blobB64 = c3 + 1;
                    unsigned char rec[XBL_ACCOUNT_LEN];
                    int n = base64Decode(blobB64, rec, sizeof(rec));
                    if (n == XBL_ACCOUNT_LEN) {
                        int slot = (wantSlot >= 0 && wantSlot < XBL_ACCOUNT_MAX)
                                       ? wantSlot
                                       : xblPickRestoreSlot(&cur, xuid);
                        if (slot >= 0 && xblWriteAccount(slot, rec)) {
                            /* keep our local view in sync for subsequent picks */
                            memcpy(cur.slots[slot].raw, rec, XBL_ACCOUNT_LEN);
                            cur.slots[slot].present = TRUE;
                            strncpy(cur.slots[slot].xuidHex, xuid,
                                    sizeof(cur.slots[slot].xuidHex) - 1);
                            confirmXblRestored(UPLOAD_HOST, UPLOAD_PORT, sessionKey, idStr);
                            restored++;
                            ui_logf("  Restored account to slot %d", slot);
                        } else {
                            ui_logf("  WARNING: no free account slot");
                        }
                    }
                }
                if (!nl) {
                    break;
                }
                line = nl + 1;
            }
            if (restored > 0) {
                ui_logf("  Installed %d Xbox LIVE account(s)", restored);
            }
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
    if (grandTotalTitles > 0) {
        int created = 0, uploaded = 0, skipped = 0;
        int uploadTotal = grandTotalTitles > 0 ? grandTotalTitles : 1;
        int doneCount = 0;
        if (loggedIn) {
            ui_setUploadStats(0, grandTotalTitles);
        }
        for (int v = 0; v < varCount; v++) {
            if (!haveScans[v]) {
                continue;
            }
            ScanResult *sc = &scans[v];
            const char *profKey = vars[v].profileKey;
            const char *profLabel = vars[v].profileLabel[0] ? vars[v].profileLabel : NULL;
            for (int i = 0; i < sc->titleCount; i++) {
                const TitleInfo *title = &sc->titles[i];
                char gameName[META_NAME_MAX];
                char progLabel[112];
                titleDisplayName(title, gameName, sizeof(gameName));
                if (profLabel) {
                    snprintf(progLabel, sizeof(progLabel), "Processing %s [%s]", gameName, profLabel);
                } else {
                    snprintf(progLabel, sizeof(progLabel), "Processing %s", gameName);
                }
                ui_setProgress(0.15f + 0.65f * ((float)doneCount / (float)uploadTotal), progLabel);
                doneCount++;

                char fp[24];
                titleFingerprintHex(title, fp, sizeof(fp));

                if (loggedIn &&
                    manifestTitleMatches(manifest, consoleId, profKey, title->titleId, fp)) {
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
                                        profKey, profLabel, title->titleId, title->titleName,
                                        title->saveCount, title->totalSize, fp, saveMod,
                                        manifestJson, dukexPath)) {
                        uploaded++;
                        ui_setUploadStats(uploaded, grandTotalTitles);
                        ui_logf("  Uploaded %s", gameName);
                    } else {
                        ui_logf("  WARNING: upload failed for %s", gameName);
                    }
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

    /* 7. Pull down the game saves the website chose to sync back (sync-enabled,
     * from another Xbox/profile), extracting them into the active E:\UDATA\<TitleID>\.
     * The user picks which profile variation to sync on the dashboard, so we just
     * honour the manifest here. */
    if (loggedIn && manifest[0]) {
        ui_setProgress(0.90f, "Checking downloads...");
        ui_logf("Checking for saves to download...");
        CreateDirectory(UDATA_PATH, NULL);
        int pulled = 0;
        char pulledTids[64][64];
        int pulledCount = 0;
        const char *p = manifest;
        while (*p) {
            char lineKey[128];
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

            /* Key format: "console_id:profile:title_id" (profile may be empty);
             * older servers may send "console_id:title_id" or just "title_id". */
            char srcConsole[40];
            char srcProfile[40];
            char tid[64];
            srcConsole[0] = '\0';
            srcProfile[0] = '\0';
            tid[0] = '\0';
            {
                const char *c1 = strchr(lineKey, ':');
                if (c1) {
                    size_t clen = (size_t)(c1 - lineKey);
                    if (clen < sizeof(srcConsole)) {
                        memcpy(srcConsole, lineKey, clen);
                        srcConsole[clen] = '\0';
                    }
                    const char *c2 = strchr(c1 + 1, ':');
                    if (c2) {
                        size_t plen = (size_t)(c2 - (c1 + 1));
                        if (plen < sizeof(srcProfile)) {
                            memcpy(srcProfile, c1 + 1, plen);
                            srcProfile[plen] = '\0';
                        }
                        strncpy(tid, c2 + 1, sizeof(tid) - 1);
                        tid[sizeof(tid) - 1] = '\0';
                    } else {
                        strncpy(tid, c1 + 1, sizeof(tid) - 1);
                        tid[sizeof(tid) - 1] = '\0';
                    }
                } else {
                    strncpy(tid, lineKey, sizeof(tid) - 1);
                    tid[sizeof(tid) - 1] = '\0';
                }
            }
            if (!tid[0]) {
                continue;
            }
            if (srcConsole[0] && consoleId[0] && strcmp(srcConsole, consoleId) == 0) {
                continue; /* already uploaded from this console (any profile) */
            }

            /* Skip if the active UDATA already has this title. */
            BOOL localHasIt = FALSE;
            if (haveScans[0]) {
                for (int i = 0; i < scans[0].titleCount; i++) {
                    if (strcmp(scans[0].titles[i].titleId, tid) == 0) {
                        localHasIt = TRUE;
                        break;
                    }
                }
            }
            if (localHasIt) {
                continue;
            }

            /* Only restore one variation per title per run (first sync-enabled wins). */
            BOOL already = FALSE;
            for (int j = 0; j < pulledCount; j++) {
                if (strcmp(pulledTids[j], tid) == 0) {
                    already = TRUE;
                    break;
                }
            }
            if (already) {
                continue;
            }

            char dukexPath[MAX_PATH];
            char destDir[MAX_PATH];
            snprintf(dukexPath, sizeof(dukexPath), "%s\\%s.dukex", OUTPUT_DIR, tid);
            snprintf(destDir, sizeof(destDir), "%s\\%s", UDATA_PATH, tid);

            if (!downloadGameDukex(UPLOAD_HOST, UPLOAD_PORT, sessionKey,
                                   srcConsole[0] ? srcConsole : "legacy",
                                   consoleId[0] ? consoleId : "", srcProfile, tid, dukexPath)) {
                ui_logf("  WARNING: download failed for %s", tid);
                continue;
            }
            if (unzipToDir(dukexPath, destDir)) {
                pulled++;
                if (pulledCount < 64) {
                    strncpy(pulledTids[pulledCount], tid, sizeof(pulledTids[0]) - 1);
                    pulledTids[pulledCount][sizeof(pulledTids[0]) - 1] = '\0';
                    pulledCount++;
                }
                ui_logf("  Restored %s", tid);
            } else {
                ui_logf("  WARNING: could not extract %s", tid);
            }
        }
        ui_setProgress(1.0f, "Done");
        ui_logf("  Downloaded %d save(s) from server", pulled);
    }

    for (int v = 0; v < varCount; v++) {
        if (haveScans[v]) {
            freeScanResult(&scans[v]);
        }
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
