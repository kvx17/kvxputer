#include "hid_remote.h"
#include "hid_remote_modes.h"
#include "hid_remote_transport.h"
#include "hid_remote_ui.h"
#include "root/ui/display.h"
#include "root/app/utils.h"
#include "root/config/config.h"
#include "root/input/mykeyboard.h"
#include <globals.h>

static HidRemoteTransport resolveTransport(HidRemoteLaunch launch) {
    if (launch == HID_REMOTE_LAUNCH_USB) return HID_REMOTE_USB;
    if (launch == HID_REMOTE_LAUNCH_BLE) return HID_REMOTE_BLE;
    return kvxConfig.hidRemoteTransport ? HID_REMOTE_BLE : HID_REMOTE_USB;
}

static void hidRemoteSettingsMenu() {
    while (true) {
        std::vector<Option> opts = {
            {String("Transport: ") + (kvxConfig.hidRemoteTransport ? "BLE" : "USB"),
             []() { kvxConfig.setHidRemoteTransport(kvxConfig.hidRemoteTransport ? 0 : 1); }},
            {"BLE Name: " + kvxConfig.hidRemoteBleName,
             []() {
                 String n = keyboard(kvxConfig.hidRemoteBleName, 20, "BLE HID name");
                 if (n.length() > 0 && n != "\x1B") kvxConfig.setHidRemoteBleName(n);
             }},
            {"Mouse sensitivity: " + String(kvxConfig.hidRemoteMouseSensitivity),
             []() {
                 kvxConfig.setHidRemoteMouseSensitivity(kvxConfig.hidRemoteMouseSensitivity % 10 + 1);
             }},
            {"Jiggler interval: " + String(kvxConfig.hidRemoteJigglerInterval) + "s",
             []() {
                 kvxConfig.setHidRemoteJigglerInterval(kvxConfig.hidRemoteJigglerInterval + 5);
             }},
            {"Stealth min: " + String(kvxConfig.hidRemoteStealthMin) + "s",
             []() { kvxConfig.setHidRemoteStealthMin(kvxConfig.hidRemoteStealthMin + 5); }},
            {"Stealth max: " + String(kvxConfig.hidRemoteStealthMax) + "s",
             []() { kvxConfig.setHidRemoteStealthMax(kvxConfig.hidRemoteStealthMax + 10); }},
            {"Clicker delay: " + String(kvxConfig.hidRemoteClickerDelay) + "ms",
             []() { kvxConfig.setHidRemoteClickerDelay(kvxConfig.hidRemoteClickerDelay + 50); }},
            {"Clicker button: " + String(kvxConfig.hidRemoteClickerButton),
             []() { kvxConfig.setHidRemoteClickerButton((kvxConfig.hidRemoteClickerButton + 1) % 3); }},
            {"PTT preset: " + String(kvxConfig.hidRemotePttPreset),
             []() { kvxConfig.setHidRemotePttPreset((kvxConfig.hidRemotePttPreset + 1) % 5); }},
            {"Back", []() {}},
        };
        int sel = loopOptions(opts, MENU_TYPE_SUBMENU, "HID Settings");
        if (sel < 0 || sel == (int)opts.size() - 1) return;
    }
}

static bool hidRemoteConnect(HidRemoteTransport transport) {
    tft.fillScreen(0x0841);
    hidRemoteDrawHeader(transport, false, "Connecting");
    if (transport == HID_REMOTE_USB) {
        hidRemoteDrawStatus("Plug in USB cable", "Waiting for host...");
    } else {
        hidRemoteDrawStatus("Pair from host", kvxConfig.hidRemoteBleName.c_str());
    }
    hidRemoteDrawFooter("ESC cancel");

    if (!gHidRemoteSession.begin(transport, static_cast<HidRemoteCapability>(HID_CAP_KEYBOARD | HID_CAP_MEDIA | HID_CAP_MOUSE))) {
        displayError("HID init failed", true);
        return false;
    }
    if (!gHidRemoteSession.waitConnected()) {
        gHidRemoteSession.end();
        return false;
    }
    return true;
}

static int hidRemoteModePicker(int startIndex) {
    std::vector<Option> opts;
    for (int i = 0; i < HID_MODE_COUNT; i++) {
        opts.push_back({hidRemoteModeInfo(static_cast<HidRemoteMode>(i)).label, []() {}});
    }
    opts.push_back({"Settings", hidRemoteSettingsMenu});
    opts.push_back({"Exit", []() {}});
    if (startIndex < 0 || startIndex >= HID_MODE_COUNT) startIndex = 0;
    int sel = loopOptions(opts, MENU_TYPE_SUBMENU, "HID Remote", startIndex, false);
    if (sel < 0 || sel >= HID_MODE_COUNT) return -1;
    return sel;
}

void hidRemoteMenu(HidRemoteLaunch launch) {
    HidRemoteTransport transport = resolveTransport(launch);

    if (!hidRemoteConnect(transport)) {
        returnToMenu = true;
        return;
    }

    bool running = true;
    int modeIndex = kvxConfig.hidRemoteLastMode;
    if (modeIndex < 0 || modeIndex >= HID_MODE_COUNT) modeIndex = 0;

    while (running) {
        modeIndex = hidRemoteModePicker(modeIndex);
        if (modeIndex < 0) break;

        kvxConfig.setHidRemoteLastMode(modeIndex);
        HidRemoteMode mode = static_cast<HidRemoteMode>(modeIndex);

        hidRemoteRunMode(mode, gHidRemoteSession);
    }

    gHidRemoteSession.end();
    returnToMenu = true;
}
