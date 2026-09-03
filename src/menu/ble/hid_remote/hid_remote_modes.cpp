#include "hid_remote_modes.h"
#include "hid_remote_ui.h"
#include "root/input/mykeyboard.h"
#include "root/input/unit_joystick2.h"
#include "root/app/utils.h"
#include <pins_arduino.h>
#include <globals.h>
#include <keys.h>
#if defined(HAS_KEYBOARD)
#include <Keyboard.h>
extern Keyboard_Class Keyboard;
#endif

static const HidRemoteModeInfo kModeTable[HID_MODE_COUNT] = {
    {"Presenter",           HID_CAP_KEYBOARD},
    {"Presenter Vertical",  HID_CAP_KEYBOARD},
    {"Keyboard",            HID_CAP_KEYBOARD},
    {"Media",               static_cast<HidRemoteCapability>(HID_CAP_KEYBOARD | HID_CAP_MEDIA)},
    {"Apple Music",         static_cast<HidRemoteCapability>(HID_CAP_KEYBOARD | HID_CAP_MEDIA)},
    {"Movie",               HID_CAP_KEYBOARD},
    {"Mouse",               HID_CAP_MOUSE},
    {"Shorts",              HID_CAP_KEYBOARD},
    {"Mouse Clicker",       HID_CAP_MOUSE},
    {"Mouse Jiggler",       HID_CAP_MOUSE},
    {"Stealth Jiggler",     HID_CAP_MOUSE},
    {"Push-to-Talk",        HID_CAP_KEYBOARD},
    {"System Shortcuts",    HID_CAP_KEYBOARD},
};

const HidRemoteModeInfo &hidRemoteModeInfo(HidRemoteMode mode) {
    if (mode >= HID_MODE_COUNT) return kModeTable[0];
    return kModeTable[mode];
}

static bool checkModeExit(const keyStroke &key) { return key.pressed && key.fn && key.exit_key; }

static void sendRawKey(HidRemoteTransportSession &s, uint8_t hidKey) {
    if (s.keyboardHid == nullptr) return;
    s.releaseAll();
    delay(20);
    if (s.keyboardHid->press(hidKey) == 0) return;
    delay(80);
    s.releaseAll();
    delay(20);
}

static void sendCombo(HidRemoteTransportSession &s, uint8_t mod1, uint8_t mod2, uint8_t key) {
    if (s.keyboardHid == nullptr) return;
    s.keyboardHid->press(mod1);
    if (mod2) s.keyboardHid->press(mod2);
    s.keyboardHid->press(key);
    delay(30);
    s.keyboardHid->releaseAll();
}

static void sendCombo3(
    HidRemoteTransportSession &s, uint8_t mod1, uint8_t mod2, uint8_t mod3, uint8_t key
) {
    if (s.keyboardHid == nullptr) return;
    s.keyboardHid->press(mod1);
    if (mod2) s.keyboardHid->press(mod2);
    if (mod3) s.keyboardHid->press(mod3);
    s.keyboardHid->press(key);
    delay(30);
    s.keyboardHid->releaseAll();
}

static void mirrorAppendTag(String &mirror, const char *tag) {
    if (tag == nullptr || tag[0] == '\0') return;
    mirror += tag;
    if (mirror.length() > 240) mirror.remove(0, mirror.length() - 240);
}

static const char *keyboardFnLabel(int flashKey) {
    switch (flashKey) {
        case '1': return "[F1]";
        case '2': return "[F2]";
        case '3': return "[F3]";
        case '4': return "[F4]";
        case '5': return "[F5]";
        case '6': return "[F6]";
        case '7': return "[F7]";
        case '8': return "[F8]";
        case '9': return "[F9]";
        case '0': return "[F10]";
        case '-': return "[F11]";
        case '=': return "[F12]";
        case 'i': return "[Ins]";
        case 'p': return "[PrtSc]";
        case 'u': return "[Pause]";
        case 'h': return "[Home]";
        case 'e': return "[End]";
        case '[': return "[PgUp]";
        case ']': return "[PgDn]";
        case '`': return "[Esc]";
        case 't': return "[Alt+Tab]";
        case 'w': return "[Win+Tab]";
        case 'x': return "[Ctrl+Shift+Esc]";
        case 'd': return "[Ctrl+Alt+Del]";
        case 'n': return "[NumLk]";
        case 's': return "[ScrLk]";
        case 'm': return "[Menu]";
        case 'l': return "[Del]";
        default: return nullptr;
    }
}

static void sendWinKey(HidRemoteTransportSession &s, uint8_t key) { sendCombo(s, KEY_LEFT_GUI, 0, key); }

static void sendCtrlWinKey(HidRemoteTransportSession &s, uint8_t key) {
    sendCombo(s, KEY_LEFT_CTRL, KEY_LEFT_GUI, key);
}

static char strokeChar(const keyStroke &key) {
    if (!key.word.empty()) return (char)key.word[0];
    return 0;
}

static bool strokeHasArrow(const keyStroke &key, uint8_t arrowHid, int &flashId) {
    for (char c : key.word) {
        if ((uint8_t)c == arrowHid) {
            if (arrowHid == KEY_UP_ARROW) flashId = 0;
            else if (arrowHid == KEY_DOWN_ARROW) flashId = 1;
            else if (arrowHid == KEY_LEFT_ARROW) flashId = 2;
            else flashId = 3;
            return true;
        }
    }
    return false;
}

static const uint8_t kPresenterHid[] = {
    KEY_UP_ARROW, KEY_DOWN_ARROW, KEY_LEFT_ARROW, KEY_RIGHT_ARROW,
    ' ', KEY_PAGE_UP, KEY_PAGE_DOWN, KEY_HOME, KEY_END,
};

static constexpr int kPresenterNext = 1;
static constexpr int kPresenterF5 = 10;

#if defined(HAS_KEYBOARD)
static char presenterLastToken = 0;
static unsigned long presenterLastFire = 0;

static void presenterResetInputState() {
    presenterLastToken = 0;
    presenterLastFire = 0;
}

static int presenterPollDirectKey() {
    Keyboard.update();
    Keyboard_Class::KeysState status = Keyboard.keysState();

    char token = 0;
    int action = -1;

    if (status.fn) {
        if (Keyboard.isKeyPressed(';')) {
            token = ';';
            action = 0;
        } else if (Keyboard.isKeyPressed('.')) {
            token = '.';
            action = 1;
        } else if (Keyboard.isKeyPressed(',')) {
            token = ',';
            action = 2;
        } else if (Keyboard.isKeyPressed('/')) {
            token = '/';
            action = 3;
        }
    } else if (status.enter) {
        token = '\n';
        action = kPresenterNext;
    } else {
        struct Map {
            char ch;
            int act;
        };
        static const Map kMap[] = {
            {';', 0},  {'.', 1},  {',', 2},  {'/', 3},  {' ', 4},  {'[', 5},  {']', 6},
            {'h', 7},  {'H', 7},  {'e', 8},  {'E', 8},  {'p', 9},  {'P', 9},  {'5', kPresenterF5},
        };
        for (const auto &m : kMap) {
            if (Keyboard.isKeyPressed(m.ch)) {
                token = m.ch;
                action = m.act;
                break;
            }
        }
    }

    if (action < 0) {
        presenterLastToken = 0;
        return -1;
    }

    unsigned long now = millis();
    if (token != presenterLastToken || now - presenterLastFire >= 280) {
        presenterLastToken = token;
        presenterLastFire = now;
        return action;
    }
    return -1;
}
#endif

static void presenterClearNavFlags() {
    // Drop leftovers from menu navigation / Unit Scroll / Joystick drift
    PrevPress = false;
    NextPress = false;
    UpPress = false;
    DownPress = false;
    SelPress = false;
    AnyKeyPress = false;
    PrevPagePress = false;
    NextPagePress = false;
#ifdef HAS_ENCODER
    RotaryNetSteps = 0;
#endif
#if defined(HAS_KEYBOARD)
    presenterResetInputState();
    // Drain any queued KeyStroke from selecting the mode
    (void)_getKeyPress();
#endif
}

static bool runPresenterLoop(HidRemoteTransportSession &s, bool vertical) {
    int flashId = -1;
    unsigned long flashUntil = 0;

    presenterClearNavFlags();

    auto draw = [&]() {
        hidRemoteDrawHeader(s.transport, s.isConnected(), vertical ? "Presenter V" : "Presenter");
        hidDrawPresenterPad(vertical, flashId);
        hidRemoteDrawFooter("5=F5  Enter next  fn+Ok back");
    };

    draw();

    while (true) {
        if (flashId >= 0 && millis() > flashUntil) {
            flashId = -1;
            draw();
        }

        int sent = -1;
#if defined(HAS_KEYBOARD)
        // Cardputer has dedicated keys; ignore Prev/Next/Up/Down flags — Unit Scroll
        // / Joystick drift maps those to "," / previous and looks like a stuck key.
        sent = presenterPollDirectKey();
#else
        if (check(UpPress)) sent = 0;
        else if (check(DownPress)) sent = 1;
        else if (check(PrevPress)) sent = 2;
        else if (check(NextPress)) sent = 3;
        else if (check(SelPress)) sent = kPresenterNext;
#endif

        keyStroke key = _getKeyPress();
        if (checkModeExit(key)) break;

        if (sent < 0) {
            int arrowFlash = -1;
            if (strokeHasArrow(key, KEY_UP_ARROW, arrowFlash)) sent = 0;
            else if (strokeHasArrow(key, KEY_DOWN_ARROW, arrowFlash)) sent = 1;
            else if (strokeHasArrow(key, KEY_LEFT_ARROW, arrowFlash)) sent = 2;
            else if (strokeHasArrow(key, KEY_RIGHT_ARROW, arrowFlash)) sent = 3;
            else if (key.pressed) {
                if (key.enter) sent = kPresenterNext;
                else {
                    char c = strokeChar(key);
                    if (c == ';') sent = 0;
                    else if (c == '.') sent = 1;
                    else if (c == ',') sent = 2;
                    else if (c == '/') sent = 3;
                    else if (c == ' ') sent = 4;
                    else if (c == '[') sent = 5;
                    else if (c == ']') sent = 6;
                    else if (c == 'h' || c == 'H') sent = 7;
                    else if (c == 'e' || c == 'E') sent = 8;
                    else if (c == 'p' || c == 'P') sent = 9;
                    else if (c == '5') sent = kPresenterF5;
                }
            }
#if defined(HAS_KEYBOARD)
            // Optional: Ok / shoulder still advances when no other key
            else if (check(SelPress)) sent = kPresenterNext;
#endif
        }

        if (sent >= 0) {
            if (sent == 9) s.pressMedia(KEY_MEDIA_PLAY_PAUSE);
            else if (sent == kPresenterF5) sendRawKey(s, KEY_F5);
            else sendRawKey(s, kPresenterHid[sent]);
            flashId = sent;
            flashUntil = millis() + 120;
            draw();
            delay(40);
        }

        delay(8);
    }

    s.releaseAll();
    return true;
}

static bool runPresenter(HidRemoteTransportSession &s, bool vertical) {
#if defined(HAS_SCREEN)
    if (vertical) {
        HidVertDisplayScope vertScope;
        return runPresenterLoop(s, vertical);
    }
#endif
    return runPresenterLoop(s, vertical);
}

static void keyboardMirrorChar(String &mirror, char c) {
    if (c == '\b') {
        if (mirror.length() > 0) mirror.remove(mirror.length() - 1);
    } else if (c >= 32 && c <= 126) {
        mirror += c;
        if (mirror.length() > 240) mirror.remove(0, mirror.length() - 240);
    }
}

static void keyboardPressStroke(HidRemoteTransportSession &s, const keyStroke &key, String &mirror) {
    if (s.keyboardHid == nullptr) return;

    if (key.alt) s.keyboardHid->press(KEY_LEFT_ALT);
    if (key.ctrl) s.keyboardHid->press(KEY_LEFT_CTRL);
    if (key.gui) s.keyboardHid->press(KEY_LEFT_GUI);

    if (key.enter) {
        s.keyboardHid->println();
        mirror += '\n';
        return;
    }
    if (key.del) {
        s.keyboardHid->press(KEYBACKSPACE);
        keyboardMirrorChar(mirror, '\b');
        return;
    }

    for (char k : key.word) {
        if (k == '`') {
            s.keyboardHid->press(KEY_ESC);
            mirror += "[Esc]";
        } else if ((uint8_t)k == KEY_DELETE) {
            s.keyboardHid->press(KEY_DELETE);
            mirror += "[Del]";
        } else if ((uint8_t)k == KEYTAB || (uint8_t)k == 0xB3) {
            s.keyboardHid->press(KEYTAB);
            mirror += "[Tab]";
        } else {
            uint8_t hidKey = (uint8_t)k;
            if (hidKey == KEY_OPT) hidKey = KEY_LEFT_GUI;
            s.keyboardHid->press(hidKey);
            if (hidKey >= KEY_LEFT_CTRL && hidKey <= KEY_LEFT_GUI) {
                if (hidKey == KEY_LEFT_GUI) mirror += "[Win]";
                else if (hidKey == KEY_LEFT_ALT) mirror += "[Alt]";
                else if (hidKey == KEY_LEFT_CTRL) mirror += "[Ctrl]";
            } else if (k >= 32 && k <= 126) {
                keyboardMirrorChar(mirror, k);
            }
        }
    }

    for (uint8_t mk : key.modifier_keys) {
        uint8_t hidKey = mk == KEY_OPT ? KEY_LEFT_GUI : mk;
        s.keyboardHid->press(hidKey);
    }
    for (uint8_t hk : key.hid_keys) {
        uint8_t hidKey = hk == KEY_OPT ? KEY_LEFT_GUI : hk;
        s.keyboardHid->press(hidKey);
    }
}

static bool handleKeyboardFn(HidRemoteTransportSession &s, const keyStroke &key, int &flashKey) {
    if (!key.fn || !key.pressed) return false;
    char c = strokeChar(key);
    if (c == 0) return true;

    uint8_t uc = (uint8_t)c;
    if (uc == KEY_UP_ARROW || uc == KEY_DOWN_ARROW || uc == KEY_LEFT_ARROW || uc == KEY_RIGHT_ARROW) {
        sendRawKey(s, uc);
        return true;
    }
    if (uc == KEY_ESC || c == '`') {
        sendRawKey(s, KEY_ESC);
        flashKey = '`';
        return true;
    }

    flashKey = (int)(unsigned char)c;
    switch (c) {
        case '1': sendRawKey(s, KEY_F1); break;
        case '2': sendRawKey(s, KEY_F2); break;
        case '3': sendRawKey(s, KEY_F3); break;
        case '4': sendRawKey(s, KEY_F4); break;
        case '5': sendRawKey(s, KEY_F5); break;
        case '6': sendRawKey(s, KEY_F6); break;
        case '7': sendRawKey(s, KEY_F7); break;
        case '8': sendRawKey(s, KEY_F8); break;
        case '9': sendRawKey(s, KEY_F9); break;
        case '0': sendRawKey(s, KEY_F10); break;
        case '-': sendRawKey(s, KEY_F11); break;
        case '=': sendRawKey(s, KEY_F12); break;
        case 'i':
        case 'I': sendRawKey(s, KEY_INSERT); break;
        case 'p':
        case 'P': sendRawKey(s, KEY_PRINT_SCREEN); break;
        case 'u':
        case 'U': sendRawKey(s, KEY_PAUSE); break;
        case 'h':
        case 'H': sendRawKey(s, KEY_HOME); break;
        case 'e':
        case 'E': sendRawKey(s, KEY_END); break;
        case '[': sendRawKey(s, KEY_PAGE_UP); break;
        case ']': sendRawKey(s, KEY_PAGE_DOWN); break;
        case 't':
        case 'T': sendCombo(s, KEY_LEFT_ALT, 0, KEYTAB); break;
        case 'w':
        case 'W': sendCombo(s, KEY_LEFT_GUI, 0, KEYTAB); break;
        case 'x':
        case 'X': sendCombo(s, KEY_LEFT_CTRL, KEY_LEFT_SHIFT, KEY_ESC); break;
        case 'd':
        case 'D': sendCombo(s, KEY_LEFT_CTRL, KEY_LEFT_ALT, KEY_DELETE); break;
        case 'n':
        case 'N': sendRawKey(s, KEY_NUM_LOCK); break;
        case 's':
        case 'S': sendRawKey(s, KEY_SCROLL_LOCK); break;
        case 'm':
        case 'M': sendRawKey(s, KEY_MENU); break;
        case 'l':
        case 'L': sendRawKey(s, KEY_DELETE); break;
        default: flashKey = 0; return false;
    }
    if (flashKey >= 'A' && flashKey <= 'Z') flashKey += 32;
    return true;
}

static void runSystemShortcutsMenu(HidRemoteTransportSession &s);

#if defined(HAS_KEYBOARD) && defined(ARDUINO_M5STACK_CARDPUTER)
extern bool UseTCA8418;
extern bool fn_key_pressed;
#endif

// Level-based FN (KeyStroke.fn is pulse-cleared and false-edges while held).
static bool pollFnHeld(const keyStroke &key) {
    if (key.fn) return true;
#if defined(HAS_KEYBOARD) && defined(ARDUINO_M5STACK_CARDPUTER)
    if (UseTCA8418) return fn_key_pressed;
    Keyboard.update();
    return Keyboard.keysState().fn;
#elif defined(HAS_KEYBOARD)
    Keyboard.update();
    return Keyboard.keysState().fn;
#else
    (void)key;
    return false;
#endif
}

static bool runKeyboard(HidRemoteTransportSession &s) {
#if !defined(HAS_KEYBOARD)
    tft.fillScreen(0x0841);
    hidRemoteDrawHeader(s.transport, s.isConnected(), "Keyboard");
    hidRemoteDrawStatus("Keyboard not available", nullptr);
    hidRemoteWaitBack();
    return true;
#endif

    String mirror;
    bool dirty = true;
    bool fnLayer = false;
    bool prevFnHeld = false;
    int fnFlash = 0;
    unsigned long fnFlashUntil = 0;

    auto redrawKb = [&]() {
        hidRemoteDrawHeader(s.transport, s.isConnected(), fnLayer ? "Keyboard FN" : "Keyboard");
        if (fnLayer) {
            hidDrawKeyboardFnPad(fnFlash);
            hidRemoteDrawFooter("tap key  FN=hide  fn+Ok");
        } else {
            tft.fillRect(0, 27, tftWidth, tftHeight - 27 - 18, 0x0841);
            tft.setTextSize(FP);
            tft.setTextColor(0x07E0, 0x0841);
            tft.setCursor(6, 30);
            tft.println(mirror.length() > 0 ? mirror : "_");
            hidRemoteDrawFooter("FN overlay  Opt+Ok shortcuts");
        }
        dirty = false;
    };

    redrawKb();

    while (true) {
        if (fnLayer && fnFlash && millis() > fnFlashUntil) {
            fnFlash = 0;
            dirty = true;
        }
        if (dirty) redrawKb();

        keyStroke key = _getKeyPress();
        if (checkModeExit(key)) break;

        const bool fnHeld = pollFnHeld(key);
        const bool fnEdge = fnHeld && !prevFnHeld;
        prevFnHeld = fnHeld;

        const char ch = strokeChar(key);
        const bool hasChar = key.pressed && ch != 0;

        // Sticky FN overlay: show until a key is used; FN rising edge toggles
        if (fnLayer || fnHeld) {
            if (hasChar) {
                keyStroke fnKey = key;
                fnKey.fn = true;
                int flash = 0;
                if (handleKeyboardFn(s, fnKey, flash)) {
                    const char *tag = keyboardFnLabel(flash);
                    if (tag != nullptr) mirrorAppendTag(mirror, tag);
                    else if ((uint8_t)ch == KEY_UP_ARROW) mirrorAppendTag(mirror, "[Up]");
                    else if ((uint8_t)ch == KEY_DOWN_ARROW) mirrorAppendTag(mirror, "[Down]");
                    else if ((uint8_t)ch == KEY_LEFT_ARROW) mirrorAppendTag(mirror, "[Left]");
                    else if ((uint8_t)ch == KEY_RIGHT_ARROW) mirrorAppendTag(mirror, "[Right]");
                    fnFlash = flash;
                    fnFlashUntil = millis() + 120;
                } else if (!fnHeld) {
                    if (s.keyboardHid == nullptr) break;
                    keyboardPressStroke(s, key, mirror);
                    s.keyboardHid->releaseAll();
                }
                fnLayer = false;
                dirty = true;
                delay(60);
                continue;
            }

            if (fnEdge) {
                fnLayer = !fnLayer;
                fnFlash = 0;
                dirty = true;
            }
            delay(8);
            continue;
        }

        if (!key.pressed) {
            delay(5);
            continue;
        }

        // Opt/Win + Ok opens system shortcuts without stealing Enter
        if (key.enter && key.gui && !key.fn) {
            runSystemShortcutsMenu(s);
            dirty = true;
            delay(120);
            continue;
        }

        if (s.keyboardHid == nullptr) break;

        keyboardPressStroke(s, key, mirror);
        s.keyboardHid->releaseAll();
        dirty = true;
        delay(20);
    }

    s.releaseAll();
    return true;
}

static void runSystemShortcutsMenu(HidRemoteTransportSession &s) {
    struct Shortcut {
        const char *label;
        void (*send)(HidRemoteTransportSession &);
    };

    static const Shortcut kShortcuts[] = {
        // Reused from Keyboard FN layer
        {"Alt+Tab", [](HidRemoteTransportSession &x) { sendCombo(x, KEY_LEFT_ALT, 0, KEYTAB); }},
        {"Win/Cmd+Tab", [](HidRemoteTransportSession &x) { sendCombo(x, KEY_LEFT_GUI, 0, KEYTAB); }},
        {"Ctrl+Shift+Esc",
         [](HidRemoteTransportSession &x) { sendCombo(x, KEY_LEFT_CTRL, KEY_LEFT_SHIFT, KEY_ESC); }},
        {"Ctrl+Alt+Del",
         [](HidRemoteTransportSession &x) { sendCombo(x, KEY_LEFT_CTRL, KEY_LEFT_ALT, KEY_DELETE); }},
        {"Alt+F4 close", [](HidRemoteTransportSession &x) { sendCombo(x, KEY_LEFT_ALT, 0, KEY_F4); }},
        {"Ctrl+Alt+Shift+V",
         [](HidRemoteTransportSession &x) {
             sendCombo3(x, KEY_LEFT_CTRL, KEY_LEFT_ALT, KEY_LEFT_SHIFT, 'v');
         }},
        // Clipboard / edit
        {"Ctrl+C copy", [](HidRemoteTransportSession &x) { sendCombo(x, KEY_LEFT_CTRL, 0, 'c'); }},
        {"Ctrl+V paste", [](HidRemoteTransportSession &x) { sendCombo(x, KEY_LEFT_CTRL, 0, 'v'); }},
        {"Ctrl+X cut", [](HidRemoteTransportSession &x) { sendCombo(x, KEY_LEFT_CTRL, 0, 'x'); }},
        {"Ctrl+A select all", [](HidRemoteTransportSession &x) { sendCombo(x, KEY_LEFT_CTRL, 0, 'a'); }},
        {"Ctrl+Z undo", [](HidRemoteTransportSession &x) { sendCombo(x, KEY_LEFT_CTRL, 0, 'z'); }},
        {"Ctrl+Y redo", [](HidRemoteTransportSession &x) { sendCombo(x, KEY_LEFT_CTRL, 0, 'y'); }},
        {"Ctrl+S save", [](HidRemoteTransportSession &x) { sendCombo(x, KEY_LEFT_CTRL, 0, 's'); }},
        {"Ctrl+F find", [](HidRemoteTransportSession &x) { sendCombo(x, KEY_LEFT_CTRL, 0, 'f'); }},
        // Window / desktop
        {"Win/Cmd+D desktop", [](HidRemoteTransportSession &x) { sendWinKey(x, 'd'); }},
        {"Win/Cmd+L lock", [](HidRemoteTransportSession &x) { sendWinKey(x, 'l'); }},
        {"Win/Cmd+E files", [](HidRemoteTransportSession &x) { sendWinKey(x, 'e'); }},
        {"Win/Cmd+R run", [](HidRemoteTransportSession &x) { sendWinKey(x, 'r'); }},
        {"Win/Cmd+V clipboard", [](HidRemoteTransportSession &x) { sendWinKey(x, 'v'); }},
        {"Win/Cmd+Shift+S snip",
         [](HidRemoteTransportSession &x) { sendCombo(x, KEY_LEFT_GUI, KEY_LEFT_SHIFT, 's'); }},
        {"Print Screen", [](HidRemoteTransportSession &x) { sendRawKey(x, KEY_PRINT_SCREEN); }},
        {"Alt+PrtSc",
         [](HidRemoteTransportSession &x) { sendCombo(x, KEY_LEFT_ALT, 0, KEY_PRINT_SCREEN); }},
        // Browser / tabs
        {"Ctrl+T new tab", [](HidRemoteTransportSession &x) { sendCombo(x, KEY_LEFT_CTRL, 0, 't'); }},
        {"Ctrl+W close tab", [](HidRemoteTransportSession &x) { sendCombo(x, KEY_LEFT_CTRL, 0, 'w'); }},
        {"Ctrl+Shift+T reopen",
         [](HidRemoteTransportSession &x) { sendCombo(x, KEY_LEFT_CTRL, KEY_LEFT_SHIFT, 't'); }},
        {"Ctrl+L address", [](HidRemoteTransportSession &x) { sendCombo(x, KEY_LEFT_CTRL, 0, 'l'); }},
        {"Alt+Left back",
         [](HidRemoteTransportSession &x) { sendCombo(x, KEY_LEFT_ALT, 0, KEY_LEFT_ARROW); }},
        {"Alt+Right forward",
         [](HidRemoteTransportSession &x) { sendCombo(x, KEY_LEFT_ALT, 0, KEY_RIGHT_ARROW); }},
        // Linux / Mac oriented
        {"Ctrl+Alt+T terminal",
         [](HidRemoteTransportSession &x) { sendCombo(x, KEY_LEFT_CTRL, KEY_LEFT_ALT, 't'); }},
        {"Cmd/Ctrl+Q quit", [](HidRemoteTransportSession &x) { sendCombo(x, KEY_LEFT_GUI, 0, 'q'); }},
        {"Cmd/Ctrl+Space spotlight",
         [](HidRemoteTransportSession &x) { sendCombo(x, KEY_LEFT_GUI, 0, ' '); }},
        {"Cmd+Opt+Esc force quit",
         [](HidRemoteTransportSession &x) { sendCombo(x, KEY_LEFT_GUI, KEY_LEFT_ALT, KEY_ESC); }},
    };

    int start = 0;
    while (true) {
        std::vector<Option> opts;
        opts.reserve((sizeof(kShortcuts) / sizeof(kShortcuts[0])) + 1);
        for (const auto &sc : kShortcuts) {
            opts.push_back({String(sc.label), []() {}});
        }
        opts.push_back({"Back", []() {}});

        int sel = loopOptions(opts, MENU_TYPE_SUBMENU, "System Shortcuts", start, false);
        if (sel < 0 || sel >= (int)(sizeof(kShortcuts) / sizeof(kShortcuts[0]))) return;
        start = sel;
        kShortcuts[sel].send(s);
        delay(80);
    }
}

static bool runSystemShortcuts(HidRemoteTransportSession &s) {
    runSystemShortcutsMenu(s);
    return true;
}

static bool handleMediaKey(HidRemoteTransportSession &s, const keyStroke &key, int &flashId) {
    if (!key.pressed) return false;

    char c = strokeChar(key);
    if (c == ' ') {
        s.pressMedia(KEY_MEDIA_PLAY_PAUSE);
        flashId = 13;
        return true;
    }
    if (c == ';') {
        s.pressMedia(KEY_MEDIA_VOLUME_UP);
        flashId = 0;
        return true;
    }
    if (c == '.') {
        s.pressMedia(KEY_MEDIA_VOLUME_DOWN);
        flashId = 1;
        return true;
    }
    if (c == 'm' || c == 'M') {
        s.pressMedia(KEY_MEDIA_MUTE);
        flashId = 2;
        return true;
    }
    if (c == ',') {
        s.pressMedia(KEY_MEDIA_PREVIOUS_TRACK);
        flashId = 3;
        return true;
    }
    if (c == '/') {
        s.pressMedia(KEY_MEDIA_NEXT_TRACK);
        flashId = 4;
        return true;
    }
    if (c == 's' || c == 'S') {
        s.pressMedia(KEY_MEDIA_STOP);
        flashId = 5;
        return true;
    }
    if (c == 'c' || c == 'C') {
        sendCombo(s, KEY_LEFT_CTRL, KEY_LEFT_SHIFT, 'm');
        flashId = 6;
        return true;
    }
    if (c == 'v' || c == 'V') {
        sendCtrlWinKey(s, 'v');
        flashId = 7;
        return true;
    }
    if (c == 'a' || c == 'A') {
        sendWinKey(s, 'a');
        flashId = 8;
        return true;
    }
    if (c == 'd' || c == 'D') {
        sendWinKey(s, 'p');
        flashId = 9;
        return true;
    }
    if (c == '`') {
        sendWinKey(s, 'd');
        flashId = 10;
        return true;
    }
    if (c == 'b' || c == 'B') {
        sendRawKey(s, KEY_LEFT_ARROW);
        flashId = 11;
        return true;
    }
    if (c == 'n' || c == 'N') {
        sendRawKey(s, KEY_RIGHT_ARROW);
        flashId = 12;
        return true;
    }
    return false;
}

static bool runMediaLayout(HidRemoteTransportSession &s, const char *title) {
    int flashId = -1;
    unsigned long flashUntil = 0;

    auto draw = [&]() {
        hidRemoteDrawHeader(s.transport, s.isConnected(), title);
        hidDrawMediaPad(flashId);
        hidRemoteDrawFooter("SPC play/pause  fn+Ok");
    };

    draw();

    while (true) {
        if (flashId >= 0 && millis() > flashUntil) {
            flashId = -1;
            draw();
        }

        keyStroke key = _getKeyPress();
        if (checkModeExit(key)) break;

        int fid = -1;
        if (handleMediaKey(s, key, fid)) {
            flashId = fid;
            flashUntil = millis() + 120;
            draw();
            delay(80);
        }

        delay(8);
    }

    s.releaseAll();
    return true;
}

static bool runMedia(HidRemoteTransportSession &s) { return runMediaLayout(s, "Media"); }

static bool runAppleMusic(HidRemoteTransportSession &s) { return runMediaLayout(s, "Apple Music"); }

static bool runMovie(HidRemoteTransportSession &s) { return runMediaLayout(s, "Movie"); }

static bool runMouse(HidRemoteTransportSession &s) {
    int sens = kvxConfig.hidRemoteMouseSensitivity;
    int flashId = -1;
    unsigned long flashUntil = 0;
    unitJoystick2Begin(true);
    const bool joy = unitJoystick2IsPresent();
    bool joyBtnHeld = false;
    bool joyRightSent = false;
    unsigned long joyBtnAt = 0;
    const unsigned long joyRightHoldMs = 400;

    auto draw = [&]() {
        hidRemoteDrawHeader(s.transport, s.isConnected(),
                            kvxConfig.hidRemoteJoyInvertY ? "Mouse Y-inv" : "Mouse");
        hidDrawMousePad(flashId, joy);
        hidRemoteDrawFooter(joy ? "D invert  hold=R  fn+Ok" : "fn+Ok back");
    };

    draw();

    while (true) {
        if (flashId >= 0 && millis() > flashUntil) {
            flashId = -1;
            draw();
        }

        keyStroke key = _getKeyPress();
        if (checkModeExit(key)) break;

        bool moved = false;
        if (check(UpPress)) {
            s.mouseMove(0, -sens);
            flashId = 0;
            moved = true;
        }
        if (check(DownPress)) {
            s.mouseMove(0, sens);
            flashId = 1;
            moved = true;
        }
        if (check(PrevPress)) {
            s.mouseMove(-sens, 0);
            flashId = 2;
            moved = true;
        }
        if (check(NextPress)) {
            s.mouseMove(sens, 0);
            flashId = 3;
            moved = true;
        }

        char c = strokeChar(key);
        if (c == 'd' || c == 'D') {
            kvxConfig.setHidRemoteJoyInvertY(!kvxConfig.hidRemoteJoyInvertY);
            moved = true;
        } else if (c == 'l' || c == 'L') {
            s.mouseClick(0x01);
            flashId = 4;
            moved = true;
        } else if (c == '\'') {
            s.mouseClick(0x02);
            flashId = 5;
            moved = true;
        }

        int8_t jx = 0, jy = 0;
        if (unitJoystick2ReadMove(jx, jy, sens)) {
            s.mouseMove(jx, jy);
            if (jx < 0) flashId = 2;
            else if (jx > 0) flashId = 3;
            if (jy < 0) flashId = 0;
            else if (jy > 0) flashId = 1;
            moved = true;
        }

        bool joyDown = unitJoystick2ButtonDown();
        if (joyDown && !joyBtnHeld) {
            joyBtnHeld = true;
            joyRightSent = false;
            joyBtnAt = millis();
        }
        if (joyDown && joyBtnHeld && !joyRightSent && (millis() - joyBtnAt) >= joyRightHoldMs) {
            s.mouseClick(0x02);
            joyRightSent = true;
            flashId = 5;
            moved = true;
        }
        if (!joyDown && joyBtnHeld) {
            if (!joyRightSent) {
                s.mouseClick(0x01);
                flashId = 4;
                moved = true;
            }
            joyBtnHeld = false;
        }

        int steps = drainRotarySteps();
        if (steps != 0) {
            s.mouseMove(0, 0, (int8_t)(steps > 0 ? 1 : -1));
            flashId = 6;
            moved = true;
        }

        if (moved) {
            flashUntil = millis() + 100;
            draw();
        }

        delay(12);
    }

    return true;
}

static bool runShorts(HidRemoteTransportSession &s) {
    int flashId = -1;
    unsigned long flashUntil = 0;
    int remapStep = 0; // 0 run, 1 wait up, 2 wait down
    uint8_t pendingUp = kvxConfig.hidRemoteShortsUp;

    auto prompt = [&]() -> const char * {
        if (remapStep == 1) return "Press key for UP";
        if (remapStep == 2) return "Press key for DOWN";
        return nullptr;
    };

    auto draw = [&]() {
        hidRemoteDrawHeader(s.transport, s.isConnected(), "Shorts");
        hidDrawShortsPad((char)kvxConfig.hidRemoteShortsUp, (char)kvxConfig.hidRemoteShortsDown, flashId, prompt());
        hidRemoteDrawFooter(remapStep ? "fn+Ok cancel" : "Ok remap  fn+Ok back");
    };

    draw();

    while (true) {
        if (flashId >= 0 && millis() > flashUntil) {
            flashId = -1;
            draw();
        }

        keyStroke key = _getKeyPress();
        if (checkModeExit(key)) {
            if (remapStep) {
                remapStep = 0;
                draw();
                continue;
            }
            break;
        }

        if (remapStep) {
            if (!key.pressed) {
                delay(8);
                continue;
            }
            char c = strokeChar(key);
            if (c == 0) {
                delay(8);
                continue;
            }
            if (key.enter || key.fn) {
                delay(8);
                continue;
            }
            if (remapStep == 1) {
                pendingUp = (uint8_t)c;
                remapStep = 2;
            } else {
                kvxConfig.setHidRemoteShortsKeys(pendingUp, (uint8_t)c);
                remapStep = 0;
            }
            delay(80);
            draw();
            continue;
        }

        if (key.enter && key.pressed) {
            remapStep = 1;
            flashId = 3;
            draw();
            delay(80);
            continue;
        }

        char c = strokeChar(key);
        if (key.pressed && c == (char)kvxConfig.hidRemoteShortsDown) {
            sendRawKey(s, KEY_DOWN_ARROW);
            flashId = 1;
            flashUntil = millis() + 120;
            draw();
            delay(80);
        } else if (key.pressed && c == (char)kvxConfig.hidRemoteShortsUp) {
            sendRawKey(s, KEY_UP_ARROW);
            flashId = 0;
            flashUntil = millis() + 120;
            draw();
            delay(80);
        } else if (c == ' ' && key.pressed) {
            sendRawKey(s, ' ');
            flashId = 2;
            flashUntil = millis() + 120;
            draw();
            delay(80);
        }
        delay(8);
    }
    s.releaseAll();
    return true;
}

static void drawClickerPanel(bool running, unsigned long clicks, bool full) {
    if (full) {
        hidClearContentArea();
        tft.setTextSize(1);
        tft.setTextColor(0x07E0, 0x0841);
        tft.drawString("Delay (-/=): " + String(kvxConfig.hidRemoteClickerDelay) + "ms", 8, 32);
        const char *btn = "LEFT";
        if (kvxConfig.hidRemoteClickerButton == 1) btn = "RIGHT";
        else if (kvxConfig.hidRemoteClickerButton == 2) btn = "MID";
        tft.drawString("Button (b): " + String(btn), 8, 48);
        tft.drawString("Space: start/stop", 8, 64);
    }
    tft.fillRect(8, 78, tftWidth - 16, 28, 0x0841);
    tft.setTextColor(running ? 0x9818 : 0x07E0, 0x0841);
    tft.drawString(running ? "RUNNING" : "STOPPED", 8, 80);
    tft.setTextColor(0x07E0, 0x0841);
    tft.drawString("Clicks: " + String(clicks), 8, 94);
}

static bool runClicker(HidRemoteTransportSession &s) {
    bool running = false;
    unsigned long lastClick = 0;
    unsigned long clicks = 0;
    uint8_t btn = 0x01;
    if (kvxConfig.hidRemoteClickerButton == 1) btn = 0x02;
    else if (kvxConfig.hidRemoteClickerButton == 2) btn = 0x04;

    hidRemoteDrawHeader(s.transport, s.isConnected(), "Auto Clicker");
    drawClickerPanel(running, clicks, true);
    hidRemoteDrawFooter("fn+Ok back");

    while (true) {
        keyStroke key = _getKeyPress();
        if (checkModeExit(key)) break;

        char c = strokeChar(key);
        if (c == ' ' && key.pressed) {
            running = !running;
            drawClickerPanel(running, clicks, false);
        } else if ((c == '-' || c == '_') && key.pressed) {
            kvxConfig.setHidRemoteClickerDelay(kvxConfig.hidRemoteClickerDelay - 50);
            drawClickerPanel(running, clicks, true);
        } else if ((c == '=' || c == '+') && key.pressed) {
            kvxConfig.setHidRemoteClickerDelay(kvxConfig.hidRemoteClickerDelay + 50);
            drawClickerPanel(running, clicks, true);
        } else if ((c == 'b' || c == 'B') && key.pressed) {
            kvxConfig.setHidRemoteClickerButton((kvxConfig.hidRemoteClickerButton + 1) % 3);
            if (kvxConfig.hidRemoteClickerButton == 1) btn = 0x02;
            else if (kvxConfig.hidRemoteClickerButton == 2) btn = 0x04;
            else btn = 0x01;
            drawClickerPanel(running, clicks, true);
        }

        if (running && millis() - lastClick >= (unsigned long)kvxConfig.hidRemoteClickerDelay) {
            s.mouseClick(btn);
            clicks++;
            lastClick = millis();
            drawClickerPanel(running, clicks, false);
        }

        delay(5);
    }
    return true;
}

static void drawJigglerPanel(bool stealth, bool running, bool full) {
    if (full) {
        hidClearContentArea();
        tft.setTextSize(1);
        tft.setTextColor(0x07E0, 0x0841);
        if (stealth) {
            tft.drawString("Min (-/=): " + String(kvxConfig.hidRemoteStealthMin) + "s", 8, 32);
            tft.drawString("Max (+): " + String(kvxConfig.hidRemoteStealthMax) + "s", 8, 48);
        } else {
            tft.drawString("Interval (-/=): " + String(kvxConfig.hidRemoteJigglerInterval) + "s", 8, 32);
        }
        tft.drawString("Space: start/stop", 8, stealth ? 64 : 48);
    }
    tft.fillRect(8, stealth ? 78 : 62, tftWidth - 16, 16, 0x0841);
    tft.setTextColor(running ? 0x9818 : 0x07E0, 0x0841);
    tft.drawString(running ? "ACTIVE" : "PAUSED", 8, stealth ? 80 : 64);
}

static bool runJiggler(HidRemoteTransportSession &s, bool stealth) {
    bool running = false;
    unsigned long nextMove = 0;

    hidRemoteDrawHeader(s.transport, s.isConnected(), stealth ? "Stealth Jiggler" : "Mouse Jiggler");
    drawJigglerPanel(stealth, running, true);
    hidRemoteDrawFooter("fn+Ok back");

    while (true) {
        keyStroke key = _getKeyPress();
        if (checkModeExit(key)) break;

        char c = strokeChar(key);
        if (c == ' ' && key.pressed) {
            running = !running;
            if (running) nextMove = millis();
            drawJigglerPanel(stealth, running, false);
        } else if ((c == '-' || c == '_') && key.pressed) {
            if (stealth) kvxConfig.setHidRemoteStealthMin(kvxConfig.hidRemoteStealthMin - 5);
            else kvxConfig.setHidRemoteJigglerInterval(kvxConfig.hidRemoteJigglerInterval - 5);
            drawJigglerPanel(stealth, running, true);
        } else if ((c == '=' || c == '+') && key.pressed) {
            if (stealth) {
                kvxConfig.setHidRemoteStealthMax(kvxConfig.hidRemoteStealthMax + 10);
            } else {
                kvxConfig.setHidRemoteJigglerInterval(kvxConfig.hidRemoteJigglerInterval + 5);
            }
            drawJigglerPanel(stealth, running, true);
        }

        if (running && millis() >= nextMove) {
            if (stealth) {
                int dx = random(-2, 3);
                int dy = random(-2, 3);
                if (dx == 0 && dy == 0) dx = 1;
                s.mouseMove((int8_t)dx, (int8_t)dy);
                nextMove = millis() + random(kvxConfig.hidRemoteStealthMin, kvxConfig.hidRemoteStealthMax + 1) * 1000UL;
            } else {
                s.mouseMove(2, 0);
                delay(20);
                s.mouseMove(-2, 0);
                nextMove = millis() + (unsigned long)kvxConfig.hidRemoteJigglerInterval * 1000UL;
            }
        }

        delay(8);
    }
    return true;
}

static void sendPttCombo(HidRemoteTransportSession &s, bool down) {
    if (s.keyboardHid == nullptr) return;
    if (down) {
        s.keyboardHid->press(KEY_LEFT_CTRL);
        s.keyboardHid->press(KEY_LEFT_SHIFT);
        s.keyboardHid->press('m');
    } else {
        s.keyboardHid->releaseAll();
    }
}

static bool runPushToTalk(HidRemoteTransportSession &s) {
    bool talking = false;

    auto draw = [&]() {
        hidRemoteDrawHeader(s.transport, s.isConnected(), "Push-to-Talk");
        hidDrawPttPad(talking);
        hidRemoteDrawFooter("fn+Ok back");
    };

    draw();

    while (true) {
        keyStroke key = _getKeyPress();
        if (checkModeExit(key)) break;

        bool spaceDown = (strokeChar(key) == ' ' && key.pressed);
        if (spaceDown && !talking) {
            talking = true;
            sendPttCombo(s, true);
            draw();
        }
        if (!spaceDown && talking && !key.pressed) {
            talking = false;
            sendPttCombo(s, false);
            draw();
        }
        delay(10);
    }
    if (talking) sendPttCombo(s, false);
    return true;
}

bool hidRemoteRunMode(HidRemoteMode mode, HidRemoteTransportSession &session) {
    switch (mode) {
        case HID_MODE_PRESENTER: return runPresenter(session, false);
        case HID_MODE_PRESENTER_VERT: return runPresenter(session, true);
        case HID_MODE_KEYBOARD: return runKeyboard(session);
        case HID_MODE_MEDIA: return runMedia(session);
        case HID_MODE_APPLE_MUSIC: return runAppleMusic(session);
        case HID_MODE_MOVIE: return runMovie(session);
        case HID_MODE_MOUSE: return runMouse(session);
        case HID_MODE_SHORTS: return runShorts(session);
        case HID_MODE_CLICKER: return runClicker(session);
        case HID_MODE_JIGGLER: return runJiggler(session, false);
        case HID_MODE_JIGGLER_STEALTH: return runJiggler(session, true);
        case HID_MODE_PUSH_TO_TALK: return runPushToTalk(session);
        case HID_MODE_SHORTCUTS: return runSystemShortcuts(session);
        default: return false;
    }
}
