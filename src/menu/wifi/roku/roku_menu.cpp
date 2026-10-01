#include "roku.h"
#include "roku_ecp.h"
#include "root/app/utils.h"
#include "root/config/config.h"
#include "root/input/mykeyboard.h"
#include "root/net/wifi_common.h"
#include "root/ui/display.h"
#include <WiFi.h>
#include <globals.h>

namespace {

RokuEcp gEcp;
RokuDevice gDevice;
std::vector<RokuApp> gAppsCache;
bool gAppsLoaded = false;
bool gPowerConfirmDone = false;
bool gFnPad = false;
bool gPrevFnHeld = false;
int gFlashKey = 0;
unsigned long gFlashUntil = 0;
unsigned long gLastRepeatMs = 0;
String gStatusLine;

bool gNeedRedraw = false;

constexpr unsigned long REPEAT_MS = 150;
constexpr unsigned long POWER_HOLD_MS = 2000;

bool wifiOkOrBail() {
    if (WiFi.isConnected()) return true;
    displayError("WiFi down", true);
    return false;
}

void setStatus(const String &s) { gStatusLine = s; }

void drawKeyBtn(int x, int y, int w, int h, const char *keyLabel, const char *desc, bool highlight) {
    if (w < 8 || h < 10) return;
    const uint16_t fill = kvxConfig.secColor;
    const uint16_t border = highlight ? kvxConfig.priColor : kvxConfig.priColor;
    tft.fillRoundRect(x, y, w, h, 3, fill);
    tft.drawRoundRect(x, y, w, h, 3, border);
    if (highlight) tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 2, kvxConfig.priColor);
    tft.setTextSize(uiDenseFont());

    const bool hasDesc = desc != nullptr && desc[0] != '\0';
    const bool twoLine = hasDesc && h >= 18;
    const int textY1 = twoLine ? y + 1 : y + (h - 8) / 2;
    const int textY2 = y + h - 9;

    tft.setTextColor(kvxConfig.priColor, fill);
    tft.drawCentreString(keyLabel, x + w / 2, textY1, 1);
    if (twoLine) {
        tft.setTextColor(kvxConfig.bgColor, fill);
        // Use pri on sec fill for readability when bg is dark; prefer orange-ish via pri
        tft.setTextColor(kvxConfig.priColor, fill);
        tft.drawCentreString(desc, x + w / 2, textY2, 1);
    }
}

void drawFnPad() {
    int contentTop = BORDER_PAD_Y + uiLineH(FM) + 2;
    int contentBottom = uiFooterY(FP) - 2;
    const int margin = 2;
    const int cols = 5;
    const int hgap = 2;
    const int vgap = 2;

    struct Cell {
        const char *key;
        const char *dest;
        int ch;
    };
    static const Cell cells[] = {
        {";", "Up", ';'},     {".", "Dn", '.'},     {",", "Lt", ','},     {"/", "Rt", '/'},
        {"Ok", "Sel", '\n'},  {"Del", "Back", '\b'}, {"H", "Home", 'h'},  {"S", "Srch", 's'},
        {"P", "Play", 'p'},   {"O", "Pause", 'o'},  {"R", "Rev", 'r'},   {"F", "Fwd", 'f'},
        {"=", "Vol+", '='},   {"-", "Vol-", '-'},   {"M", "Mute", 'm'},  {"I", "Info", 'i'},
        {"G", "Rply", 'g'},   {"A", "Apps", 'a'},   {"D", "Dev", 'd'},
    };
    const size_t nCells = sizeof(cells) / sizeof(cells[0]);
    const int rows = (int)((nCells + cols - 1) / cols);
    int colW = (tftWidth - margin * 2 - hgap * (cols - 1)) / cols;
    int rowH = (contentBottom - contentTop - vgap * (rows - 1)) / rows;
    if (rowH > 22) rowH = 22;
    if (rowH < 14) rowH = 14;

    tft.fillRect(0, contentTop, tftWidth, contentBottom - contentTop, kvxConfig.bgColor);
    for (size_t i = 0; i < nCells; i++) {
        const int col = i % cols;
        const int row = i / cols;
        const int x = margin + col * (colW + hgap);
        const int y = contentTop + row * (rowH + vgap);
        if (y + rowH > contentBottom) break;
        bool flash = gFlashKey && (gFlashKey == cells[i].ch);
        // Map Enter/Del flash aliases
        if (gFlashKey == (int)' ' && cells[i].ch == '\n') flash = true;
        drawKeyBtn(x, y, colW, rowH, cells[i].key, cells[i].dest, flash);
    }
    tft.setTextSize(uiDenseFont());
    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    tft.drawCentreString("hold P power  Fn hides  fn+Ok exit", tftWidth / 2, uiFooterY(FP), 1);
}

void drawRemoteChrome(const char *modeHint) {
    TftFrame frame;
    drawMainBorderWithTitle("Roku");
    tft.setTextSize(uiMenuFont());
    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    String name = gDevice.name.length() ? gDevice.name : gEcp.ip();
    if (name.length() > 18) name = name.substring(0, 18);
    tft.drawCentreString(name, tftWidth / 2, uiStatusY(0), 1);

    tft.setTextSize(uiBodyFont());
    String wifiLine = WiFi.isConnected() ? WiFi.SSID() : String("No WiFi");
    if (wifiLine.length() > 20) wifiLine = wifiLine.substring(0, 20);
    tft.drawCentreString(wifiLine, tftWidth / 2, uiStatusY(1), 1);

    tft.setTextColor(kvxConfig.secColor, kvxConfig.bgColor);
    tft.drawCentreString(modeHint, tftWidth / 2, uiStatusY(2), 1);

    if (gStatusLine.length()) {
        tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
        String st = gStatusLine;
        if (st.length() > 22) st = st.substring(0, 22);
        tft.drawCentreString(st, tftWidth / 2, uiStatusY(3), 1);
    }

    tft.setTextSize(uiDenseFont());
    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    tft.drawCentreString("Fn=keys  fn+Ok=exit", tftWidth / 2, uiFooterY(FP), 1);
}

bool sendKey(const char *ecpKey, int flashCh = 0) {
    if (!wifiOkOrBail()) return false;
    RokuEcpStatus st = gEcp.postKey(ecpKey);
    if (st != ROKU_ECP_OK) {
        setStatus(RokuEcp::statusMessage(st));
        gNeedRedraw = true;
        // Modal only once for real 403 so a sticky Roku setting does not brick every key
        static bool showedForbidden = false;
        if (st == ROKU_ECP_FORBIDDEN && !showedForbidden) {
            showedForbidden = true;
            displayError("Settings > System >\nAdvanced > Control by\nmobile apps = Enabled", true);
            gNeedRedraw = true;
        }
        if (st == ROKU_ECP_WIFI_DOWN) return false;
        return false;
    }
    setStatus("");
    if (flashCh) {
        gFlashKey = flashCh;
        gFlashUntil = millis() + 120;
    }
    return true;
}

String litKey(char c) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
        return String("Lit_") + c;
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "Lit_%%%02X", (unsigned char)c);
    return String(buf);
}

bool pollFnEdge() {
    const bool held = isFnKeyHeld();
    const bool edge = held && !gPrevFnHeld;
    gPrevFnHeld = held;
    return edge;
}

void clearNavFlags() {
    UpPress = false;
    DownPress = false;
    PrevPress = false;
    NextPress = false;
    NextPagePress = false;
    PrevPagePress = false;
    SelPress = false;
    EscPress = false;
    AnyKeyPress = false;
}

bool confirmPowerOff() {
    if (gPowerConfirmDone) return true;
    options = {
        {"Power Off", []() {}},
        {"Cancel", []() {}},
    };
    int sel = loopOptions(options, MENU_TYPE_SUBMENU, "Confirm");
    options.clear();
    gNeedRedraw = true;
    if (returnToMenu || forceHome) return false;
    if (sel == 0) {
        gPowerConfirmDone = true;
        return true;
    }
    return false;
}

bool applyDevice(const RokuDevice &dev) {
    gEcp.setBase(dev.ip);
    RokuDevice info = dev;
    RokuEcpStatus st = gEcp.queryDeviceInfo(info, 1500);
    if (st != ROKU_ECP_OK) {
        displayError(RokuEcp::statusMessage(st), true);
        return false;
    }
    gDevice = info;
    kvxConfig.setRokuDevice(info.ip, info.name, info.serial);
    gAppsLoaded = false;
    gAppsCache.clear();
    return true;
}

bool promptManualIp() {
    String ip = keyboard(kvxConfig.rokuIp, 15, "Roku IP:");
    if (ip == "\x1B" || returnToMenu || forceHome) return false;
    ip.trim();
    if (!ip.length()) {
        displayError("Empty IP", true);
        return false;
    }
    RokuDevice d;
    d.ip = ip;
    d.name = ip;
    return applyDevice(d);
}

enum PickResult { PICK_NONE, PICK_DEVICE, PICK_MANUAL, PICK_RESCAN, PICK_CANCEL };

PickResult pickDeviceList(std::vector<RokuDevice> &found, bool allowEmptyActions) {
    while (!returnToMenu && !forceHome) {
        options.clear();
        for (size_t i = 0; i < found.size(); i++) {
            String label = found[i].name.length() ? found[i].name : found[i].ip;
            if (found[i].model.length()) label += " (" + found[i].model + ")";
            if (label.length() > 28) label = label.substring(0, 28);
            size_t idx = i;
            options.push_back({label.c_str(), [idx, &found]() {
                                   // selection index captured via loopOptions return
                                   (void)idx;
                                   (void)found;
                               }});
        }
        options.push_back({"Enter IP", []() {}});
        options.push_back({"Rescan", []() {}});
        addOptionToMainMenu();

        int sel = loopOptions(options, MENU_TYPE_SUBMENU, "Pick Roku");
        options.clear();
        if (returnToMenu || forceHome) return PICK_CANCEL;
        if (sel < 0) return PICK_CANCEL;

        const int nDev = (int)found.size();
        if (sel < nDev) {
            // Enrich name via device-info when we only have IP from SSDP
            if (applyDevice(found[sel])) return PICK_DEVICE;
            continue;
        }
        if (sel == nDev) return PICK_MANUAL;
        if (sel == nDev + 1) return PICK_RESCAN;
        return PICK_CANCEL;
    }
    (void)allowEmptyActions;
    return PICK_CANCEL;
}

bool emptyDiscoveryMenu() {
    while (!returnToMenu && !forceHome) {
        options = {
            {"Rescan", []() {}},
            {"Enter IP", []() {}},
        };
        addOptionToMainMenu();
        int sel = loopOptions(options, MENU_TYPE_SUBMENU, "No Roku found");
        options.clear();
        if (returnToMenu || forceHome) return false;
        if (sel == 0) return true; // rescan
        if (sel == 1) {
            if (promptManualIp()) {
                // Connected — stop outer discover loop
                return false;
            }
            continue;
        }
        return false;
    }
    return false;
}

bool discoverAndPick() {
    while (!returnToMenu && !forceHome) {
        if (!wifiOkOrBail()) return false;
        displayInfo("Scanning…");
        auto found = RokuEcp::ssdpDiscover(2000);
        if (returnToMenu || forceHome) return false;

        // Fetch friendly names for each IP (short timeout)
        for (auto &d : found) {
            if (returnToMenu || forceHome) return false;
            RokuEcp tmp;
            tmp.setBase(d.ip);
            RokuDevice info;
            if (tmp.queryDeviceInfo(info, 1000) == ROKU_ECP_OK) d = info;
        }

        if (found.empty()) {
            bool again = emptyDiscoveryMenu();
            if (gEcp.ip().length() && gDevice.ip.length()) return true;
            if (!again) return false;
            continue;
        }

        if (found.size() == 1) return applyDevice(found[0]);

        PickResult r = pickDeviceList(found, true);
        if (r == PICK_DEVICE) return true;
        if (r == PICK_MANUAL) {
            if (promptManualIp()) return true;
            continue;
        }
        if (r == PICK_RESCAN) continue;
        return false;
    }
    return false;
}

bool gateWifiAndDevice() {
    if (!WiFi.isConnected() && !wifiConnectMenu(WIFI_STA)) return false;
    if (!WiFi.isConnected()) return false;

    displayInfo(WiFi.SSID() + " " + WiFi.localIP().toString(), true);

    // Try last device first
    if (kvxConfig.rokuIp.length()) {
        gEcp.setBase(kvxConfig.rokuIp);
        RokuDevice info;
        if (gEcp.queryDeviceInfo(info, 1500) == ROKU_ECP_OK) {
            gDevice = info;
            kvxConfig.setRokuDevice(info.ip, info.name, info.serial);
            return true;
        }
    }

    return discoverAndPick();
}

enum RemoteAction {
    ACT_NONE,
    ACT_LEAVE,
    ACT_TYPING,
    ACT_APPS,
    ACT_REDISCOVER,
};

RemoteAction handleRemoteKeys() {
    const unsigned long now = millis();

    auto edgeOrRepeat = [&](bool pressed, char flashCh, const char *ecp, bool &wasHeld) {
        if (pressed) {
            if (!wasHeld || now - gLastRepeatMs >= REPEAT_MS) {
                if (sendKey(ecp, flashCh)) gLastRepeatMs = millis();
            }
            wasHeld = true;
        } else {
            wasHeld = false;
        }
    };

    static bool heldUp = false, heldDn = false, heldLt = false, heldRt = false;
    static bool heldVu = false, heldVd = false;

    // Consume firmware nav pulses; rate-limit ECP ourselves
    const bool up = UpPress || isCardputerKeyHeld(';');
    const bool dn = DownPress || isCardputerKeyHeld('.');
    const bool lt = PrevPagePress || isCardputerKeyHeld(',');
    const bool rt = NextPagePress || isCardputerKeyHeld('/');
    UpPress = false;
    DownPress = false;
    PrevPress = false;
    NextPress = false;
    PrevPagePress = false;
    NextPagePress = false;

    edgeOrRepeat(up, ';', "Up", heldUp);
    edgeOrRepeat(dn, '.', "Down", heldDn);
    edgeOrRepeat(lt, ',', "Left", heldLt);
    edgeOrRepeat(rt, '/', "Right", heldRt);

    const bool vu = isCardputerKeyHeld('=') || isCardputerKeyHeld(']');
    const bool vd = isCardputerKeyHeld('-') || isCardputerKeyHeld('[');
    edgeOrRepeat(vu, '=', "VolumeUp", heldVu);
    edgeOrRepeat(vd, '-', "VolumeDown", heldVd);

    // Hold P for power; short tap = Play on release
    static unsigned long pDownAt = 0;
    static bool pPowered = false;
    static bool pWasHeld = false;
    const bool pHeld = isCardputerKeyHeld('p') || isCardputerKeyHeld('P');
    if (pHeld) {
        if (!pWasHeld) {
            pDownAt = millis();
            pPowered = false;
        }
        pWasHeld = true;
        if (!pPowered && millis() - pDownAt >= POWER_HOLD_MS) {
            pPowered = true;
            if (confirmPowerOff()) sendKey("PowerOff", 'p');
        }
    } else if (pWasHeld) {
        if (!pPowered && pDownAt && (millis() - pDownAt < POWER_HOLD_MS)) sendKey("Play", 'p');
        pWasHeld = false;
        pDownAt = 0;
        pPowered = false;
    }

    // Fn+Ok exits the app (same as kvxkeyboard HID). Esc/` only dismisses the
    // Fn pad — it must not return to the main menu.
    if (KeyStroke.pressed && KeyStroke.fn && (KeyStroke.enter || KeyStroke.exit_key)) {
        KeyStroke.Clear();
        SelPress = false;
        EscPress = false;
        return ACT_LEAVE;
    }
    if (check(EscPress)) {
        if (gFnPad) {
            gFnPad = false;
            gNeedRedraw = true;
        }
        KeyStroke.Clear();
        return ACT_NONE;
    }

    if (check(SelPress) || (KeyStroke.pressed && KeyStroke.enter && !KeyStroke.fn)) {
        sendKey("Select", '\n');
        KeyStroke.Clear();
        return ACT_NONE;
    }
    if (KeyStroke.pressed && KeyStroke.del) {
        sendKey("Back", '\b');
        KeyStroke.Clear();
        return ACT_NONE;
    }

    if (!KeyStroke.pressed) return ACT_NONE;

    keyStroke key = KeyStroke;
    for (char c : key.word) {
        uint8_t u = (uint8_t)c;
        if (c == ';' || c == '.' || c == ',' || c == '/') continue;
        if (c == '=' || c == ']' || c == '-' || c == '[') continue;
        if (c == 0xDA || c == 0xD9 || c == 0xD8 || c == 0xD7 || c == 0xB1) continue;

        if (u == 0xB3) {
            sendKey("InstantReplay", 'g');
            continue;
        }
        if (c == ' ') {
            sendKey("Select", ' ');
            continue;
        }

        char lower = c;
        if (lower >= 'A' && lower <= 'Z') lower = (char)(lower - 'A' + 'a');

        if (lower == 'h') sendKey("Home", 'h');
        else if (lower == 'i' || c == '*') sendKey("Info", 'i');
        else if (lower == 'g') sendKey("InstantReplay", 'g');
        else if (lower == 's') {
            KeyStroke.Clear();
            return ACT_TYPING;
        } else if (lower == 'o') sendKey("Pause", 'o');
        else if (lower == 'r') sendKey("Rev", 'r');
        else if (lower == 'f') sendKey("Fwd", 'f');
        else if (lower == 'm') sendKey("VolumeMute", 'm');
        else if (lower == 'a') {
            KeyStroke.Clear();
            return ACT_APPS;
        } else if (lower == 'd') {
            KeyStroke.Clear();
            return ACT_REDISCOVER;
        } else if (lower == 'p') {
            // hold/release above
        } else if (c >= '0' && c <= '9') {
            String lit = litKey(c);
            sendKey(lit.c_str(), c);
        }
    }

    KeyStroke.Clear();
    AnyKeyPress = false;
    return ACT_NONE;
}

void runTypingMode() {
    sendKey("Search");
    String buffer;
    bool dirty = true;
    gFnPad = false;

    while (!returnToMenu && !forceHome) {
        if (dirty) {
            TftFrame frame;
            drawMainBorderWithTitle("Typing");
            tft.setTextSize(uiBodyFont());
            tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
            tft.drawString(gDevice.name.length() ? gDevice.name : gEcp.ip(), BORDER_PAD_X, uiStatusY(0));
            tft.setTextColor(kvxConfig.secColor, kvxConfig.bgColor);
            tft.drawString("Search", BORDER_PAD_X, uiStatusY(1));
            tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
            String show = buffer;
            if (show.length() > 22) show = show.substring(show.length() - 22);
            tft.fillRect(BORDER_PAD_X, uiStatusY(2), tftWidth - 2 * BORDER_PAD_X, uiLineH(FP) + 2,
                         kvxConfig.bgColor);
            tft.drawString(show.length() ? show : "_", BORDER_PAD_X, uiStatusY(2));
            tft.setTextSize(uiDenseFont());
            tft.drawCentreString("`=back  Del=erase", tftWidth / 2, uiFooterY(FP), 1);
            dirty = false;
        }

        // Always drain nav flags so ;.,/ are Lit_ only
        clearNavFlags();

        if (check(EscPress)) break;

        if (!KeyStroke.pressed && !KeyStroke.del && !check(SelPress) && !AnyKeyPress) {
            delay(10);
            continue;
        }

        keyStroke key = KeyStroke;
        if (key.del) {
            sendKey("Backspace");
            if (buffer.length()) buffer.remove(buffer.length() - 1);
            dirty = true;
        } else if (key.enter || check(SelPress)) {
            sendKey("Enter");
        } else {
            for (char c : key.word) {
                uint8_t u = (uint8_t)c;
                if (u == 0xB3 || u == 0xB1 || u == 0xDA || u == 0xD9 || u == 0xD8 || u == 0xD7) continue;
                if (c == '`') {
                    KeyStroke.Clear();
                    return;
                }
                if (c < 32 || c > 126) continue;
                String lit = litKey(c);
                if (!sendKey(lit.c_str())) {
                    KeyStroke.Clear();
                    return;
                }
                buffer += c;
                dirty = true;
            }
        }
        KeyStroke.Clear();
        AnyKeyPress = false;
        delay(10);
    }
}

void runAppsMode() {
    if (!gAppsLoaded) {
        displayInfo("Loading apps…");
        RokuEcpStatus st = gEcp.queryApps(gAppsCache, 2500);
        if (st != ROKU_ECP_OK) {
            displayError(RokuEcp::statusMessage(st), true);
            return;
        }
        gAppsLoaded = true;
    }

    String filter;
    int cursor = 0;
    int scroll = 0;
    bool dirty = true;

    auto matches = [&](const RokuApp &a) {
        if (!filter.length()) return true;
        String n = a.name;
        n.toLowerCase();
        String f = filter;
        f.toLowerCase();
        return n.indexOf(f) >= 0;
    };

    while (!returnToMenu && !forceHome) {
        std::vector<int> idxs;
        for (size_t i = 0; i < gAppsCache.size(); i++) {
            if (matches(gAppsCache[i])) idxs.push_back((int)i);
        }
        if (cursor >= (int)idxs.size()) cursor = max(0, (int)idxs.size() - 1);
        if (cursor < 0) cursor = 0;

        const int visible = 4;
        if (cursor < scroll) scroll = cursor;
        if (cursor >= scroll + visible) scroll = cursor - visible + 1;

        if (dirty) {
            TftFrame frame;
            drawMainBorderWithTitle("Apps");
            tft.setTextSize(uiDenseFont());
            tft.setTextColor(kvxConfig.secColor, kvxConfig.bgColor);
            String fl = filter.length() ? filter : "(type to filter)";
            if (fl.length() > 22) fl = fl.substring(0, 22);
            tft.drawString(fl, BORDER_PAD_X, uiStatusY(0));

            tft.setTextSize(uiBodyFont());
            for (int row = 0; row < visible; row++) {
                int li = scroll + row;
                int y = uiStatusY(1 + row);
                tft.fillRect(BORDER_PAD_X - 2, y, tftWidth - 2 * BORDER_PAD_X + 4, uiRowH(FP),
                             kvxConfig.bgColor);
                if (li >= (int)idxs.size()) continue;
                const auto &app = gAppsCache[idxs[li]];
                String label = app.name;
                if (label.length() > 20) label = label.substring(0, 20);
                if (li == cursor) {
                    tft.fillRect(BORDER_PAD_X - 2, y, tftWidth - 2 * BORDER_PAD_X + 4, uiLineH(FP) + 2,
                                 kvxConfig.secColor);
                    tft.setTextColor(kvxConfig.priColor, kvxConfig.secColor);
                } else {
                    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
                }
                tft.drawString(label, BORDER_PAD_X, y);
            }
            tft.setTextSize(uiDenseFont());
            tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
            tft.drawCentreString("Enter=launch  `=back", tftWidth / 2, uiFooterY(FP), 1);
            dirty = false;
        }

        if (check(EscPress)) break;
        if (check(UpPress) || check(PrevPress)) {
            if (cursor > 0) cursor--;
            dirty = true;
        }
        if (check(DownPress) || check(NextPress)) {
            if (cursor + 1 < (int)idxs.size()) cursor++;
            dirty = true;
        }
        if (check(SelPress) || (KeyStroke.pressed && KeyStroke.enter)) {
            if (idxs.size()) {
                const auto &app = gAppsCache[idxs[cursor]];
                RokuEcpStatus st = gEcp.launch(app.id);
                if (st != ROKU_ECP_OK) displayError(RokuEcp::statusMessage(st), true);
                else displaySuccess(app.name, true);
                dirty = true;
            }
            KeyStroke.Clear();
            continue;
        }

        if (KeyStroke.pressed) {
            if (KeyStroke.del) {
                if (filter.length()) filter.remove(filter.length() - 1);
                cursor = 0;
                scroll = 0;
                dirty = true;
            } else {
                for (char c : KeyStroke.word) {
                    if (c == '`') {
                        KeyStroke.Clear();
                        return;
                    }
                    if (c >= 32 && c <= 126 && c != ';' && c != '.' && c != ',' && c != '/') {
                        // Allow letters/digits/space in filter; skip pure nav punctuation
                        // Actually plan says letters narrow — allow most printable except nav
                    }
                    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == ' ') {
                        filter += c;
                        cursor = 0;
                        scroll = 0;
                        dirty = true;
                    }
                }
            }
            KeyStroke.Clear();
            AnyKeyPress = false;
        }
        delay(10);
    }
}

void runRemoteLoop() {
    bool dirty = true;
    gFnPad = false;
    gPrevFnHeld = false;
    gNeedRedraw = false;
    setStatus("");

    while (!returnToMenu && !forceHome) {
        if (gFlashKey && millis() > gFlashUntil) {
            gFlashKey = 0;
            dirty = true;
        }
        if (gNeedRedraw) {
            gNeedRedraw = false;
            dirty = true;
        }

        // Exit before Fn-pad toggle so fn+Ok does not also flip the legend
        if (KeyStroke.pressed && KeyStroke.fn && (KeyStroke.enter || KeyStroke.exit_key)) {
            KeyStroke.Clear();
            SelPress = false;
            EscPress = false;
            break;
        }

        if (pollFnEdge()) {
            gFnPad = !gFnPad;
            dirty = true;
        }

        if (dirty) {
            if (gFnPad) {
                TftFrame frame;
                drawMainBorderWithTitle("Roku");
                drawFnPad();
            } else {
                drawRemoteChrome("Nav");
            }
            dirty = false;
        }

        RemoteAction act = handleRemoteKeys();
        if (gFlashKey || gNeedRedraw) dirty = true;

        if (act == ACT_LEAVE) break;
        if (act == ACT_TYPING) {
            runTypingMode();
            EscPress = false;
            KeyStroke.Clear();
            dirty = true;
            gFnPad = false;
            continue;
        }
        if (act == ACT_APPS) {
            runAppsMode();
            EscPress = false;
            KeyStroke.Clear();
            dirty = true;
            gFnPad = false;
            continue;
        }
        if (act == ACT_REDISCOVER) {
            if (discoverAndPick()) dirty = true;
            else if (returnToMenu || forceHome) break;
            dirty = true;
            continue;
        }

        delay(10);
    }
}

} // namespace

void rokuMenu() {
    if (!forceHome) returnToMenu = false;
    gDevice = {};
    gAppsCache.clear();
    gAppsLoaded = false;
    gPowerConfirmDone = false;
    gFnPad = false;
    gPrevFnHeld = false;
    gFlashKey = 0;
    gStatusLine = "";
    gEcp.setBase("");

    if (!gateWifiAndDevice()) return;
    if (returnToMenu || forceHome) return;

    runRemoteLoop();
}
