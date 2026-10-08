#include "pc_connect.h"

#include "root/input/mykeyboard.h"
#include "root/net/wifi_common.h"
#include "root/serial/serial_commands/gpio_commands.h"
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
bool g_lineOverflow = false;

static const size_t kMaxLine = 2048;

const char *radioName(PcRadio r) {
    switch (r) {
        case PcRadio::Wifi: return "wifi";
        case PcRadio::Ble: return "ble";
        case PcRadio::Ir: return "ir";
        case PcRadio::Rf: return "rf";
        case PcRadio::Rfid: return "rfid";
        default: return "idle";
    }
}

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
        padprintln(String("Radio: ") + radioName(g_radio));
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
    serialDevice->println(line);
}

void handleHello() {
    g_linked = true;
    g_uiDirty = true;
    String mac = deviceMac();
    String out = "{\"evt\":\"hello\",\"name\":\"kvxputer\",\"mac\":\"";
    out += mac;
    out += "\",\"ready\":true,\"apps\":[";
#if !defined(LITE_VERSION)
    out += "\"wifi.analyzer\",\"ble.scan\",\"gpio\",\"ir.rx\",\"rf.rx\",\"rf.rssi\",\"rfid.read\",\"jam."
           "detect\"";
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

#if !defined(LITE_VERSION)
void handleGpio(const String &rest) {
    // gpio read|mode|set …
    if (pcConnectRadio() != PcRadio::Idle) {
        emitErr("gpio", "radio busy");
        return;
    }
    int sp = rest.indexOf(' ');
    String op = sp < 0 ? rest : rest.substring(0, sp);
    String args = sp < 0 ? String() : rest.substring(sp + 1);
    op.toLowerCase();
    args.trim();

    if (op == "read") {
        int pin = args.toInt();
        if (!is_free_gpio_pin(pin)) {
            emitErr("gpio.read", "pin not allowed");
            return;
        }
        int val = digitalRead(pin);
        JsonDocument doc;
        doc["evt"] = "ack";
        doc["cmd"] = "gpio.read";
        doc["ok"] = true;
        doc["pin"] = pin;
        doc["value"] = val;
        String out;
        serializeJson(doc, out);
        emitRaw(out);
        return;
    }
    if (op == "mode") {
        int pin = -1, mode = -1;
        if (sscanf(args.c_str(), "%d %d", &pin, &mode) != 2 || mode < 0 || mode > 9 ||
            !is_free_gpio_pin(pin)) {
            emitErr("gpio.mode", "invalid args");
            return;
        }
        pinMode(pin, mode);
        emitAck("gpio.mode", true);
        return;
    }
    if (op == "set") {
        int pin = -1, value = -1;
        if (sscanf(args.c_str(), "%d %d", &pin, &value) != 2 || value < 0 || value > 1 ||
            !is_free_gpio_pin(pin)) {
            emitErr("gpio.set", "invalid args");
            return;
        }
        digitalWrite(pin, value);
        emitAck("gpio.set", true);
        return;
    }
    emitErr("gpio", "use read|mode|set");
}
#endif

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

    int sp = line.indexOf(' ');
    String cmd = sp < 0 ? line : line.substring(0, sp);
    String rest = sp < 0 ? String() : line.substring(sp + 1);
    rest.trim();
    cmd.toLowerCase();

#if !defined(LITE_VERSION)
    if (cmd == "gpio") {
        handleGpio(rest);
        return;
    }

    if (cmd == "wifi.analyzer") {
        if (rest.startsWith("start")) {
            uint16_t dwell = 350;
            uint8_t lockCh = 0;
            String args = rest.substring(5);
            args.trim();
            if (args.length()) {
                int sp2 = args.indexOf(' ');
                if (sp2 < 0) {
                    dwell = (uint16_t)args.toInt();
                } else {
                    dwell = (uint16_t)args.substring(0, sp2).toInt();
                    String chArg = args.substring(sp2 + 1);
                    chArg.trim();
                    if (chArg.length()) lockCh = (uint8_t)chArg.toInt();
                }
            }
            if (dwell < 150) dwell = 150;
            if (dwell > 1000) dwell = 1000;
            if (lockCh > 11) lockCh = 0;
            if (pcConnectWifiAnalyzerStart(dwell, lockCh)) {
                JsonDocument doc;
                doc["evt"] = "ack";
                doc["cmd"] = "wifi.analyzer";
                doc["ok"] = true;
                doc["dwell"] = dwell;
                doc["ch"] = lockCh;
                String out;
                serializeJson(doc, out);
                emitRaw(out);
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
    if (cmd == "ir.rx") {
        if (rest.startsWith("start")) {
            bool raw = rest.indexOf("raw") >= 0;
            if (pcConnectIrRxStart(raw)) {
                emitAck("ir.rx", true);
                paintStatus(true);
            } else {
                emitErr("ir.rx", "start failed");
            }
            return;
        }
    }
    if (cmd == "ir.tx") {
        // ir.tx <protocol> <address> <command>
        int sp1 = rest.indexOf(' ');
        if (sp1 < 0) {
            emitErr("ir.tx", "need protocol address command");
            return;
        }
        String protocol = rest.substring(0, sp1);
        String rem = rest.substring(sp1 + 1);
        rem.trim();
        int sp2 = rem.indexOf(' ');
        if (sp2 < 0) {
            emitErr("ir.tx", "need protocol address command");
            return;
        }
        String address = rem.substring(0, sp2);
        String command = rem.substring(sp2 + 1);
        command.trim();
        if (pcConnectIrTx(protocol, address, command)) emitAck("ir.tx", true);
        else emitErr("ir.tx", "tx failed");
        return;
    }
    if (cmd == "ir.tx_raw") {
        // ir.tx_raw <freq> <samples…>
        int sp1 = rest.indexOf(' ');
        if (sp1 < 0) {
            emitErr("ir.tx_raw", "need freq samples");
            return;
        }
        uint32_t freq = (uint32_t)rest.substring(0, sp1).toInt();
        String samples = rest.substring(sp1 + 1);
        samples.trim();
        if (!freq || !samples.length()) {
            emitErr("ir.tx_raw", "invalid args");
            return;
        }
        if (pcConnectIrTxRaw(freq, samples)) emitAck("ir.tx_raw", true);
        else emitErr("ir.tx_raw", "tx failed");
        return;
    }
    if (cmd == "rf.rx") {
        if (rest.startsWith("start")) {
            String args = rest.substring(5);
            args.trim();
            bool raw = false;
            float mhz = 0;
            if (args.length()) {
                int sp2 = args.indexOf(' ');
                if (sp2 < 0) {
                    if (args.equalsIgnoreCase("raw")) raw = true;
                    else mhz = args.toFloat();
                } else {
                    String a0 = args.substring(0, sp2);
                    String a1 = args.substring(sp2 + 1);
                    a1.trim();
                    if (a0.equalsIgnoreCase("raw")) {
                        raw = true;
                        mhz = a1.toFloat();
                    } else {
                        mhz = a0.toFloat();
                        if (a1.equalsIgnoreCase("raw")) raw = true;
                    }
                }
            }
            if (pcConnectRfRxStart(mhz, raw)) {
                emitAck("rf.rx", true);
                paintStatus(true);
            } else {
                emitErr("rf.rx", "start failed");
            }
            return;
        }
    }
    if (cmd == "rf.rssi") {
        if (rest.startsWith("start")) {
            if (pcConnectRfRssiStart()) {
                emitAck("rf.rssi", true);
                paintStatus(true);
            } else {
                emitErr("rf.rssi", "CC1101 required");
            }
            return;
        }
    }
    if (cmd == "rf.tx") {
        if (pcConnectRfTxLast()) emitAck("rf.tx", true);
        else emitErr("rf.tx", "no capture");
        return;
    }
    if (cmd == "rfid.read") {
        if (rest.startsWith("start")) {
            if (pcConnectRfidStart()) {
                emitAck("rfid.read", true);
                paintStatus(true);
            } else {
                emitErr("rfid.read", "module not found");
            }
            return;
        }
    }
    if (cmd == "jam.detect") {
        if (rest.startsWith("start")) {
            uint32_t thr = (uint32_t)kvxConfig.jamDetectAlertPerSec;
            String args = rest.substring(5);
            args.trim();
            if (args.length()) thr = (uint32_t)args.toInt();
            if (pcConnectJamStart(thr)) {
                JsonDocument doc;
                doc["evt"] = "ack";
                doc["cmd"] = "jam.detect";
                doc["ok"] = true;
                doc["threshold"] = thr < 5 ? 5 : (thr > 250 ? 250 : thr);
                String out;
                serializeJson(doc, out);
                emitRaw(out);
                paintStatus(true);
            } else {
                emitErr("jam.detect", "start failed");
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
            if (g_lineOverflow) {
                g_lineBuf = "";
                g_lineOverflow = false;
                emitErr("line", "too long");
                continue;
            }
            String line = g_lineBuf;
            g_lineBuf = "";
            handleLine(line);
        } else {
            if (g_lineBuf.length() < kMaxLine) {
                g_lineBuf += c;
            } else {
                g_lineOverflow = true;
            }
        }
    }
}

void tickRadio() {
#if !defined(LITE_VERSION)
    if (pcConnectWifiAnalyzerActive()) pcConnectWifiAnalyzerTick();
    if (pcConnectBleScanActive()) pcConnectBleScanTick();
    if (pcConnectIrRxActive()) pcConnectIrRxTick();
    if (pcConnectRfRxActive()) pcConnectRfRxTick();
    if (pcConnectRfRssiActive()) pcConnectRfRssiTick();
    if (pcConnectRfidActive()) pcConnectRfidTick();
    if (pcConnectJamActive()) pcConnectJamTick();
#endif
#if defined(EVIL_EXTENSIONS)
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
    if (g_radio != radio || g_mode != (mode ? mode : "idle")) g_uiDirty = true;
    g_radio = radio;
    g_mode = mode ? mode : "idle";
    g_channel = channel;
}

void pcConnectStopRadio() {
#if !defined(LITE_VERSION)
    pcConnectWifiAnalyzerStop();
    pcConnectBleScanStop();
    pcConnectIrRxStop();
    pcConnectRfRxStop();
    pcConnectRfRssiStop();
    pcConnectRfidStop();
    pcConnectJamStop();
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
    g_lineOverflow = false;

    pcConnectOwnsSerial = true;
    if (serialcmdsTaskHandle) vTaskSuspend(serialcmdsTaskHandle);
    while (Serial && Serial.available()) { Serial.read(); }
    g_lineBuf = "";

    paintStatus(true);

    while (!forceHome) {
        if (check(EscPress)) break;
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
