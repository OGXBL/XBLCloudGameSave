#include "app_input.h"

#include <hal/xbox.h>
#include <usbh_lib.h>
#include <xid_driver.h>

#include "app_ui.h"

#include <hal/debug.h>
#include <windows.h>

static BOOL g_inputReady = FALSE;
static volatile uint16_t g_lastButtons = 0;

static void xid_read_done(UTR_T *utr)
{
    xid_dev_t *xid = (xid_dev_t *)utr->context;
    if (utr->status < 0 || !xid || !utr->buff) {
        return;
    }
    if (utr->xfer_len >= 4) {
        xid_gamepad_in *pad = (xid_gamepad_in *)utr->buff;
        g_lastButtons = pad->dButtons;
    }
    utr->xfer_len = 0;
    utr->bIsTransferDone = 0;
    usbh_int_xfer(utr);
}

static void xid_connected(xid_dev_t *xid, int status)
{
    (void)status;
    if (!xid || xid->xid_desc.bType != XID_TYPE_GAMECONTROLLER) {
        return;
    }
    xid->user_data = xid;
    usbh_xid_read(xid, 0, xid_read_done);
}

static void xid_disconnected(xid_dev_t *xid, int status)
{
    (void)status;
    (void)xid;
    g_lastButtons = 0;
}

void inputInit(void)
{
    if (g_inputReady) {
        return;
    }
    usbh_core_init();
    usbh_xid_init();
    usbh_install_xid_conn_callback(xid_connected, xid_disconnected);

    xid_dev_t *xid = usbh_xid_get_device_list();
    for (; xid; xid = xid->next) {
        if (xid->xid_desc.bType == XID_TYPE_GAMECONTROLLER) {
            xid_connected(xid, 0);
            break;
        }
    }
    g_inputReady = TRUE;
}

BOOL inputAnyExitPressed(void)
{
    if (!g_inputReady) {
        return FALSE;
    }
    usbh_pooling_hubs();
    uint16_t b = g_lastButtons;
    return (b & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_BACK)) != 0;
}

void inputWaitExitToDashboard(const char *summary1, const char *summary2)
{
    inputInit();
    ui_showComplete(summary1, summary2);

    for (;;) {
        if (inputAnyExitPressed()) {
            break;
        }
        Sleep(50);
    }

    debugPrint("\nReturning to dashboard...\n");
    XLaunchXBEEx(NULL, NULL);
}
