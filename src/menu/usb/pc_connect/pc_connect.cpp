#include "pc_connect.h"

#include "root/input/mykeyboard.h"
#include "root/net/wifi_common.h"
#include "root/serial/serialcmds.h"
#include "root/ui/display.h"
#include "root/ui/kvx_ui.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <globals.h>

volatile bool pcConnectOwnsSerial = false;

namespace {

bool g_linked = false;
PcRadio g_radio = PcRadio::Idle;
String g_mode = "idle";
uint8_t g_channel = 0;
uint32_t g_evtCount = 0;
uint32_t g_evtWindowStart = 0;
float g_evtPerSec = 0;
bool g_uiDirty = true;
bool g_chromeDrawn = false;
float g_paintedEvt = -1;
uint8_t g_paintedCh = 255;
bool g_paintedLinked = false;
PcRadio g_paintedRadio = PcRadio::Idle;
String g_paintedMode;
String g_lineBuf;

String deviceMac() {
    String m = WiFi.macAddress();
    if (m.length() && m != "00:00:00:00:00:00") return m;
    uint64_t chip = ESP.getEfuseMac();
    char buf[18];
    snprintf(
        buf,
        sizeof(buf),
        "%02X:%02X:%02X:%02X:%02X:%02X",
        (uint8_t)(chip >> 40),
        (uint8_t)(chip >> 32),
        (uint8_t)(chip >> 24),
        (uint8_t)(chip >> 16),
        (uint8_t)(chip >> 8),
        (uint8_t)chip
    );
    return String(buf);
}

int16_t statusBodyY() { return BORDER_PAD_Y + FM * LH + 4; }

int16_t lineY(int indexAfterBlank) {
    // index 0 = blank line under title; 1 = Link; 2 = Radio; 3 = Mode; 4 = Ch or Evt
    return statusBodyY() + indexAfterBlank * uiLineH(FP);
}

void paintFieldLine(int lineIdx, const String &text) {
    const int lineH = uiLineH(FP);
    int16_t y = lineY(lineIdx);
    tft.fillRect(BORDER_PAD_X, y, tftWidth - 2 * BORDER_PAD_X, lineH, kvxConfig.bgColor);
    tft.setTextSize(FP);
    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    tft.setCursor(BORDER_PAD_X, y);
    tft.print(text);
}

void paintStatus(bool force = false) {
    const bool layoutNeedsCh = (g_radio == PcRadio::Wifi && g_channel != 0);
    const bool paintedHadCh = (g_paintedRadio == PcRadio::Wifi && g_paintedCh != 0 && g_paintedCh != 255);
    const bool majorChange =
        force || g_uiDirty || !g_chromeDrawn || g_linked != g_paintedLinked || g_radio != g_paintedRadio ||
        g_mode != g_paintedMode || layoutNeedsCh != paintedHadCh;

    if (majorChange) {
        g_uiDirty = false;
        g_chromeDrawn = true;
        drawMainBorderWithTitle("PC Connect", true);
        tft.setTextSize(FP);
        tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
        tft.setCursor(BORDER_PAD_X, statusBodyY());
        padprintln("");
        padprintln(String("Link: ") + (g_linked ? "yes" : "waiting"));
        padprintln(
            String("Radio: ") +
            (g_radio == PcRadio::Wifi ? "wifi" : g_radio == PcRadio::Ble ? "ble" : "idle")
        );
        padprintln(String("Mode: ") + g_mode);
        if (layoutNeedsCh) padprintln(String("Ch: ") + String(g_channel));
        padprintln(String("Evt/s: ") + String(g_evtPerSec, 1));
        padprintln("");
        padprintln("Esc = leave");
        g_paintedLinked = g_linked;
        g_paintedRadio = g_radio;
        g_paintedMode = g_mode;
        g_paintedCh = g_channel;
        g_paintedEvt = g_evtPerSec;
        return;
    }

    // In-place updates only (no full clear → no jitter).
    if (layoutNeedsCh && g_channel != g_paintedCh) {
        paintFieldLine(4, String("Ch: ") + String(g_channel));
        g_paintedCh = g_channel;
    }
    if (g_evtPerSec != g_paintedEvt) {
        int evtLine = layoutNeedsCh ? 5 : 4;
        paintFieldLine(evtLine, String("Evt/s: ") + String(g_evtPerSec, 1));
        g_paintedEvt = g_evtPerSec;
    }
}

void emitRaw(const String &line) {
    if (!serialDevice) return;
    // Do not gate on Serial's bool operator — USB-Serial-JTAG can report
    // "not connected" spuriously and would swallow hello/ack replies.
    serialDevice->println(line);
}

void handleHello() {
    g_linked = true;
    g_uiDirty = true;
    // Avoid large JsonDocument during USB reopen; keep this on the stack and small.
    String mac = deviceMac();
    String out = "{\"evt\":\"hello\",\"name\":\"kvxputer\",\"mac\":\"";
    out += mac;
    out += "\",\"ready\":true,\"apps\":[";
#if !defined(LITE_VERSION)
    out += "\"wifi.analyzer\",\"ble.scan\"";
#if defined(EVIL_EXTENSIONS)
    out += ",";
#endif
#endif
#if defined(EVIL_EXTENSIONS)
    out += "\"ble.flipper\",\"ble.airtag\",\"ble.skimmer\"";
#endif
    out += "]}";
    emitRaw(out);
    paintStatus(true);
}

void emitAck(const char *cmd, bool ok, const char *msg = nullptr) {
    JsonDocument doc;
    doc["evt"] = ok ? "ack" : "err";
    doc["cmd"] = cmd;
    doc["ok"] = ok;
    if (msg) doc["msg"] = msg;
    String out;
    serializeJson(doc, out);
    emitRaw(out);
}

void emitErr(const char *cmd, const char *msg) { emitAck(cmd, false, msg); }

void handleStop() {
    pcConnectStopRadio();
    g_uiDirty = true;
    emitAck("stop", true);
    paintStatus(true);
}

void handleLine(String line) {
    line.trim();
    if (!line.length()) return;

    if (line.equalsIgnoreCase("hello")) {
        handleHello();
        return;
    }
    if (line.equalsIgnoreCase("stop")) {
        handleStop();
        return;
    }

    // token + optional args
    int sp = line.indexOf(' ');
    String cmd = sp < 0 ? line : line.substring(0, sp);
    String rest = sp < 0 ? String() : line.substring(sp + 1);
    rest.trim();

    cmd.toLowerCase();

#if !defined(LITE_VERSION)
    if (cmd == "wifi.analyzer") {
        if (rest.startsWith("start")) {
            uint16_t dwell = 350;
            int sp2 = rest.indexOf(' ');
            if (sp2 >= 0) {
                String arg = rest.substring(sp2 + 1);
                arg.trim();
                if (arg.length()) dwell = (uint16_t)arg.toInt();
            }
            if (pcConnectWifiAnalyzerStart(dwell)) {
                emitAck("wifi.analyzer", true);
                paintStatus(true);
            } else {
                emitErr("wifi.analyzer", "start failed");
            }
            return;
        }
    }
    if (cmd == "ble.scan") {
        if (rest.startsWith("start")) {
            if (pcConnectBleScanStart()) {
                emitAck("ble.scan", true);
                paintStatus(true);
            } else {
                emitErr("ble.scan", "start failed");
            }
            return;
        }
    }
#endif

#if defined(EVIL_EXTENSIONS)
    if (cmd == "ble.flipper") {
        if (rest.startsWith("start")) {
            if (pcConnectBleFlipperStart()) {
                emitAck("ble.flipper", true);
                paintStatus(true);
            } else {
                emitErr("ble.flipper", "start failed");
            }
            return;
        }
    }
    if (cmd == "ble.airtag") {
        if (rest.startsWith("start")) {
            if (pcConnectBleAirtagStart()) {
                emitAck("ble.airtag", true);
                paintStatus(true);
            } else {
                emitErr("ble.airtag", "start failed");
            }
            return;
        }
    }
    if (cmd == "ble.skimmer") {
        if (rest.startsWith("start")) {
            if (pcConnectBleSkimmerStart()) {
                emitAck("ble.skimmer", true);
                paintStatus(true);
            } else {
                emitErr("ble.skimmer", "start failed");
            }
            return;
        }
    }
#endif

    emitErr(cmd.c_str(), "unknown or unavailable");
}

void pollSerial() {
    if (!serialDevice) return;
    while (serialDevice->available()) {
        char c = (char)serialDevice->read();
        if (c == '\r') continue;
        if (c == '\n') {
            String line = g_lineBuf;
            g_lineBuf = "";
            handleLine(line);
        } else {
            if (g_lineBuf.length() < 256) g_lineBuf += c;
        }
    }
}

void tickRadio() {
#if !defined(LITE_VERSION)
    if (pcConnectWifiAnalyzerActive()) pcConnectWifiAnalyzerTick();
    if (pcConnectBleScanActive()) pcConnectBleScanTick();
#endif
#if defined(EVIL_EXTENSIONS)
    // Flipper/airtag/skimmer share the BLE scan tick path via mode name
    if (g_radio == PcRadio::Ble) {
        if (g_mode == "ble.flipper") pcConnectBleFlipperTick();
        else if (g_mode == "ble.airtag") pcConnectBleAirtagTick();
        else if (g_mode == "ble.skimmer") pcConnectBleSkimmerTick();
    }
#endif
}

void updateEvtRate() {
    uint32_t now = millis();
    if (now - g_evtWindowStart >= 1000) {
        g_evtPerSec = (float)g_evtCount * 1000.0f / (float)(now - g_evtWindowStart);
        g_evtCount = 0;
        g_evtWindowStart = now;
    }
}

} // namespace

void pcConnectEmitJson(const String &jsonLine) {
    emitRaw(jsonLine);
    pcConnectNoteEvent();
}

void pcConnectNoteEvent() {
    g_evtCount++;
    updateEvtRate();
}

void pcConnectPollSerial() { pollSerial(); }

PcRadio pcConnectRadio() { return g_radio; }
const char *pcConnectModeName() { return g_mode.c_str(); }
uint8_t pcConnectChannel() { return g_channel; }
bool pcConnectLinked() { return g_linked; }

void pcConnectSetStatus(PcRadio radio, const char *mode, uint8_t channel) {
    // Radio/mode changes need a full layout pass; channel-only updates are in-place.
    if (g_radio != radio || g_mode != (mode ? mode : "idle")) g_uiDirty = true;
    g_radio = radio;
    g_mode = mode ? mode : "idle";
    g_channel = channel;
}

void pcConnectStopRadio() {
#if !defined(LITE_VERSION)
    pcConnectWifiAnalyzerStop();
    pcConnectBleScanStop();
#endif
    if (g_radio != PcRadio::Idle || g_mode != "idle" || g_channel != 0) g_uiDirty = true;
    g_radio = PcRadio::Idle;
    g_mode = "idle";
    g_channel = 0;
}

void pcConnectMenu() {
    returnToMenu = false;
    forceHome = false;
    g_linked = false;
    g_radio = PcRadio::Idle;
    g_mode = "idle";
    g_channel = 0;
    g_evtCount = 0;
    g_evtWindowStart = millis();
    g_evtPerSec = 0;
    g_paintedEvt = -1;
    g_paintedCh = 255;
    g_paintedLinked = false;
    g_paintedRadio = PcRadio::Idle;
    g_paintedMode = "";
    g_chromeDrawn = false;
    g_uiDirty = true;
    g_lineBuf = "";

    // Claim the port before suspending CLI so a racing hello cannot call backToMenu().
    pcConnectOwnsSerial = true;
    if (serialcmdsTaskHandle) vTaskSuspend(serialcmdsTaskHandle);
    // Drain any bytes the CLI may have been mid-reading.
    while (Serial && Serial.available()) { Serial.read(); }
    g_lineBuf = "";

    paintStatus(true);

    while (!forceHome) {
        if (check(EscPress)) break;
        // Ignore returnToMenu from other tasks while we own the session.
        returnToMenu = false;
        pollSerial();
        tickRadio();
        updateEvtRate();
        paintStatus(false);
        delay(10);
    }

    pcConnectStopRadio();
    emitRaw("{\"evt\":\"bye\"}");
    g_linked = false;

    pcConnectOwnsSerial = false;
    if (serialcmdsTaskHandle) vTaskResume(serialcmdsTaskHandle);

    returnToMenu = true;
}
