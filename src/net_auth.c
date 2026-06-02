/*
 * Insignia device-login flow for the merged SaveBackup app.
 */
#include "net_auth.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hal/debug.h>

#include "../third_party/https_client.h"
#include "../third_party/qrcodegen.h"
#include "app_ui.h"
#include "ui_font.h"

#define AUTH_SERVER_HOST "auth.insigniastats.live"
#define AUTH_SERVER_PORT_HTTPS "443"

#define JSON_BUF_SIZE 16384

static void delay_rough_ms(unsigned ms)
{
    for (unsigned j = 0; j < ms; j++) {
        for (volatile unsigned i = 0; i < 80000u; i++) {
        }
    }
}

static const char *http_body_json(const char *resp)
{
    const char *p = strstr(resp, "\r\n\r\n");
    if (!p) {
        return resp;
    }
    return p + 4;
}

static int json_extract_string(const char *json, const char *key, char *out, size_t outsz)
{
    char prefix[80];
    snprintf(prefix, sizeof(prefix), "\"%s\":\"", key);
    const char *p = strstr(json, prefix);
    if (!p) {
        return -1;
    }
    p += strlen(prefix);
    size_t i = 0;
    while (*p && *p != '"' && i < outsz - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return 0;
}

static int json_extract_int(const char *json, const char *key, int *out)
{
    char prefix[80];
    snprintf(prefix, sizeof(prefix), "\"%s\":", key);
    const char *p = strstr(json, prefix);
    if (!p) {
        return -1;
    }
    p += strlen(prefix);
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    *out = atoi(p);
    return 0;
}

static int encode_and_show_qr(const char *text)
{
    uint8_t temp[qrcodegen_BUFFER_LEN_MAX];
    uint8_t qrcode[qrcodegen_BUFFER_LEN_MAX];
    if (!qrcodegen_encodeText(text, temp, qrcode, qrcodegen_Ecc_LOW, 1, 40, qrcodegen_Mask_AUTO,
                              true)) {
        debugPrint("QR encode failed\n");
        return -1;
    }
    int size = qrcodegen_getSize(qrcode);
    ui_showQrScreen(qrcode, size);
    return 0;
}

BOOL verifySession(const char *sessionKey, char *username, size_t unSz)
{
    static char resp[JSON_BUF_SIZE];

    if (!sessionKey || !sessionKey[0]) {
        return FALSE;
    }

    char authHeader[256];
    snprintf(authHeader, sizeof(authHeader), "X-Session-Key: %s", sessionKey);
    const char *headers[] = { authHeader };

    memset(resp, 0, sizeof(resp));
    if (https_request(AUTH_SERVER_HOST, AUTH_SERVER_PORT_HTTPS, "GET", "/api/auth/user",
                      "application/json", headers, 1, NULL, 0, resp, sizeof(resp), NULL,
                      NULL) != 0) {
        return FALSE;
    }

    const char *sp = strchr(resp, ' ');
    if (!sp || sp[1] != '2') {
        return FALSE;
    }

    const char *json = http_body_json(resp);
    char un[256];
    if (json_extract_string(json, "username", un, sizeof(un)) == 0) {
        if (username && unSz) {
            strncpy(username, un, unSz - 1);
            username[unSz - 1] = '\0';
        }
        return TRUE;
    }
    return FALSE;
}

BOOL deviceLogin(char *sessionKey, size_t skSz, char *username, size_t unSz)
{
    static char resp[JSON_BUF_SIZE];

    sessionKey[0] = '\0';
    if (username && unSz) {
        username[0] = '\0';
    }

    memset(resp, 0, sizeof(resp));
    if (https_post_json(AUTH_SERVER_HOST, AUTH_SERVER_PORT_HTTPS, "/api/auth/device", "{}", resp,
                        sizeof(resp)) != 0) {
        debugPrint("Device start HTTPS failed.\n");
        return FALSE;
    }

    const char *json = http_body_json(resp);
    char device_code[128];
    char user_code[32];
    char verification_uri_complete[512];
    int interval_sec = 5;

    if (json_extract_string(json, "device_code", device_code, sizeof(device_code)) != 0 ||
        json_extract_string(json, "user_code", user_code, sizeof(user_code)) != 0) {
        debugPrint("Bad JSON from /api/auth/device.\n");
        return FALSE;
    }
    if (json_extract_string(json, "verification_uri_complete", verification_uri_complete,
                            sizeof(verification_uri_complete)) != 0) {
        debugPrint("missing verification_uri_complete\n");
        return FALSE;
    }
    json_extract_int(json, "interval", &interval_sec);
    if (interval_sec < 1) {
        interval_sec = 5;
    }

    if (encode_and_show_qr(verification_uri_complete) != 0) {
        return FALSE;
    }

    for (;;) {
        delay_rough_ms((unsigned)interval_sec * 1000u);

        char body[512];
        snprintf(body, sizeof(body),
                 "{\"grant_type\":\"urn:ietf:params:oauth:grant-type:device_code\",\"device_code\":\"%s\"}",
                 device_code);

        memset(resp, 0, sizeof(resp));
        if (https_post_json(AUTH_SERVER_HOST, AUTH_SERVER_PORT_HTTPS, "/api/auth/device/token", body,
                            resp, sizeof(resp)) != 0) {
            continue;
        }

        json = http_body_json(resp);
        char err[64];
        if (json_extract_string(json, "error", err, sizeof(err)) == 0) {
            if (strcmp(err, "authorization_pending") == 0) {
                continue;
            }
            ui_fill(UI_COL_BG);
            ui_fontPrintAt(120, 220, UI_COL_ORANGE, "Login error:");
            ui_fontPrintAt(120, 242, UI_COL_TEXT, err);
            ui_syncFb();
            if (strstr(json, "expired")) {
                return FALSE;
            }
            continue;
        }

        if (json_extract_string(json, "sessionKey", sessionKey, skSz) == 0) {
            if (username && unSz) {
                json_extract_string(json, "username", username, unSz);
            }
            ui_showLoggedIn(username);
            return TRUE;
        }

        ui_fill(UI_COL_BG);
        ui_fontPrintAt(120, 220, UI_COL_ORANGE, "Unexpected login response");
        ui_syncFb();
    }
}
