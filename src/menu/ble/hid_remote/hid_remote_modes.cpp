#include "hid_remote_modes.h"
#include "hid_remote_ui.h"
#include "root/input/mykeyboard.h"
#include "root/app/utils.h"
#include <pins_arduino.h>
#include <globals.h>
#include <keys.h>

static const HidRemoteModeInfo kModeTable[HID_MODE_COUNT] = {
    {"Presenter",           HID_CAP_KEYBOARD},
    {"Presenter Vertical",    HID_CAP_KEYBOARD},
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
};

const HidRemoteModeInfo &hidRemoteModeInfo(HidRemoteMode mode) {
    if (mode >= HID_MODE_COUNT) return kModeTable[0];
    return kModeTable[mode];
}

static bool checkModeExit(const keyStroke &key) { return key.pressed && key.fn && key.exit_key; }

static void sendRawKey(HidRemoteTransportSession &s, uint8_t hidKey) {
    if (s.keyboardHid == nullptr) return;
    s.keyboardHid->press(hidKey);
    delay(30);
    s.keyboardHid->releaseAll();
}

static void sendCombo(HidRemoteTransportSession &s, uint8_t mod1, uint8_t mod2, uint8_t key) {
    if (s.keyboardHid == nullptr) return;
    s.keyboardHid->press(mod1);
    if (mod2) s.keyboardHid->press(mod2);
    s.keyboardHid->press(key);
    delay(30);
    s.keyboardHid->releaseAll();
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

static bool runPresenterLoop(HidRemoteTransportSession &s, bool vertical) {
    int flashId = -1;
    unsigned long flashUntil = 0;

    auto draw = [&]() {
        hidRemoteDrawHeader(s.transport, s.isConnected(), vertical ? "Presenter V" : "Presenter");
        hidDrawPresenterPad(vertical, flashId);
        hidRemoteDrawFooter("fn+Ok back");
    };

    draw();

    while (true) {
        if (flashId >= 0 && millis() > flashUntil) {
            flashId = -1;
            draw();
        }

        keyStroke key = _getKeyPress();
        if (checkModeExit(key)) break;

        int sent = -1;
        int arrowFlash = -1;
        if (strokeHasArrow(key, KEY_UP_ARROW, arrowFlash)) sent = 0;
        else if (strokeHasArrow(key, KEY_DOWN_ARROW, arrowFlash)) sent = 1;
        else if (strokeHasArrow(key, KEY_LEFT_ARROW, arrowFlash)) sent = 2;
        else if (strokeHasArrow(key, KEY_RIGHT_ARROW, arrowFlash)) sent = 3;
        else {
            char c = strokeChar(key);
            if (c == ' ') sent = 4;
            else if (c == '[') sent = 5;
            else if (c == ']') sent = 6;
            else if (c == 'h' || c == 'H') sent = 7;
            else if (c == 'e' || c == 'E') sent = 8;
        }

        if (sent >= 0 && key.pressed) {
            sendRawKey(s, kPresenterHid[sent]);
            flashId = sent;
            flashUntil = millis() + 120;
            draw();
            delay(80);
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

    auto redrawKb = [&]() {
        hidRemoteDrawHeader(s.transport, s.isConnected(), "Keyboard");
        tft.fillRect(0, 27, tftWidth, tftHeight - 27 - 18, 0x0841);
        tft.setTextSize(FP);
        tft.setTextColor(0x07E0, 0x0841);
        tft.setCursor(6, 30);
        tft.println(mirror.length() > 0 ? mirror : "_");
        hidRemoteDrawFooter("fn+Ok back");
        dirty = false;
    };

    redrawKb();

    while (true) {
        if (dirty) redrawKb();

        keyStroke key = _getKeyPress();
        if (checkModeExit(key)) break;
        if (!key.pressed) {
            delay(5);
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

static bool handleMediaKey(HidRemoteTransportSession &s, const keyStroke &key, int &flashId) {
    if (!key.pressed) return false;

    char c = strokeChar(key);
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
        hidRemoteDrawFooter("fn+Ok back");
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

    auto draw = [&]() {
        hidRemoteDrawHeader(s.transport, s.isConnected(), "Mouse");
        hidDrawMousePad(flashId);
        hidRemoteDrawFooter("fn+Ok back");
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
        if (c == 'l' || c == 'L') {
            s.mouseClick(0x01);
            flashId = 4;
            moved = true;
        } else if (c == '\'') {
            s.mouseClick(0x02);
            flashId = 5;
            moved = true;
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
    hidRemoteDrawHeader(s.transport, s.isConnected(), "Shorts");
    hidClearContentArea();
    tft.setTextColor(0x07E0, 0x0841);
    tft.drawCentreString("; /  prev/next video", tftWidth / 2, 50, 1);
    tft.drawCentreString("Space = play/pause", tftWidth / 2, 70, 1);
    hidRemoteDrawFooter("fn+Ok back");

    while (true) {
        keyStroke key = _getKeyPress();
        if (checkModeExit(key)) break;
        char c = strokeChar(key);
        if (c == '.' || check(NextPress)) {
            sendRawKey(s, KEY_DOWN_ARROW);
            delay(100);
        } else if (c == ';' || check(PrevPress)) {
            sendRawKey(s, KEY_UP_ARROW);
            delay(100);
        } else if (c == ' ' && key.pressed) {
            sendRawKey(s, ' ');
            delay(100);
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
    hidRemoteDrawHeader(s.transport, s.isConnected(), "Push-to-Talk");
    hidClearContentArea();
    tft.setTextColor(0x07E0, 0x0841);
    tft.drawCentreString("Hold Space to talk", tftWidth / 2, 50, 1);
    tft.drawCentreString("Ctrl+Shift+M", tftWidth / 2, 70, 1);
    hidRemoteDrawFooter("fn+Ok back");

    bool talking = false;
    while (true) {
        keyStroke key = _getKeyPress();
        if (checkModeExit(key)) break;

        bool spaceDown = (strokeChar(key) == ' ' && key.pressed);
        if (spaceDown && !talking) {
            talking = true;
            sendPttCombo(s, true);
        }
        if (!spaceDown && talking && !key.pressed) {
            talking = false;
            sendPttCombo(s, false);
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
        default: return false;
    }
}
