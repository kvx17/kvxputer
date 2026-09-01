#include "modules_menu.h"
#include "root/ui/display.h"
#include "root/hal/pahub.h"
#include "root/input/unit_scroll.h"
#include "root/input/unit_joystick2.h"
#include "root/config/configPins.h"
#include <globals.h>

static void pahubMaybeSetRfid(PahubDevice dev) {
    RFIDModules cur = (RFIDModules)kvxConfigPins.rfidModule;
    bool i2cReader = (cur == M5_RFID2_MODULE || cur == PN532_I2C_MODULE);
    if (!i2cReader) return;
    if (dev == PahubDevRFID2) kvxConfigPins.setRfidModule(M5_RFID2_MODULE);
    else if (dev == PahubDevNFC) kvxConfigPins.setRfidModule(PN532_I2C_MODULE);
}

static void pahubMaybeSetRf(PahubDevice dev) {
    if (dev == PahubDevRF433R && kvxConfigPins.rfModule != CC1101_SPI_MODULE) {
        kvxConfigPins.setRfModule(M5_RF_MODULE);
    }
}

static String pahubChannelLabel(uint8_t ch) {
    PahubDevice dev = (PahubDevice)kvxConfig.pahubChannels[ch];
    String s = "Ch" + String(ch) + ": " + pahubDeviceName(dev);
    String hint = pahubScanHint(ch);
    if (hint.length()) {
        s += " ";
        s += hint;
    }
    return s;
}

void ModulesMenu::optionsMenu() {
    returnToMenu = false;
    while (true) {
        if (returnToMenu) {
            returnToMenu = false;
            return;
        }

        std::vector<Option> localOptions = {
#if defined(UNIT_SCROLL)
            {"Unit Scroll", [this]() { unitScrollMenu(); }},
#endif
#if defined(UNIT_JOYSTICK2)
            {"Unit Joystick", [this]() { unitJoystick2Menu(); }},
#endif
            {"PaHub", [this]() { pahubMenu(); }},
            {"Main Menu", []() {}},
        };

        int selected = loopOptions(localOptions, MENU_TYPE_SUBMENU, "Modules");
        if (selected == -1 || selected == (int)localOptions.size() - 1) return;
    }
}

void ModulesMenu::unitScrollMenu() {
    while (true) {
        String status = unitScrollStatusLabel();
        String groveNote = grovePortABusy() ? " (Grove busy)" : "";
        std::vector<Option> localOptions = {
            {status + groveNote, []() {}},
            {"Reconnect / Search",
             []() {
                 displayInfo("Searching direct + PaHub...");
                 bool ok = unitScrollReconnect();
                 displayInfo(ok ? "Unit Scroll connected" : "Unit Scroll not found", true);
             }},
            {String("Probe at boot: ") + (kvxConfig.unitScrollEnabled ? "ON" : "OFF"),
             []() {
                 kvxConfig.setUnitScrollEnabled(!kvxConfig.unitScrollEnabled);
                 if (kvxConfig.unitScrollEnabled) unitScrollReconnect();
             }},
            {String("Invert direction: ") + (kvxConfig.unitScrollInvert ? "ON" : "OFF"),
             []() { kvxConfig.setUnitScrollInvert(!kvxConfig.unitScrollInvert); }},
            {"Back", []() {}},
        };

        int selected = loopOptions(localOptions, MENU_TYPE_SUBMENU, "Unit Scroll");
        if (selected == -1 || selected == (int)localOptions.size() - 1) return;
    }
}

void ModulesMenu::unitJoystick2Menu() {
    while (true) {
        String status = unitJoystick2StatusLabel();
        std::vector<Option> localOptions = {
            {status, []() {}},
            {"Reconnect / Search",
             []() {
                 displayInfo("Searching direct + PaHub...");
                 bool ok = unitJoystick2Reconnect();
                 displayInfo(ok ? "Joystick connected" : "Joystick not found", true);
             }},
            {"Back", []() {}},
        };

        int selected = loopOptions(localOptions, MENU_TYPE_SUBMENU, "Unit Joystick");
        if (selected == -1 || selected == (int)localOptions.size() - 1) return;
    }
}

void ModulesMenu::pahubChannelMenu(uint8_t ch) {
    PahubDevice cur = (PahubDevice)kvxConfig.pahubChannels[ch];
    PahubDevice choices[] = {
        PahubDevNone,
        PahubDevRFID2,
        PahubDevNFC,
        PahubDevScroll,
        PahubDevJoystick2,
        PahubDevRF433R,
    };
    std::vector<Option> localOptions;
    for (PahubDevice d : choices) {
        localOptions.push_back(
            {pahubDeviceName(d),
             [this, ch, d]() {
                 if (!kvxConfig.setPahubChannel(ch, d)) {
                     displayWarning("Already assigned to another channel", true);
                     return;
                 }
                 pahubMaybeSetRfid(d);
                 pahubMaybeSetRf(d);
             },
             cur == d}
        );
    }
    localOptions.push_back({"Back", []() {}});
    String title = "PaHub Ch" + String(ch);
    loopOptions(localOptions, MENU_TYPE_SUBMENU, title.c_str());
}

void ModulesMenu::pahubScanMenu() {
    displayInfo("Scanning PaHub channels...");
    PahubScanResult results[PAHUB_CH_COUNT];
    bool ok = pahubScanAll(results);
    if (!ok && !pahubIsConnected()) {
        displayError("PaHub not found", true);
        return;
    }

    std::vector<PahubDevice> fingerprints(PAHUB_CH_COUNT);
    String mismatch;
    for (uint8_t ch = 0; ch < PAHUB_CH_COUNT; ch++) {
        fingerprints[ch] = results[ch].fingerprint;
        PahubDevice assigned = (PahubDevice)kvxConfig.pahubChannels[ch];
        if (assigned != PahubDevNone && results[ch].fingerprint != PahubDevNone &&
            results[ch].fingerprint != assigned) {
            if (mismatch.length()) mismatch += ", ";
            mismatch += "ch" + String(ch);
        }
    }
    if (mismatch.length()) displayWarning("Scan mismatch " + mismatch, true);

    while (true) {
        std::vector<Option> localOptions;
        for (uint8_t ch = 0; ch < PAHUB_CH_COUNT; ch++) {
            String row = "Ch" + String(ch) + ": ";
            String hint = pahubScanHint(ch);
            row += hint.length() ? hint : "(empty)";
            localOptions.push_back({row, []() {}});
        }
        localOptions.push_back(
            {"Apply detected",
             [this, fingerprints]() {
                 int applied = 0;
                 for (uint8_t ch = 0; ch < PAHUB_CH_COUNT; ch++) {
                     PahubDevice assigned = (PahubDevice)kvxConfig.pahubChannels[ch];
                     PahubDevice fp = fingerprints[ch];
                     if (fp == PahubDevNone) continue;
                     if (assigned != PahubDevNone) continue;
                     if (!kvxConfig.setPahubChannel(ch, fp)) continue;
                     pahubMaybeSetRfid(fp);
                     pahubMaybeSetRf(fp);
                     applied++;
                 }
                 displayInfo(String("Applied ") + applied + " channel(s)", true);
             }}
        );
        localOptions.push_back({"Back", []() {}});
        int selected = loopOptions(localOptions, MENU_TYPE_SUBMENU, "PaHub Scan");
        if (selected == -1 || selected == (int)localOptions.size() - 1) return;
    }
}

void ModulesMenu::pahubMenu() {
    pahubReconnect();
    while (true) {
        String groveNote = grovePortABusy() && !pahubIsConnected() ? " (Grove busy)" : "";
        char addrBuf[8];
        snprintf(addrBuf, sizeof(addrBuf), "0x%02X", kvxConfig.pahubAddr);
        std::vector<Option> localOptions = {
            {pahubStatusLabel() + groveNote, []() {}},
            {"Reconnect PaHub",
             []() {
                 bool ok = pahubReconnect();
                 displayInfo(ok ? "PaHub connected" : "PaHub not found", true);
             }},
            {String("Probe at boot: ") + (kvxConfig.pahubEnabled ? "ON" : "OFF"),
             []() {
                 kvxConfig.setPahubEnabled(!kvxConfig.pahubEnabled);
                 if (kvxConfig.pahubEnabled) pahubReconnect();
             }},
            {String("Address: ") + addrBuf,
             []() {
                 std::vector<Option> addrs;
                 uint8_t cur = kvxConfig.pahubAddr;
                 int idx = 0;
                 for (uint8_t a = 0x70; a <= 0x77; a++) {
                     char b[8];
                     snprintf(b, sizeof(b), "0x%02X", a);
                     if (a == cur) idx = (int)addrs.size();
                     uint8_t addr = a;
                     addrs.push_back(
                         {b,
                          [addr]() {
                              kvxConfig.setPahubAddr(addr);
                              pahubReconnect();
                          },
                          a == cur}
                     );
                 }
                 addrs.push_back({"Back", []() {}});
                 loopOptions(addrs, MENU_TYPE_SUBMENU, "PaHub Address", idx);
             }},
        };
        for (uint8_t ch = 0; ch < PAHUB_CH_COUNT; ch++) {
            uint8_t channel = ch;
            localOptions.push_back({pahubChannelLabel(ch), [this, channel]() { pahubChannelMenu(channel); }});
        }
        localOptions.push_back({"Scan channels", [this]() { pahubScanMenu(); }});
        localOptions.push_back({"Back", []() {}});

        int selected = loopOptions(localOptions, MENU_TYPE_SUBMENU, "PaHub");
        if (selected == -1 || selected == (int)localOptions.size() - 1) return;
    }
}

void ModulesMenu::drawIcon(float scale) {
    clearIconArea();
    int w = (int)(scale * 14);
    int h = (int)(scale * 10);
    int gap = (int)(scale * 3);
    int x0 = iconCenterX - w - gap / 2;
    int y0 = iconCenterY - h / 2;
    tft.drawRect(x0, y0, w, h, kvxConfig.priColor);
    tft.drawRect(x0 + w + gap, y0, w, h, kvxConfig.priColor);
    tft.drawRect(x0, y0 + h + gap, w, h, kvxConfig.priColor);
    tft.fillRect(x0 + w + gap, y0 + h + gap, w, h, kvxConfig.priColor);
}
