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

static void hidRemoteDrawConnectScreen(HidRemoteTransport transport, const char *line1, const char *line2);
static int hidRemoteWaitLink(HidRemoteTransport transport, unsigned long timeoutMs);
static void hidRemoteSettingsMenu();

static void hidRemotePairWaitUi(int slot) {
    tft.fillScreen(0x0841);
    hidRemoteDrawHeader(HID_REMOTE_BLE, false, ("Pair slot " + String(slot)).c_str());
    hidRemoteDrawStatus("Pair new host only", kvxConfig.hidRemoteBleName.c_str());
    hidRemoteDrawFooter("ESC cancel");
}

static void hidRemoteSwitchWaitUi(int slot, const String &name) {
    tft.fillScreen(0x0841);
    hidRemoteDrawHeader(HID_REMOTE_BLE, false, ("Slot " + String(slot)).c_str());
    hidRemoteDrawStatus("Waiting for host...", name.c_str());
    hidRemoteDrawFooter("ESC cancel");
}

static bool hidRemoteRunPairIntoSlot(int slot) {
    hidRemotePairWaitUi(slot);
    // pairIntoSlot rejects already-remembered hosts (other slots / prior bonds)
    return gHidRemoteSession.pairIntoSlot(slot, 0);
}

static void hidRemoteRestoreIdleAdvertise() {
    // Prefer open HID ADV so bonded phones can reconnect without whitelist issues
    gHidRemoteSession.advertiseOpen();
}

static void hidRemoteHostDetailMenu(int slot) {
    while (true) {
        String addr = kvxConfig.getHidRemoteHostSlot(slot);
        if (addr.isEmpty()) return;

        const String name = gHidRemoteSession.displayNameForAddr(addr);
        const String connected = gHidRemoteSession.getConnectedAddress();
        const bool isLive =
            connected.length() && connected.equalsIgnoreCase(addr) && gHidRemoteSession.isConnected();

        std::vector<Option> opts = {
            {String("Slot ") + String(slot) + (isLive ? ": connected" : ": remembered"), []() {}},
            {String("Connect / switch"),
             [=]() {
                 hidRemoteSwitchWaitUi(slot, name);
                 if (gHidRemoteSession.switchToSlot(slot, 20000)) {
                     displaySuccess("Connected:\n" + name, true);
                 } else {
                     displayWarning(
                         "Not in range / host idle.\nOpen Bluetooth on host\nor tap the keyboard.",
                         true
                     );
                 }
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
        if (sel == 4) {
            if (kvxConfig.getHidRemoteHostSlot(slot).isEmpty()) return;
        }
    }
}

static void hidRemoteConnectNewDevice() {
    int slot = kvxConfig.findEmptyHidRemoteHostSlot();
    if (slot <= 0) {
        displayWarning("All 8 host slots full.\nForget a host first.", true);
        return;
    }
    if (hidRemoteRunPairIntoSlot(slot)) {
        String name = gHidRemoteSession.getHostLabel();
        if (name.isEmpty()) {
            name = gHidRemoteSession.displayNameForAddr(gHidRemoteSession.getConnectedAddress());
        }
        displaySuccess(String("Slot ") + String(slot) + ":\n" + name, true);
    } else {
        displayWarning("Cancelled", true);
        gHidRemoteSession.syncHostSlotsWithBonds();
        hidRemoteRestoreIdleAdvertise();
    }
}

// Interactive 1-8 slot selector.
// fromSettings=false (startup): success → continue to modes; ESC → exit app
// fromSettings=true: ESC/Ok → return to settings (session stays up)
static bool hidRemoteHostSlotScreen(bool fromSettings) {
    gHidRemoteSession.syncHostSlotsWithBonds();
    hidRemoteRestoreIdleAdvertise();

    unsigned long lastAdvKick = 0;
    bool wasConnected = gHidRemoteSession.isConnected();
    String lastLiveAddr = wasConnected ? gHidRemoteSession.getConnectedAddress() : String("");
    (void)_getKeyPress(); // drain menu key

    tft.fillScreen(0x0841);
    hidRemoteDrawHostSlots(HID_REMOTE_BLE, wasConnected);
    if (fromSettings) {
        hidRemoteDrawFooter("1-8 select  Ok done  ESC back");
    }

    while (!check(EscPress)) {
        const bool linked = gHidRemoteSession.isConnected();
        const String liveAddr = linked ? gHidRemoteSession.getConnectedAddress() : String("");
        // Redraw only when connection identity changes — not on a timer (flicker).
        if (linked != wasConnected || !liveAddr.equalsIgnoreCase(lastLiveAddr)) {
            hidRemoteDrawHostSlots(HID_REMOTE_BLE, linked);
            if (fromSettings) {
                hidRemoteDrawFooter("1-8 select  Ok done  ESC back");
            }
            wasConnected = linked;
            lastLiveAddr = liveAddr;
        }

        keyStroke key = _getKeyPress();
        for (auto c : key.word) {
            if (c >= '1' && c <= '8') {
                int slot = c - '0';
                String addr = kvxConfig.getHidRemoteHostSlot(slot);
                if (addr.isEmpty()) {
                    if (hidRemoteRunPairIntoSlot(slot)) {
                        if (!fromSettings) return true;
                        String name = gHidRemoteSession.getHostLabel();
                        if (name.isEmpty()) {
                            name = gHidRemoteSession.displayNameForAddr(
                                gHidRemoteSession.getConnectedAddress()
                            );
                        }
                        displaySuccess(String("Slot ") + String(slot) + ":\n" + name, true);
                    } else {
                        hidRemoteRestoreIdleAdvertise();
                    }
                } else {
                    String name = gHidRemoteSession.displayNameForAddr(addr);
                    hidRemoteSwitchWaitUi(slot, name);
                    if (gHidRemoteSession.switchToSlot(slot, 20000)) {
                        if (!fromSettings) return true;
                        displaySuccess("Connected:\n" + name, true);
                    } else {
                        displayWarning(
                            "Not in range / host idle.\nOpen Bluetooth on host\nor tap the keyboard.",
                            true
                        );
                        gHidRemoteSession.advertiseOpen();
                    }
                }
                // Force one clean redraw after modal flows
                tft.fillScreen(0x0841);
                wasConnected = !gHidRemoteSession.isConnected();
                lastLiveAddr = "";
                break;
            }
        }

        if (check(SelPress)) {
            if (linked) {
                gHidRemoteSession.rememberConnectedHost();
                return !fromSettings;
            }
            String pref = kvxConfig.hidRemotePreferredHost;
            if (pref.length()) {
                hidRemoteDrawConnectScreen(
                    HID_REMOTE_BLE,
                    "Reconnecting...",
                    gHidRemoteSession.displayNameForAddr(pref).c_str()
                );
                gHidRemoteSession.advertiseOpen();
                int r = hidRemoteWaitLink(HID_REMOTE_BLE, 8000);
                if (r == 1) return !fromSettings;
                if (r < 0 && !fromSettings) return false;
            } else {
                int filled = 0;
                for (int s = 1; s <= KvxputerConfig::HID_REMOTE_HOST_SLOT_COUNT; s++) {
                    if (kvxConfig.getHidRemoteHostSlot(s).length()) {
                        filled = s;
                        break;
                    }
                }
                if (filled > 0) {
                    String a = kvxConfig.getHidRemoteHostSlot(filled);
                    hidRemoteSwitchWaitUi(filled, gHidRemoteSession.displayNameForAddr(a));
                    if (gHidRemoteSession.switchToSlot(filled, 8000)) return !fromSettings;
                }
            }
            tft.fillScreen(0x0841);
            wasConnected = !gHidRemoteSession.isConnected();
            lastLiveAddr = "";
            continue;
        }

        if (!linked && (millis() - lastAdvKick) > 2500) {
            // Only restart ADV if it stopped — do not tear down mid-handshake
            gHidRemoteSession.ensureAdvertising();
            lastAdvKick = millis();
        }

        delay(40);
    }
    // ESC
    return false;
}

static void hidRemoteHostsMenu() {
    if (gHidRemoteSession.transport != HID_REMOTE_BLE) {
        displayInfo("Switch transport to BLE first", true);
        return;
    }

    gHidRemoteSession.syncHostSlotsWithBonds();

    while (true) {
        const String live = gHidRemoteSession.getConnectedAddress();
        std::vector<Option> opts;

        for (int slot = 1; slot <= KvxputerConfig::HID_REMOTE_HOST_SLOT_COUNT; slot++) {
            String addr = kvxConfig.getHidRemoteHostSlot(slot);
            String label;
            if (addr.isEmpty()) {
                label = String(slot) + " — empty";
                opts.push_back(
                    {label,
                     [=]() {
                         if (hidRemoteRunPairIntoSlot(slot)) {
                             String name = gHidRemoteSession.getHostLabel();
                             if (name.isEmpty()) {
                                 name = gHidRemoteSession.displayNameForAddr(
                                     gHidRemoteSession.getConnectedAddress()
                                 );
                             }
                             displaySuccess(String("Slot ") + String(slot) + ":\n" + name, true);
                         } else {
                             displayWarning("Cancelled", true);
                             hidRemoteRestoreIdleAdvertise();
                         }
                     }}
                );
            } else {
                label = String(slot) + " " + gHidRemoteSession.displayNameForAddr(addr);
                if (live.length() && live.equalsIgnoreCase(addr) && gHidRemoteSession.isConnected()) {
                    label = "* " + label;
                }
                opts.push_back({label, [=]() { hidRemoteHostDetailMenu(slot); }});
            }
        }

        opts.push_back({"Connect to new device", []() { hidRemoteConnectNewDevice(); }});
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
            {"Host slots...",
             []() {
                 if (gHidRemoteSession.transport != HID_REMOTE_BLE) {
                     displayInfo("Switch transport to BLE first", true);
                     return;
                 }
                 (void)hidRemoteHostSlotScreen(true);
             }},
            {"Connect to new device",
             []() {
                 if (gHidRemoteSession.transport != HID_REMOTE_BLE) {
                     displayInfo("Switch transport to BLE first", true);
                     return;
                 }
                 hidRemoteConnectNewDevice();
             }},
            {"BLE Hosts...", hidRemoteHostsMenu},
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
                     displayInfo("Forgot all pairings.\nSlots cleared.", true);
                 } else {
                     displayError(
                         String("Forget incomplete.\nStill bonded: ") + String(left), true
                     );
                 }
             }},
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
    if (transport == HID_REMOTE_USB) {
        hidRemoteDrawConnectScreen(transport, nullptr, nullptr);
        if (!gHidRemoteSession.begin(
                transport, static_cast<HidRemoteCapability>(HID_CAP_KEYBOARD | HID_CAP_MEDIA | HID_CAP_MOUSE)
            )) {
            displayError("HID init failed", true);
            return false;
        }
        int r = hidRemoteWaitLink(transport, 0);
        if (r == 1) return true;
        gHidRemoteSession.end();
        return false;
    }

    // BLE: bring stack up, then host-slot UI
    tft.fillScreen(0x0841);
    hidRemoteDrawHeader(HID_REMOTE_BLE, false, "Starting");
    hidRemoteDrawStatus("Starting BLE...", nullptr);
    hidRemoteDrawFooter("ESC cancel");

    if (!gHidRemoteSession.begin(
            transport, static_cast<HidRemoteCapability>(HID_CAP_KEYBOARD | HID_CAP_MEDIA | HID_CAP_MOUSE)
        )) {
        displayError("HID init failed", true);
        return false;
    }

    if (!hidRemoteHostSlotScreen(false)) {
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
