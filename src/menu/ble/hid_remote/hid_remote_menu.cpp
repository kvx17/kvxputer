#include "hid_remote.h"
#include "hid_remote_modes.h"
#include "hid_remote_transport.h"
#include "hid_remote_ui.h"
#include "root/ui/display.h"
#include "root/app/utils.h"
#include "root/config/config.h"
#include "root/input/mykeyboard.h"
#include <globals.h>
#if defined(CONFIG_BT_ENABLED)
#include <NimBLEDevice.h>
#endif

static HidRemoteTransport resolveTransport(HidRemoteLaunch launch) {
    if (launch == HID_REMOTE_LAUNCH_USB) return HID_REMOTE_USB;
    if (launch == HID_REMOTE_LAUNCH_BLE) return HID_REMOTE_BLE;
    return kvxConfig.hidRemoteTransport ? HID_REMOTE_BLE : HID_REMOTE_USB;
}

static void hidRemoteHostDetailMenu(const String &addr) {
    while (true) {
        const String name = gHidRemoteSession.displayNameForAddr(addr);
        const String connected = gHidRemoteSession.getConnectedAddress();
        const bool isLive = connected.length() && connected.equalsIgnoreCase(addr) && gHidRemoteSession.isConnected();
        const bool isPref = kvxConfig.hidRemotePreferredHost.equalsIgnoreCase(addr);

        std::vector<Option> opts = {
            {String(isLive ? "Status: connected" : "Status: remembered"), []() {}},
            {String("Connect / switch"),
             [=]() {
                 tft.fillScreen(0x0841);
                 hidRemoteDrawHeader(HID_REMOTE_BLE, false, "Switch host");
                 hidRemoteDrawStatus("Waiting for host...", name.c_str());
                 hidRemoteDrawFooter("ESC cancel");
                 if (gHidRemoteSession.switchToHost(addr, 20000)) {
                     displaySuccess("Connected:\n" + name, true);
                 } else {
                     displayWarning(
                         "Not in range / host idle.\nOpen Bluetooth on host\nor tap the keyboard.",
                         true
                     );
                 }
             }},
            {String(isPref ? "Preferred: yes" : "Set as preferred"),
             [=]() {
                 kvxConfig.setHidRemotePreferredHost(addr);
                 displayInfo("Preferred host set", true);
             }},
            {"Rename",
             [=]() {
                 String n = keyboard(name, 24, "Host name");
                 if (n.length() > 0 && n != "\x1B") kvxConfig.setHidRemoteHostAlias(addr, n);
             }},
            {"Disconnect",
             [=]() {
                 if (!isLive) {
                     displayInfo("Not connected to this host", true);
                     return;
                 }
                 if (gHidRemoteSession.disconnectHost()) displayInfo("Disconnected", true);
                 else displayError("Disconnect failed", true);
             }},
            {"Forget this host",
             [=]() {
                 drawMainBorder(true);
                 int8_t choice =
                     displayMessage(("Forget " + name + "?").c_str(), "No", nullptr, "Yes", TFT_WHITE);
                 if (choice != 1) return;
                 if (gHidRemoteSession.forgetBond(addr)) displayInfo("Forgot host", true);
                 else displayError("Forget failed", true);
             }},
            {"Back", []() {}},
        };

        int sel = loopOptions(opts, MENU_TYPE_SUBMENU, name.c_str());
        if (sel < 0 || sel == (int)opts.size() - 1) return;
        // Leaving after forget if bond gone
        if (sel == 5) {
            bool still = false;
            for (int i = 0; i < gHidRemoteSession.getBondCount(); i++) {
                if (gHidRemoteSession.getBondLabel(i).equalsIgnoreCase(addr)) {
                    still = true;
                    break;
                }
            }
            if (!still) return;
        }
    }
}

static void hidRemoteHostsMenu() {
    if (gHidRemoteSession.transport != HID_REMOTE_BLE) {
        displayInfo("Switch transport to BLE first", true);
        return;
    }

    while (true) {
        const int n = gHidRemoteSession.getBondCount();
        const String live = gHidRemoteSession.getConnectedAddress();
        std::vector<Option> opts;

        if (n == 0) {
            opts.push_back({"No remembered hosts", []() {}});
        } else {
            for (int i = 0; i < n; i++) {
                String addr = gHidRemoteSession.getBondLabel(i);
                String label = gHidRemoteSession.displayNameForAddr(addr);
                if (live.length() && live.equalsIgnoreCase(addr) && gHidRemoteSession.isConnected()) {
                    label = "* " + label;
                } else if (kvxConfig.hidRemotePreferredHost.equalsIgnoreCase(addr)) {
                    label = "> " + label;
                }
                opts.push_back({label, [=]() { hidRemoteHostDetailMenu(addr); }});
            }
        }

        opts.push_back(
            {"Disconnect current",
             []() {
                 if (!gHidRemoteSession.isConnected()) {
                     displayInfo("Not connected", true);
                     return;
                 }
                 if (gHidRemoteSession.disconnectHost()) displayInfo("Disconnected", true);
                 else displayError("Disconnect failed", true);
             }}
        );
        opts.push_back(
            {"Accept any bonded",
             []() {
                 kvxConfig.setHidRemotePreferredHost("");
                 gHidRemoteSession.disconnectHost();
                 if (gHidRemoteSession.advertiseForAnyBonded()) {
                     displayInfo("Advertising for any\nremembered host", true);
                 } else {
                     displayError("Advertise failed", true);
                 }
             }}
        );
        opts.push_back(
            {"Connect to new device",
             []() {
                 // Disconnect current (if any), stay discoverable until a host pairs
                 tft.fillScreen(0x0841);
                 hidRemoteDrawHeader(HID_REMOTE_BLE, false, "New device");
                 hidRemoteDrawStatus("Pair from host", kvxConfig.hidRemoteBleName.c_str());
                 hidRemoteDrawFooter("ESC cancel");
                 if (gHidRemoteSession.reconnectNewHost()) {
                     String name = gHidRemoteSession.getHostLabel();
                     if (name.isEmpty()) {
                         name = gHidRemoteSession.displayNameForAddr(
                             gHidRemoteSession.getConnectedAddress()
                         );
                     }
                     displaySuccess(String("Connected:\n") + name, true);
                 } else {
                     displayWarning("Cancelled", true);
                     if (gHidRemoteSession.getBondCount() > 0) {
                         gHidRemoteSession.advertiseForAnyBonded();
                     } else {
                         gHidRemoteSession.advertiseOpen();
                     }
                 }
             }}
        );
        opts.push_back({"Back", []() {}});

        int sel = loopOptions(opts, MENU_TYPE_SUBMENU, "BLE Hosts");
        if (sel < 0 || sel == (int)opts.size() - 1) return;
    }
}

static void hidRemoteSettingsMenu() {
    while (true) {
        const bool live = gHidRemoteSession.isConnected();
        const String liveName = live ? gHidRemoteSession.getHostLabel() : String("");
        std::vector<Option> opts = {
            {String("Status: ") + (live ? ("Connected " + liveName) : "Not connected"), []() {}},
            {"Disconnect",
             []() {
                 if (gHidRemoteSession.transport != HID_REMOTE_BLE) {
                     displayInfo("BLE only", true);
                     return;
                 }
                 if (!gHidRemoteSession.isConnected()) {
                     displayInfo("Not connected", true);
                     return;
                 }
                 if (gHidRemoteSession.disconnectHost()) displayInfo("Disconnected", true);
                 else displayError("Disconnect failed", true);
             }},
            {"Connect to new device",
             []() {
                 if (gHidRemoteSession.transport != HID_REMOTE_BLE) {
                     displayInfo("Switch transport to BLE first", true);
                     return;
                 }
                 tft.fillScreen(0x0841);
                 hidRemoteDrawHeader(HID_REMOTE_BLE, false, "New device");
                 hidRemoteDrawStatus("Pair from host", kvxConfig.hidRemoteBleName.c_str());
                 hidRemoteDrawFooter("ESC cancel");
                 if (gHidRemoteSession.reconnectNewHost()) {
                     String name = gHidRemoteSession.getHostLabel();
                     if (name.isEmpty()) {
                         name = gHidRemoteSession.displayNameForAddr(
                             gHidRemoteSession.getConnectedAddress()
                         );
                     }
                     displaySuccess(String("Connected:\n") + name, true);
                 } else {
                     displayWarning("Cancelled", true);
                     if (gHidRemoteSession.getBondCount() > 0) {
                         gHidRemoteSession.advertiseForAnyBonded();
                     } else {
                         gHidRemoteSession.advertiseOpen();
                     }
                 }
             }},
            {"BLE Hosts...", hidRemoteHostsMenu},
            {String("Transport: ") + (kvxConfig.hidRemoteTransport ? "BLE" : "USB"),
             []() { kvxConfig.setHidRemoteTransport(kvxConfig.hidRemoteTransport ? 0 : 1); }},
            {"BLE Name: " + kvxConfig.hidRemoteBleName,
             []() {
                 String n = keyboard(kvxConfig.hidRemoteBleName, 20, "BLE HID name");
                 if (n.length() > 0 && n != "\x1B") kvxConfig.setHidRemoteBleName(n);
             }},
            {"Host name: " + (kvxConfig.hidRemoteHostName.length() ? kvxConfig.hidRemoteHostName : "(auto)"),
             []() {
                 String n = keyboard(kvxConfig.hidRemoteHostName, 32, "Host name");
                 if (n == "\x1B") return;
                 kvxConfig.setHidRemoteHostName(n);
                 gHidRemoteSession.refreshHostLabel();
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
            {"Forget all BLE pairings",
             []() {
                 if (gHidRemoteSession.transport != HID_REMOTE_BLE) {
                     displayInfo("Switch transport to BLE first", true);
                     return;
                 }
                 drawMainBorder(true);
                 int8_t choice =
                     displayMessage("Forget ALL hosts?", "No", nullptr, "Yes", TFT_WHITE);
                 if (choice != 1) return;
                 bool ok = gHidRemoteSession.forgetBonds();
                 int left = gHidRemoteSession.getBondCount();
                 if (ok && left == 0) {
                     displayInfo("Forgot all pairings.\nDevice is discoverable.", true);
                 } else {
                     displayError(
                         String("Forget incomplete.\nStill bonded: ") + String(left), true
                     );
                 }
             }},
            {"Back", []() {}},
        };
        int sel = loopOptions(opts, MENU_TYPE_SUBMENU, "HID Settings");
        if (sel < 0 || sel == (int)opts.size() - 1) return;
    }
}

static void hidRemoteDrawConnectScreen(
    HidRemoteTransport transport, const char *line1, const char *line2
) {
    const bool linked = gHidRemoteSession.isConnected();
    tft.fillScreen(0x0841);
    hidRemoteDrawHeader(transport, linked, linked ? nullptr : "Connecting");
    if (line1 == nullptr) {
        if (transport == HID_REMOTE_USB) {
            line1 = linked ? "Connected" : "Plug in USB cable";
            line2 = linked ? nullptr : "Waiting for host...";
        } else {
            line1 = linked ? "Connected" : "Pair from host";
            line2 = kvxConfig.hidRemoteBleName.c_str();
        }
    }
    hidRemoteDrawStatus(line1, line2);
    hidRemoteDrawFooter("Ok settings  ESC cancel");
}

// 1 = connected, 0 = timeout, -1 = cancelled
static int hidRemoteWaitLink(HidRemoteTransport transport, unsigned long timeoutMs) {
    unsigned long start = millis();
    unsigned long lastAdvKick = 0;

    while (!check(EscPress)) {
        if (check(SelPress)) {
            hidRemoteSettingsMenu();
            hidRemoteDrawConnectScreen(transport, nullptr, nullptr);
            start = millis(); // reset timeout after settings
            continue;
        }
        if (gHidRemoteSession.isConnected()) return 1;

        if (transport == HID_REMOTE_BLE && (millis() - lastAdvKick) > 2000) {
            gHidRemoteSession.ensureAdvertising();
            lastAdvKick = millis();
        }

        if (timeoutMs > 0 && (millis() - start) >= timeoutMs) return 0;
        delay(40);
    }
    return -1;
}

static bool hidRemoteConnect(HidRemoteTransport transport) {
    hidRemoteDrawConnectScreen(transport, nullptr, nullptr);

    if (!gHidRemoteSession.begin(
            transport, static_cast<HidRemoteCapability>(HID_CAP_KEYBOARD | HID_CAP_MEDIA | HID_CAP_MOUSE)
        )) {
        displayError("HID init failed", true);
        return false;
    }

    if (transport == HID_REMOTE_USB) {
        int r = hidRemoteWaitLink(transport, 0);
        if (r == 1) return true;
        gHidRemoteSession.end();
        return false;
    }

    // BLE: try preferred / bonded hosts first, then offer to add a new pair
    const int bonds = gHidRemoteSession.getBondCount();
    if (bonds > 0) {
        String bondHint;
        if (kvxConfig.hidRemotePreferredHost.length() > 0) {
            bondHint = gHidRemoteSession.displayNameForAddr(kvxConfig.hidRemotePreferredHost);
            gHidRemoteSession.advertiseForHost(kvxConfig.hidRemotePreferredHost, true);
        } else {
            bondHint = gHidRemoteSession.displayNameForAddr(gHidRemoteSession.getBondLabel(0));
            if (bonds > 1) bondHint = String(bonds) + " bonded hosts";
            gHidRemoteSession.advertiseForAnyBonded();
        }

        hidRemoteDrawConnectScreen(transport, "Reconnecting...", bondHint.c_str());

        int r = hidRemoteWaitLink(transport, 15000);
        if (r == 1) {
            String addr = gHidRemoteSession.getConnectedAddress();
            if (addr.length() && kvxConfig.hidRemotePreferredHost.isEmpty()) {
                kvxConfig.setHidRemotePreferredHost(addr);
            }
            return true;
        }
        if (r < 0) {
            gHidRemoteSession.end();
            return false;
        }

        drawMainBorder(true);
        int8_t choice = displayMessage(
            "No host connected.\nOpen BLE Hosts?", "Wait", nullptr, "Hosts", TFT_WHITE
        );
        if (choice == 1) {
            hidRemoteHostsMenu();
            if (gHidRemoteSession.isConnected()) return true;
        }

        // Keep waiting for bonded hosts (or after hosts menu)
        hidRemoteDrawConnectScreen(transport, "Waiting for host...", bondHint.c_str());
        gHidRemoteSession.advertiseForAnyBonded();
        r = hidRemoteWaitLink(transport, 0);
        if (r == 1) return true;
        gHidRemoteSession.end();
        return false;
    }

    hidRemoteDrawConnectScreen(transport, "Pair from host", kvxConfig.hidRemoteBleName.c_str());
    gHidRemoteSession.ensureAdvertising();
    int r = hidRemoteWaitLink(transport, 0);
    if (r == 1) return true;
    gHidRemoteSession.end();
    return false;
}

static int hidRemoteModePicker(int startIndex) {
    std::vector<Option> opts;
    for (int i = 0; i < HID_MODE_COUNT; i++) {
        opts.push_back({hidRemoteModeInfo(static_cast<HidRemoteMode>(i)).label, []() {}});
    }
    opts.push_back({"Settings", hidRemoteSettingsMenu});
    opts.push_back({"Exit", []() {}});
    if (startIndex < 0 || startIndex >= HID_MODE_COUNT) startIndex = 0;
    int sel = loopOptions(opts, MENU_TYPE_SUBMENU, KVXKEYBOARD_HID_NAME, startIndex, false);
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
