#include "usb_menu.h"
#include "root/ui/display.h"
#include "root/app/utils.h"
#include "root/storage/massStorage.h"
#include "menu/others/badusb_ble/ducky_typer.h"
#include "menu/others/clicker.h"
#include "menu/others/u2f.h"
#include "menu/ble/hid_remote/hid_remote.h"

void UsbMenu::optionsMenu() {
    options = {
#if !defined(LITE_VERSION)
        {"kvxkeyboard HID",       [=]() { hidRemoteMenu(HID_REMOTE_LAUNCH_USB); }},
        {"BadUSB",                [=]() { ducky_setup(hid_usb, false); }        },
        {"USB Keyboard (legacy)", [=]() { ducky_keyboard(hid_usb, false); }     },
#ifdef USB_as_HID
        {"USB Clicker (legacy)",  clicker_setup                                 },
        {"USB U2F",               u2f_setup                                     },
#endif
#endif
#if defined(SOC_USB_OTG_SUPPORTED)
        {"Mass Storage",          [=]() { MassStorage(); }                      },
#endif
    };
    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_SUBMENU, "USB");
}

void UsbMenu::drawIcon(float scale) {
    clearIconArea();

    int t = (int)(scale * 3);
    if (t < 2) t = 2;
    int cx = iconCenterX;
    int cy = iconCenterY + (int)(scale * 2);
    int stemH = (int)(scale * 32);
    if (stemH % 2) stemH++;
    int arm = (int)(scale * 18);
    int r = (int)(scale * 5);
    if (r < 3) r = 3;
    int sq = (int)(scale * 8);
    if (sq < 4) sq = 4;
    int tri = (int)(scale * 7);
    if (tri < 4) tri = 4;

    int stemTop = cy - stemH / 2;
    int stemBot = cy + stemH / 2;
    int armY = cy - (int)(scale * 2);
    uint16_t color = kvxConfig.priColor;

    tft.fillRect(cx - t / 2, stemTop, t, stemH, color);
    tft.fillRect(cx - sq / 2, stemTop - sq + t, sq, sq, color);

    tft.fillRect(cx - arm, armY - t / 2, arm, t, color);
    tft.fillCircle(cx - arm, armY, r, color);

    tft.fillRect(cx, armY - t / 2, arm, t, color);
    tft.fillTriangle(cx + arm, armY - tri, cx + arm, armY + tri, cx + arm + tri + 2, armY, color);

    int plugW = (int)(scale * 14);
    int plugH = (int)(scale * 6);
    tft.fillRect(cx - plugW / 2, stemBot, plugW, plugH, color);
}
