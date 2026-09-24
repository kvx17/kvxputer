#include "hid_remote.h"
#include "hid_remote_modes.h"
#include "hid_remote_transport.h"
#include "hid_remote_ui.h"
#include "root/ui/display.h"
#include "root/app/utils.h"
#include "root/config/config.h"
#include "root/input/mykeyboard.h"
#include <globals.h>
#include <interface.h>
#if defined(HAS_KEYBOARD)
#include <Keyboard.h>
extern Keyboard_Class Keyboard;
#endif
#if defined(CONFIG_BT_ENABLED)
#include <NimBLEDevice.h>
#endif

// Hold a slot key this long to open that host's options instead of connecting.
static const unsigned long HID_SLOT_HOLD_MS = 2000;

static HidRemoteTransport resolveTransport(HidRemoteLaunch launch) {
    if (launch == HID_REMOTE_LAUNCH_USB) return HID_REMOTE_USB;
    if (launch == HID_REMOTE_LAUNCH_BLE) return HID_REMOTE_BLE;
    return kvxConfig.hidRemoteTransport ? HID_REMOTE_BLE : HID_REMOTE_USB;
}

static void hidRemoteDrawConnectScreen(HidRemoteTransport transport, const char *line1, const char *line2);
static int hidRemoteWaitLink(HidRemoteTransport transport, unsigned long timeoutMs);
static void hidRemoteSettingsMenu();
static bool hidRemoteHostListScreen(bool fromSettings);

static void hidRemoteClearMenuKeys() {
    EscPress = false;
    SelPress = false;
    AnyKeyPress = false;
    (void)_getKeyPress();
}

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
    hidRemoteClearMenuKeys();
    hidRemotePairWaitUi(slot);
    hidRemoteLedSet(HID_REMOTE_LED_PAIRING);
    return gHidRemoteSession.pairIntoSlot(slot, 0);
}

static void hidRemoteIdleAdvertiseStop() { gHidRemoteSession.advertiseStop(); }

// True when the slot key is still held after HID_SLOT_HOLD_MS (options).
// False on early release (tap → connect / pair).
static bool hidRemoteSlotKeyHeld(char key) {
#if defined(HAS_KEYBOARD)
    const unsigned long start = millis();
    bool hinted = false;

    // Let InputHandler run so TCA8418 press/release can update held state.
    while (true) {
        if (!isCardputerKeyHeld(key)) return false;
        if (millis() - start >= HID_SLOT_HOLD_MS) return true;
        if (!hinted && (millis() - start) > 350) {
            hidRemoteDrawFooter("Keep holding for host options");
            hinted = true;
        }
        hidRemoteLedTick();
        delay(20);
    }
#else
    (void)key;
    return false;
#endif
}

static void hidRemoteHostDetailMenu(int slot) {
    while (true) {
        String addr = kvxConfig.getHidRemoteHostSlot(slot);
        if (addr.isEmpty()) return;

        const String name = gHidRemoteSession.displayNameForAddr(addr);
        const String connected = gHidRemoteSession.getConnectedAddress();
        const bool isLive =
            connected.length() && hidRemoteAddrEqual(connected, addr) && gHidRemoteSession.isConnected();

        std::vector<Option> opts = {
            {String("Slot ") + String(slot) + (isLive ? ": connected" : ": remembered"), []() {}},
            {String("Connect / switch"),
             [=]() {
                 hidRemoteSwitchWaitUi(slot, name);
                 hidRemoteLedSet(HID_REMOTE_LED_CONNECTING);
                 if (gHidRemoteSession.switchToSlot(slot, 30000)) {
                     hidRemoteLedSet(HID_REMOTE_LED_CONNECTED);
                     displaySuccess("Connected:\n" + name, true);
                 } else {
                     hidRemoteLedSet(HID_REMOTE_LED_DISCONNECTED);
                     displayWarning(
                         "Wrong phone raced in / host idle.\nOpen BT and tap keyboard.",
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
                 if (gHidRemoteSession.disconnectHost()) {
                     hidRemoteIdleAdvertiseStop();
                     hidRemoteLedSet(HID_REMOTE_LED_DISCONNECTED);
                     displayInfo("Disconnected", true);
                 } else displayError("Disconnect failed", true);
             }},
            {"Forget this host",
             [=]() {
                 drawMainBorder(true);
                 String prompt = "Forget " + name + "?\nOk=Yes  . =No";
                 int8_t choice = displayMessage(prompt.c_str(), "Yes", nullptr, "No", TFT_WHITE);
                 if (choice != 0) return;
                 bool ok = gHidRemoteSession.forgetBond(addr);
                 bool slotGone = kvxConfig.getHidRemoteHostSlot(slot).isEmpty();
                 if (ok || slotGone) {
                     hidRemoteLedSet(HID_REMOTE_LED_FORGET_OK);
                     displayInfo(ok ? "Forgot host" : "Host removed from slots", true);
                 } else {
                     displayError("Forget failed.\nBond still stored.", true);
                 }
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
        displayWarning("All 6 host slots full.\nForget a host first.", true);
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
        hidRemoteIdleAdvertiseStop();
    }
}

// Interactive 1-6 slot selector.
// fromSettings=false (startup): success → continue to modes; ESC → exit app
// fromSettings=true: ESC/Ok → return to settings (session stays up)
static bool hidRemoteHostSlotScreen(bool fromSettings) {
#if !defined(HAS_KEYBOARD)
    return hidRemoteHostListScreen(fromSettings);
#else
    gHidRemoteSession.syncHostSlotsWithBonds();
    hidRemoteIdleAdvertiseStop();

    bool wasConnected = gHidRemoteSession.isConnected();
    String lastLiveAddr = wasConnected ? gHidRemoteSession.getConnectedAddress() : String("");
    hidRemoteClearMenuKeys();

    const char *footer = "1-6 connect  hold=opts  S  U=USB  ESC";

    tft.fillScreen(0x0841);
    hidRemoteDrawHostSlots(HID_REMOTE_BLE, wasConnected);
    hidRemoteDrawFooter(footer);
    if (wasConnected) hidRemoteLedSet(HID_REMOTE_LED_CONNECTED);
    else hidRemoteLedSet(HID_REMOTE_LED_OFF);

    auto redrawSlots = [&]() {
        tft.fillScreen(0x0841);
        const bool linkedNow = gHidRemoteSession.isConnected();
        hidRemoteDrawHostSlots(HID_REMOTE_BLE, linkedNow);
        hidRemoteDrawFooter(footer);
        wasConnected = linkedNow;
        lastLiveAddr = "";
    };

    // Slot the user last asked for. Once set, no other host may hold the link —
    // a bonded phone that reconnects on its own gets dropped here.
    int selectedSlot = 0;

    while (!check(EscPress) && !forceHome) {
        hidRemoteLedTick();
        if (selectedSlot > 0 && gHidRemoteSession.isConnected()) {
            String want = kvxConfig.getHidRemoteHostSlot(selectedSlot);
            if (want.length() && !gHidRemoteSession.isConnectedToAddr(want)) {
                gHidRemoteSession.disconnectHost(false);
                gHidRemoteSession.advertiseStop();
                hidRemoteLedSet(HID_REMOTE_LED_REJECT);
                redrawSlots();
            }
        }
        const bool linked = gHidRemoteSession.isConnected();
        const String liveAddr = linked ? gHidRemoteSession.getConnectedAddress() : String("");
        if (linked != wasConnected || liveAddr != lastLiveAddr) {
            // liveAddr may include type suffix; slot matching uses hidRemoteAddrEqual elsewhere.
            hidRemoteDrawHostSlots(HID_REMOTE_BLE, linked);
            hidRemoteDrawFooter(footer);
            if (linked) hidRemoteLedSet(HID_REMOTE_LED_CONNECTED);
            else if (wasConnected) hidRemoteLedSet(HID_REMOTE_LED_DISCONNECTED);
            wasConnected = linked;
            lastLiveAddr = liveAddr;
        }

        keyStroke key = _getKeyPress();
        for (auto c : key.word) {
            if (c == 's' || c == 'S') {
                hidRemoteSettingsMenu();
                redrawSlots();
                break;
            }
            if (c == 'u' || c == 'U') {
#if defined(USB_as_HID)
                gHidRemoteSession.end();
                kvxConfig.setHidRemoteTransport(0);
                hidRemoteDrawConnectScreen(HID_REMOTE_USB, nullptr, nullptr);
                if (!gHidRemoteSession.begin(
                        HID_REMOTE_USB,
                        static_cast<HidRemoteCapability>(
                            HID_CAP_KEYBOARD | HID_CAP_MEDIA | HID_CAP_MOUSE
                        )
                    )) {
                    displayError("USB HID init failed", true);
                    hidRemoteLedSet(HID_REMOTE_LED_ERROR);
                    // Restart BLE so the host screen remains usable.
                    (void)gHidRemoteSession.begin(
                        HID_REMOTE_BLE,
                        static_cast<HidRemoteCapability>(
                            HID_CAP_KEYBOARD | HID_CAP_MEDIA | HID_CAP_MOUSE
                        )
                    );
                    redrawSlots();
                    break;
                }
                int r = hidRemoteWaitLink(HID_REMOTE_USB, 0);
                if (r == 1) {
                    hidRemoteLedSet(HID_REMOTE_LED_CONNECTED);
                    if (!fromSettings) return true;
                    displaySuccess("USB connected", true);
                    return true;
                }
                gHidRemoteSession.end();
                (void)gHidRemoteSession.begin(
                    HID_REMOTE_BLE,
                    static_cast<HidRemoteCapability>(HID_CAP_KEYBOARD | HID_CAP_MEDIA | HID_CAP_MOUSE)
                );
                redrawSlots();
#else
                displayError("USB HID not available", true);
                redrawSlots();
#endif
                break;
            }
            if (c >= '1' && c < ('1' + KvxputerConfig::HID_REMOTE_HOST_SLOT_COUNT)) {
                int slot = c - '0';
                String addr = kvxConfig.getHidRemoteHostSlot(slot);
                // Filled slot: tap → connect, hold 2s → host options.
                // Empty slot: tap/hold both start pairing (no options yet).
                if (addr.length() && hidRemoteSlotKeyHeld(c)) {
                    hidRemoteHostDetailMenu(slot);
                    redrawSlots();
                    break;
                }
                if (addr.isEmpty()) {
                    if (hidRemoteRunPairIntoSlot(slot)) {
                        hidRemoteLedSet(HID_REMOTE_LED_CONNECTED);
                        if (!fromSettings) return true;
                        String name = gHidRemoteSession.getHostLabel();
                        if (name.isEmpty()) {
                            name = gHidRemoteSession.displayNameForAddr(
                                gHidRemoteSession.getConnectedAddress()
                            );
                        }
                        displaySuccess(String("Slot ") + String(slot) + ":\n" + name, true);
                    } else {
                        hidRemoteIdleAdvertiseStop();
                    }
                } else {
                    selectedSlot = slot;
                    String name = gHidRemoteSession.displayNameForAddr(addr);
                    hidRemoteSwitchWaitUi(slot, name);
                    hidRemoteLedSet(HID_REMOTE_LED_CONNECTING);
                    if (gHidRemoteSession.switchToSlot(slot, 30000)) {
                        hidRemoteLedSet(HID_REMOTE_LED_CONNECTED);
                        if (!fromSettings) return true;
                        displaySuccess("Connected:\n" + name, true);
                    } else {
                        hidRemoteLedSet(HID_REMOTE_LED_DISCONNECTED);
                        displayWarning(
                            "Wrong phone raced in / host idle.\nOpen BT and tap keyboard.",
                            true
                        );
                        hidRemoteIdleAdvertiseStop();
                    }
                }
                redrawSlots();
                break;
            }
        }

        if (check(SelPress)) {
            if (linked) {
                gHidRemoteSession.rememberConnectedHost(0, true);
                return !fromSettings;
            }
            String pref = kvxConfig.hidRemotePreferredHost;
            int filled = 0;
            if (pref.length()) filled = kvxConfig.findHidRemoteHostSlotForAddr(pref);
            if (filled <= 0) {
                for (int s = 1; s <= KvxputerConfig::HID_REMOTE_HOST_SLOT_COUNT; s++) {
                    if (kvxConfig.getHidRemoteHostSlot(s).length()) {
                        filled = s;
                        break;
                    }
                }
            }
            if (filled > 0) {
                selectedSlot = filled;
                String a = kvxConfig.getHidRemoteHostSlot(filled);
                hidRemoteSwitchWaitUi(filled, gHidRemoteSession.displayNameForAddr(a));
                hidRemoteLedSet(HID_REMOTE_LED_CONNECTING);
                if (gHidRemoteSession.switchToSlot(filled, 30000)) {
                    hidRemoteLedSet(HID_REMOTE_LED_CONNECTED);
                    if (!fromSettings) return true;
                    displaySuccess(
                        "Connected:\n" + gHidRemoteSession.displayNameForAddr(a), true
                    );
                } else {
                    hidRemoteLedSet(HID_REMOTE_LED_DISCONNECTED);
                    hidRemoteIdleAdvertiseStop();
                }
            } else {
                int slot = kvxConfig.findEmptyHidRemoteHostSlot();
                if (slot > 0) {
                    if (hidRemoteRunPairIntoSlot(slot)) {
                        hidRemoteLedSet(HID_REMOTE_LED_CONNECTED);
                        if (!fromSettings) return true;
                        String name = gHidRemoteSession.getHostLabel();
                        if (name.isEmpty()) {
                            name = gHidRemoteSession.displayNameForAddr(
                                gHidRemoteSession.getConnectedAddress()
                            );
                        }
                        displaySuccess(String("Slot ") + String(slot) + ":\n" + name, true);
                    } else {
                        hidRemoteIdleAdvertiseStop();
                    }
                } else {
                    displayWarning("All 6 host slots full.\nForget a host first.", true);
                }
            }
            redrawSlots();
            continue;
        }

        delay(40);
    }
    return false;
#endif
}

static bool hidRemoteHostListScreen(bool fromSettings) {
    if (gHidRemoteSession.transport != HID_REMOTE_BLE) {
        displayInfo("Switch transport to BLE first", true);
        return fromSettings;
    }

    gHidRemoteSession.syncHostSlotsWithBonds();
    const bool startedConnected = gHidRemoteSession.isConnected();
    const String startedAddr = startedConnected ? gHidRemoteSession.getConnectedAddress() : String("");

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
                             hidRemoteIdleAdvertiseStop();
                         }
                     }}
                );
            } else {
                label = String(slot) + " " + gHidRemoteSession.displayNameForAddr(addr);
                if (live.length() && hidRemoteAddrEqual(live, addr) && gHidRemoteSession.isConnected()) {
                    label = "* " + label;
                }
                opts.push_back({label, [=]() { hidRemoteHostDetailMenu(slot); }});
            }
        }

        opts.push_back({"Connect to new device", []() { hidRemoteConnectNewDevice(); }});
        opts.push_back({"Back", []() {}});

        int sel = loopOptions(opts, MENU_TYPE_SUBMENU, "Select host");
        // Leave this list once a host actually comes up. Rebuilding "Select host"
        // after pair/switch looked like the new connection was rejected.
        if (gHidRemoteSession.isConnected()) {
            const String now = gHidRemoteSession.getConnectedAddress();
            const bool newLink =
                !startedConnected || (now.length() && !hidRemoteAddrEqual(now, startedAddr));
            if (newLink) return true;
            if (!fromSettings) return true;
        }
        if (sel < 0 || sel == (int)opts.size() - 1) return fromSettings;
    }
}

static void hidRemoteHostsMenu() {
    (void)hidRemoteHostListScreen(true);
}

static void hidRemoteRenameHostsMenu() {
    if (gHidRemoteSession.transport != HID_REMOTE_BLE) {
        displayInfo("Switch transport to BLE first", true);
        return;
    }
    gHidRemoteSession.syncHostSlotsWithBonds();

    while (true) {
        std::vector<Option> opts;
        for (int slot = 1; slot <= KvxputerConfig::HID_REMOTE_HOST_SLOT_COUNT; slot++) {
            String addr = kvxConfig.getHidRemoteHostSlot(slot);
            if (addr.isEmpty()) continue;
            String name = gHidRemoteSession.displayNameForAddr(addr);
            opts.push_back(
                {String(slot) + " " + name,
                 [=]() {
                     String n = keyboard(name, 24, ("Name slot " + String(slot)).c_str());
                     if (n.length() > 0 && n != "\x1B") {
                         kvxConfig.setHidRemoteHostAlias(addr, n);
                         if (gHidRemoteSession.isConnectedToAddr(addr)) {
                             gHidRemoteSession.refreshHostLabel();
                         }
                         displaySuccess("Named:\n" + n, true);
                     }
                 }}
            );
        }
        if (opts.empty()) {
            displayInfo("No hosts to rename.\nPair a host first.", true);
            return;
        }
        opts.push_back({"Back", []() {}});
        int sel = loopOptions(opts, MENU_TYPE_SUBMENU, "Rename hosts");
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
                 if (gHidRemoteSession.disconnectHost()) {
                     hidRemoteIdleAdvertiseStop();
                     hidRemoteLedSet(HID_REMOTE_LED_DISCONNECTED);
                     displayInfo("Disconnected", true);
                 } else displayError("Disconnect failed", true);
             }},
            {"Host slots...",
             []() {
                 if (gHidRemoteSession.transport != HID_REMOTE_BLE) {
                     displayInfo("Switch transport to BLE first", true);
                     return;
                 }
                 (void)hidRemoteHostSlotScreen(true);
             }},
            {"Rename hosts...", hidRemoteRenameHostsMenu},
            {"Host diagnostics...",
             []() {
                 std::vector<String> lines = gHidRemoteSession.describeHostBinding();
                 lines.push_back("Back");
                 (void)hidRemotePickFromList("Host diagnostics", lines, 0);
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
                     displayMessage("Forget ALL hosts?\nOk=Yes  . =No", "Yes", nullptr, "No", TFT_WHITE);
                 if (choice != 0) return;
                 bool ok = gHidRemoteSession.forgetBonds();
                 int left = gHidRemoteSession.getBondCount();
                 // left==0 is the real success signal (ok can be false if stack was
                 // already down / getNumBonds raced during wipe).
                 if (left == 0) {
                     hidRemoteLedSet(HID_REMOTE_LED_FORGET_OK);
                     displayInfo("Forgot all pairings.\nSlots cleared.", true);
                     (void)ok;
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
            {"Default new-host name: " +
                 (kvxConfig.hidRemoteHostName.length() ? kvxConfig.hidRemoteHostName : "(auto)"),
             []() {
                 String n = keyboard(kvxConfig.hidRemoteHostName, 32, "Default host name");
                 if (n == "\x1B") return;
                 kvxConfig.setHidRemoteHostName(n);
             }},
            {String("Keyboard LED: ") + (kvxConfig.hidRemoteLedEnabled ? "On" : "Off"),
             []() {
                 kvxConfig.setHidRemoteLedEnabled(!kvxConfig.hidRemoteLedEnabled);
                 if (!kvxConfig.hidRemoteLedEnabled) hidRemoteLedSet(HID_REMOTE_LED_OFF);
                 else if (gHidRemoteSession.isConnected()) hidRemoteLedSet(HID_REMOTE_LED_CONNECTED);
                 else hidRemoteLedSet(HID_REMOTE_LED_OFF);
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
    hidRemoteLedSet(HID_REMOTE_LED_CONNECTING);
    // Drop any leftover EscPress (e.g. from an earlier Del on older firmware).
    EscPress = false;

    while (!check(EscPress)) {
        hidRemoteLedTick();
        if (forceHome) {
            hidRemoteLedSet(HID_REMOTE_LED_DISCONNECTED);
            return -1;
        }
        if (check(SelPress)) {
            hidRemoteSettingsMenu();
            hidRemoteDrawConnectScreen(transport, nullptr, nullptr);
            start = millis();
            continue;
        }
        if (gHidRemoteSession.isConnected()) {
            hidRemoteLedSet(HID_REMOTE_LED_CONNECTED);
            return 1;
        }

        if (timeoutMs > 0 && (millis() - start) >= timeoutMs) {
            hidRemoteLedSet(HID_REMOTE_LED_DISCONNECTED);
            return 0;
        }
        delay(40);
    }
    hidRemoteLedSet(HID_REMOTE_LED_DISCONNECTED);
    return -1;
}

static bool hidRemoteConnect(HidRemoteTransport transport) {
    if (transport == HID_REMOTE_USB) {
        hidRemoteDrawConnectScreen(transport, nullptr, nullptr);
        if (!gHidRemoteSession.begin(
                transport, static_cast<HidRemoteCapability>(HID_CAP_KEYBOARD | HID_CAP_MEDIA | HID_CAP_MOUSE)
            )) {
            displayError("HID init failed", true);
            hidRemoteLedSet(HID_REMOTE_LED_ERROR);
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
        hidRemoteLedSet(HID_REMOTE_LED_ERROR);
        return false;
    }

    if (!hidRemoteHostSlotScreen(false)) {
        gHidRemoteSession.end();
        return false;
    }
    return true;
}

static int hidRemoteModePicker(int startIndex) {
    if (startIndex < 0 || startIndex >= HID_MODE_COUNT) startIndex = 0;
    const int settingsIdx = HID_MODE_COUNT;
    const int exitIdx = HID_MODE_COUNT + 1;

    while (true) {
        // Drop only when live peer clearly belongs to a different filled slot.
        // Address-form mismatches must not tear down a just-accepted link.
        if (gHidRemoteSession.transport == HID_REMOTE_BLE && gHidRemoteSession.isConnected()) {
            String pref = kvxConfig.hidRemotePreferredHost;
            if (pref.length() && !gHidRemoteSession.isConnectedToAddr(pref)) {
                const int prefSlot = kvxConfig.findHidRemoteHostSlotForAddr(pref);
                const String live = gHidRemoteSession.getConnectedAddress();
                const int liveSlot =
                    live.length() ? kvxConfig.findHidRemoteHostSlotForAddr(live) : 0;
                if (prefSlot > 0 && liveSlot > 0 && liveSlot != prefSlot) {
                    gHidRemoteSession.disconnectHost(false);
                    gHidRemoteSession.advertiseStop();
                }
            }
        }

        if (gHidRemoteSession.isConnected()) hidRemoteLedSet(HID_REMOTE_LED_CONNECTED);
        else {
            hidRemoteLedSet(HID_REMOTE_LED_DISCONNECTED);
            // Stay non-connectable in the mode picker. Auto-ADV here was letting
            // iPhone reclaim the link even when the user had selected another host.
            if (gHidRemoteSession.transport == HID_REMOTE_BLE) {
                gHidRemoteSession.advertiseStop();
            }
        }
        std::vector<Option> opts;
        for (int i = 0; i < HID_MODE_COUNT; i++) {
            opts.push_back({hidRemoteModeInfo(static_cast<HidRemoteMode>(i)).label, []() {}});
        }
        opts.push_back({"Settings", hidRemoteSettingsMenu});
        opts.push_back({"Exit", []() {}});

        int sel = loopOptions(opts, MENU_TYPE_SUBMENU, KVXKEYBOARD_HID_NAME, startIndex, false);
        if (sel < 0 || sel == exitIdx) return -1; // Esc or Exit
        if (sel == settingsIdx) {
            // Settings callback already ran inside loopOptions — stay in picker
            startIndex = 0;
            continue;
        }
        if (sel >= 0 && sel < HID_MODE_COUNT) return sel;
        return -1;
    }
}

void hidRemoteMenu(HidRemoteLaunch launch) {
    HidRemoteTransport transport = resolveTransport(launch);
    hidRemoteLedBegin();

    if (!hidRemoteConnect(transport)) {
        hidRemoteLedEnd();
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
    hidRemoteLedEnd();
    returnToMenu = true;
}
