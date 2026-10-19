/**
 * @file pc_connect_nfc.cpp
 * PC Connect NDJSON NFC toolkit. Behavior inspired by Flipper NFC tools; reimplemented.
 */
#include "pc_connect.h"

#if !defined(LITE_VERSION)
#include "menu/rfid/RFIDInterface.h"
#include "menu/rfid/rfid_shared.h"
#include "root/hal/bus_HAL.h"
#include "root/hal/pahub.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <globals.h>
#include <vector>

namespace {

enum class NfcLive : uint8_t { Idle = 0, Read = 1, Field = 2, Emulate = 3 };

bool g_session = false;
NfcLive g_live = NfcLive::Idle;
RFIDInterface *g_rfid = nullptr;
PahubChannelGuard *g_pahub = nullptr;
String g_lastUid;
uint32_t g_lastPoll = 0;
uint32_t g_lastHitMs = 0;
bool g_missedOnce = false;

TaskHandle_t g_emuTask = nullptr;
volatile bool g_emuRunning = false;
volatile int g_emuResult = RFIDInterface::FAILURE;
volatile bool g_emuFinished = false;

static constexpr size_t kChunk = 1400;

void emitDoc(JsonDocument &doc) {
    String out;
    serializeJson(doc, out);
    pcConnectEmitJson(out);
}

void emitAckCmd(const char *cmd, bool ok, const char *msg = nullptr) {
    JsonDocument doc;
    doc["evt"] = ok ? "ack" : "err";
    doc["cmd"] = cmd;
    doc["ok"] = ok;
    if (msg) doc["msg"] = msg;
    emitDoc(doc);
}

void emitStatus(const char *op, const char *state, const char *detail = nullptr) {
    JsonDocument doc;
    doc["evt"] = "nfc.status";
    doc["op"] = op;
    doc["state"] = state;
    if (detail) doc["detail"] = detail;
    emitDoc(doc);
}

bool tryOpenHf(HfDomain domain) {
    holdI2CBus();
    g_pahub = openHfPahubGuard(domain);
    g_rfid = createHfModule(domain);
    if (g_rfid && g_rfid->begin()) return true;
    delete g_rfid;
    g_rfid = nullptr;
    delete g_pahub;
    g_pahub = nullptr;
    releaseI2CBusHold();
    return false;
}

void releaseModule() {
    delete g_rfid;
    g_rfid = nullptr;
    delete g_pahub;
    g_pahub = nullptr;
    releaseI2CBusHold();
}

bool ensureModule() {
    if (g_rfid) return true;
    // Open a fresh NFC session (stops other PC Connect radios first).
    return pcConnectNfcStart();
}

void emitTagInfo(const char *evt = "nfc") {
    if (!g_rfid) return;
    JsonDocument doc;
    doc["evt"] = evt;
    doc["uid"] = g_rfid->printableUID.uid;
    doc["type"] = g_rfid->printableUID.picc_type;
    doc["sak"] = g_rfid->printableUID.sak;
    doc["atqa"] = g_rfid->printableUID.atqa;
    doc["pages"] = g_rfid->totalPages;
    doc["pages_ok"] = g_rfid->pageReadSuccess;
    emitDoc(doc);
}

void emitDumpChunks(const String &uid) {
    if (!g_rfid) return;
    const String &dump = g_rfid->strAllPages;
    if (!dump.length()) {
        JsonDocument doc;
        doc["evt"] = "nfc.dump";
        doc["uid"] = uid;
        doc["offset"] = 0;
        doc["text"] = "";
        doc["done"] = true;
        emitDoc(doc);
        return;
    }
    size_t off = 0;
    while (off < dump.length()) {
        size_t n = dump.length() - off;
        if (n > kChunk) n = kChunk;
        JsonDocument doc;
        doc["evt"] = "nfc.dump";
        doc["uid"] = uid;
        doc["offset"] = (int)off;
        doc["text"] = dump.substring(off, off + n);
        doc["done"] = (off + n >= dump.length());
        emitDoc(doc);
        off += n;
        pcConnectPollSerial();
        yield();
    }
}

String argValue(const String &rest, const char *key) {
    String needle = String(key) + "=";
    int i = rest.indexOf(needle);
    if (i < 0) return String();
    int start = i + needle.length();
    int end = rest.indexOf(' ', start);
    String v = end < 0 ? rest.substring(start) : rest.substring(start, end);
    v.trim();
    // Unquote
    if (v.length() >= 2 && v[0] == '"' && v[v.length() - 1] == '"') {
        v = v.substring(1, v.length() - 1);
    }
    return v;
}

String argTailAfter(const String &rest, const char *key) {
    String needle = String(key) + "=";
    int i = rest.indexOf(needle);
    if (i < 0) return String();
    return rest.substring(i + needle.length());
}

bool pathAllowed(const String &path) {
    if (!path.length() || path.indexOf("..") >= 0) return false;
    if (!path.startsWith("/support_files/rfid")) return false;
    return true;
}

int readWithTimeout(uint32_t timeoutMs) {
    uint32_t deadline = millis() + timeoutMs;
    int result = RFIDInterface::TAG_NOT_PRESENT;
    while (millis() < deadline) {
        pcConnectPollSerial();
        result = g_rfid->read();
        if (result == RFIDInterface::SUCCESS) break;
        if (result == RFIDInterface::FAILURE) break;
        delay(40);
        yield();
    }
    return result;
}

void stopLive() {
    g_live = NfcLive::Idle;
    g_lastUid = "";
    g_missedOnce = false;
}

void requestStopEmulate() {
    // Drivers exit listen loops on EscPress (ST25 / PN532).
    EscPress = true;
}

void waitEmulateDone(uint32_t timeoutMs = 8000) {
    uint32_t t0 = millis();
    while (g_emuRunning && (millis() - t0) < timeoutMs) {
        vTaskDelay(pdMS_TO_TICKS(20));
        yield();
    }
}

void emuTaskFn(void * /*arg*/) {
    int rc = RFIDInterface::FAILURE;
    if (g_rfid) {
        returnToMenu = false;
        EscPress = false;
        rc = g_rfid->emulate();
    }
    g_emuResult = rc;
    g_emuFinished = true;
    g_emuRunning = false;
    g_emuTask = nullptr;
    vTaskDelete(nullptr);
}

bool startEmulateAsync(const char *detail) {
    if (!g_rfid) return false;
    if (g_emuRunning) {
        requestStopEmulate();
        waitEmulateDone();
    }
    stopLive();
    returnToMenu = false;
    EscPress = false;
    g_emuFinished = false;
    g_emuRunning = true;
    g_live = NfcLive::Emulate;
    pcConnectSetStatus(PcRadio::Rfid, "nfc.emulate", 0);
    emitStatus("emulate", "armed", detail);
    BaseType_t ok = xTaskCreatePinnedToCore(
        emuTaskFn, "pc_nfc_emu", 12288, nullptr, 1, &g_emuTask, 1
    );
    if (ok != pdPASS) {
        g_emuRunning = false;
        g_live = NfcLive::Idle;
        g_emuTask = nullptr;
        emitStatus("emulate", "error", "task create failed");
        return false;
    }
    return true;
}

void finishEmulateIfDone() {
    if (g_live != NfcLive::Emulate || !g_emuFinished) return;
    g_emuFinished = false;
    int rc = g_emuResult;
    g_live = NfcLive::Idle;
    if (rc == RFIDInterface::SUCCESS) emitStatus("emulate", "reader_hit", nullptr);
    else if (rc == RFIDInterface::TAG_NOT_PRESENT) emitStatus("emulate", "stopped", "timeout/esc");
    else emitStatus("emulate", "error", nullptr);
    if (g_session) pcConnectSetStatus(PcRadio::Rfid, "nfc.manage", 0);
}

void handleFiles(const String &rest) {
    if (rest.startsWith("list")) {
        String dir = argValue(rest, "dir");
        if (!dir.length()) {
            int sp = rest.indexOf(' ');
            if (sp >= 0) {
                dir = rest.substring(sp + 1);
                dir.trim();
                if (dir.startsWith("dir=")) dir = dir.substring(4);
            }
        }
        if (!dir.length()) dir = kvx::paths::RFID;
        if (!pathAllowed(dir)) {
            emitAckCmd("nfc.files", false, "path not allowed");
            return;
        }
        FS *fs = nullptr;
        if (!getFsStorage(fs) || !fs) {
            emitAckCmd("nfc.files", false, "no storage");
            return;
        }
        if (!fs->exists(dir)) {
            emitAckCmd("nfc.files", false, "dir missing");
            return;
        }
        File d = fs->open(dir);
        if (!d || !d.isDirectory()) {
            emitAckCmd("nfc.files", false, "not a directory");
            return;
        }
        JsonDocument doc;
        doc["evt"] = "nfc.files";
        doc["dir"] = dir;
        JsonArray arr = doc["files"].to<JsonArray>();
        File f = d.openNextFile();
        int count = 0;
        while (f && count < 80) {
            if (!f.isDirectory()) {
                JsonObject o = arr.add<JsonObject>();
                String name = String(f.name());
                int slash = name.lastIndexOf('/');
                if (slash >= 0) name = name.substring(slash + 1);
                o["name"] = name;
                o["path"] = String(dir) + "/" + name;
                o["size"] = (int)f.size();
                count++;
            }
            f.close();
            f = d.openNextFile();
        }
        d.close();
        emitAckCmd("nfc.files", true);
        emitDoc(doc);
        return;
    }

    if (rest.startsWith("read")) {
        String path = argValue(rest, "path");
        if (!path.length()) {
            int sp = rest.indexOf(' ');
            if (sp >= 0) path = rest.substring(sp + 1);
            path.trim();
            if (path.startsWith("path=")) path = path.substring(5);
        }
        if (!pathAllowed(path)) {
            emitAckCmd("nfc.files", false, "path not allowed");
            return;
        }
        FS *fs = nullptr;
        if (!getFsStorage(fs) || !fs) {
            emitAckCmd("nfc.files", false, "no storage");
            return;
        }
        File f = fs->open(path, FILE_READ);
        if (!f) {
            emitAckCmd("nfc.files", false, "open failed");
            return;
        }
        emitAckCmd("nfc.files", true);
        size_t off = 0;
        while (f.available()) {
            String chunk;
            chunk.reserve(kChunk + 8);
            while (f.available() && chunk.length() < (int)kChunk) {
                chunk += (char)f.read();
            }
            JsonDocument doc;
            doc["evt"] = "nfc.file";
            doc["path"] = path;
            doc["offset"] = (int)off;
            doc["text"] = chunk;
            doc["done"] = !f.available();
            emitDoc(doc);
            off += chunk.length();
            pcConnectPollSerial();
            yield();
        }
        f.close();
        if (off == 0) {
            JsonDocument doc;
            doc["evt"] = "nfc.file";
            doc["path"] = path;
            doc["offset"] = 0;
            doc["text"] = "";
            doc["done"] = true;
            emitDoc(doc);
        }
        return;
    }

    if (rest.startsWith("delete")) {
        String path = argValue(rest, "path");
        if (!path.length()) {
            int sp = rest.indexOf(' ');
            if (sp >= 0) path = rest.substring(sp + 1);
            path.trim();
            if (path.startsWith("path=")) path = path.substring(5);
        }
        if (!pathAllowed(path)) {
            emitAckCmd("nfc.files", false, "path not allowed");
            return;
        }
        FS *fs = nullptr;
        if (!getFsStorage(fs) || !fs) {
            emitAckCmd("nfc.files", false, "no storage");
            return;
        }
        if (!fs->exists(path)) {
            emitAckCmd("nfc.files", false, "not found");
            return;
        }
        if (!fs->remove(path)) {
            emitAckCmd("nfc.files", false, "delete failed");
            return;
        }
        emitAckCmd("nfc.files", true, "deleted");
        JsonDocument doc;
        doc["evt"] = "nfc.status";
        doc["op"] = "files";
        doc["state"] = "deleted";
        doc["path"] = path;
        emitDoc(doc);
        return;
    }

    emitAckCmd("nfc.files", false, "use list|read|delete");
}

void handleNdef(const String &rest) {
    if (!ensureModule()) {
        emitAckCmd("nfc.ndef", false, "module not found");
        return;
    }
    bool emulate = rest.startsWith("emulate");
    bool write = rest.startsWith("write");
    if (!emulate && !write) {
        emitAckCmd("nfc.ndef", false, "use write|emulate");
        return;
    }
    String type = argValue(rest, "type");
    if (!type.length()) type = "url";
    type.toLowerCase();
    String value = argTailAfter(rest, "value");
    // strip type= if it appears after value by mistake — value is last
    int vpos = rest.indexOf("value=");
    if (vpos >= 0) value = rest.substring(vpos + 6);
    value.trim();
    if (!value.length()) {
        value = (type == "text") ? String("kvxputer") : String("https://github.com/kvx17/kvxputer");
    }
    g_rfid->buildNdefMessage(type, value);
    if (write) {
        emitAckCmd("nfc.ndef.write", true);
        emitStatus("ndef.write", "waiting", "place tag");
        int rc = g_rfid->write_ndef();
        emitAckCmd("nfc.ndef.write", rc == RFIDInterface::SUCCESS, g_rfid->statusMessage(rc).c_str());
        return;
    }
    // emulate (async so host can send stop / q)
    g_rfid->emuMode = "t4t";
    emitAckCmd("nfc.ndef.emulate", true);
    if (!startEmulateAsync("T4T NDEF")) emitAckCmd("nfc.ndef.emulate", false, "emulate start failed");
}

void handleIso15693(const String &rest) {
    if (!ensureModule()) {
        emitAckCmd("nfc.iso15693", false, "module not found");
        return;
    }
    if (rest.startsWith("inventory") || rest.length() == 0) {
        String uid;
        int rc = g_rfid->iso15693_inventory(uid);
        if (rc == RFIDInterface::NOT_IMPLEMENTED) {
            rc = g_rfid->read_as(RFIDInterface::READ_AS_ISO15693);
            uid = g_rfid->printableUID.uid;
        }
        emitAckCmd("nfc.iso15693", rc == RFIDInterface::SUCCESS, g_rfid->statusMessage(rc).c_str());
        if (rc == RFIDInterface::SUCCESS) {
            emitTagInfo();
            emitDumpChunks(uid);
        }
        return;
    }
    if (rest.startsWith("read")) {
        uint8_t block = (uint8_t)argValue(rest, "block").toInt();
        uint8_t data[8] = {};
        int rc = g_rfid->iso15693_read_block(block, data, sizeof(data));
        emitAckCmd("nfc.iso15693", rc == RFIDInterface::SUCCESS, g_rfid->statusMessage(rc).c_str());
        if (rc == RFIDInterface::SUCCESS) {
            char hex[20];
            sprintf(hex, "%02X%02X%02X%02X", data[0], data[1], data[2], data[3]);
            JsonDocument doc;
            doc["evt"] = "nfc.status";
            doc["op"] = "iso15693";
            doc["state"] = "block";
            doc["block"] = block;
            doc["hex"] = hex;
            emitDoc(doc);
        }
        return;
    }
    if (rest.startsWith("write")) {
        uint8_t block = (uint8_t)argValue(rest, "block").toInt();
        String hex = argValue(rest, "hex");
        hex.replace(" ", "");
        if (hex.length() < 8) {
            emitAckCmd("nfc.iso15693", false, "need hex=8 digits");
            return;
        }
        uint8_t data[4];
        for (int i = 0; i < 4; i++)
            data[i] = (uint8_t)strtoul(hex.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);
        int rc = g_rfid->iso15693_write_block(block, data, 4);
        emitAckCmd("nfc.iso15693", rc == RFIDInterface::SUCCESS, g_rfid->statusMessage(rc).c_str());
        return;
    }
    emitAckCmd("nfc.iso15693", false, "use inventory|read|write");
}

void handleMagic(const String &rest) {
    if (!ensureModule()) {
        emitAckCmd("nfc.magic", false, "module not found");
        return;
    }
    if (rest.startsWith("probe") || rest.length() == 0) {
        auto t = g_rfid->magic_probe();
        const char *name = "none";
        if (t == RFIDInterface::MAGIC_GEN1) name = "gen1";
        else if (t == RFIDInterface::MAGIC_GEN2) name = "gen2";
        else if (t == RFIDInterface::MAGIC_GEN4) name = "gen4";
        else if (t == RFIDInterface::MAGIC_UNKNOWN) name = "unknown";
        bool ok = (t != RFIDInterface::MAGIC_NONE);
        emitAckCmd("nfc.magic", ok, ok ? name : "not magic / unsupported");
        JsonDocument doc;
        doc["evt"] = "nfc.status";
        doc["op"] = "magic";
        doc["state"] = name;
        emitDoc(doc);
        return;
    }
    if (rest.startsWith("uid")) {
        int rc = g_rfid->magic_write_uid();
        emitAckCmd("nfc.magic", rc == RFIDInterface::SUCCESS, g_rfid->statusMessage(rc).c_str());
        return;
    }
    if (rest.startsWith("dump")) {
        int rc = g_rfid->magic_write_dump();
        emitAckCmd("nfc.magic", rc == RFIDInterface::SUCCESS, g_rfid->statusMessage(rc).c_str());
        return;
    }
    emitAckCmd("nfc.magic", false, "use probe|uid|dump");
}

} // namespace

bool pcConnectNfcActive() { return g_session || g_live != NfcLive::Idle; }

bool pcConnectNfcStart() {
    // Stop other radios but keep our start after stop clears us.
    pcConnectStopRadio();
    if (!tryOpenHf(HfDomain::Nfc) && !tryOpenHf(HfDomain::Rfid)) return false;
    g_session = true;
    g_live = NfcLive::Idle;
    g_lastUid = "";
    g_lastPoll = 0;
    g_lastHitMs = 0;
    g_missedOnce = false;
    g_emuFinished = false;
    pcConnectSetStatus(PcRadio::Rfid, "nfc.manage", 0);
    return true;
}

void pcConnectNfcStop() {
    if (g_emuRunning) {
        requestStopEmulate();
        waitEmulateDone(8000);
    }
    stopLive();
    g_emuFinished = false;
    if (!g_session && !g_rfid) return;
    releaseModule();
    g_session = false;
}

void pcConnectNfcTick() {
    finishEmulateIfDone();
    if (!g_rfid) return;
    uint32_t now = millis();
    if (g_live == NfcLive::Read) {
        if (now - g_lastPoll < 200) return;
        g_lastPoll = now;
        int result = g_rfid->read();
        if (result != RFIDInterface::SUCCESS) {
            if (g_lastUid.length()) g_missedOnce = true;
            if (g_missedOnce) {
                g_lastUid = "";
                g_missedOnce = false;
            }
            return;
        }
        String uid = g_rfid->printableUID.uid;
        if (!uid.length()) return;
        if (uid == g_lastUid) return;
        g_lastUid = uid;
        g_missedOnce = false;
        emitTagInfo("nfc");
        if (g_rfid->strAllPages.length()) emitDumpChunks(uid);
        return;
    }
    if (g_live == NfcLive::Field) {
        if (now - g_lastPoll < 120) return;
        g_lastPoll = now;
        int rc = g_rfid->field_poll();
        bool present = (rc == RFIDInterface::SUCCESS);
        if (present) g_lastHitMs = now;
        JsonDocument doc;
        doc["evt"] = "nfc.status";
        doc["op"] = "field";
        doc["state"] = present ? "present" : "absent";
        doc["present"] = present;
        doc["ms_since_hit"] = g_lastHitMs ? (int)(now - g_lastHitMs) : -1;
        if (present && g_rfid->printableUID.uid.length()) doc["uid"] = g_rfid->printableUID.uid;
        emitDoc(doc);
    }
}

void pcConnectNfcHandle(const String &line) {
    // line is full command, e.g. "nfc.read start" or "nfc.files list dir=..."
    String cmd = line;
    cmd.trim();
    int sp = cmd.indexOf(' ');
    String head = sp < 0 ? cmd : cmd.substring(0, sp);
    String rest = sp < 0 ? String() : cmd.substring(sp + 1);
    rest.trim();
    head.toLowerCase();

    if (head == "nfc.manage") {
        if (rest.startsWith("start") || rest.length() == 0) {
            if (pcConnectNfcStart()) emitAckCmd("nfc.manage", true);
            else emitAckCmd("nfc.manage", false, "module not found");
            return;
        }
        if (rest.startsWith("stop")) {
            pcConnectNfcStop();
            emitAckCmd("nfc.manage", true, "stopped");
            return;
        }
        emitAckCmd("nfc.manage", false, "use start|stop");
        return;
    }

    if (head == "nfc.read") {
        if (rest.startsWith("start")) {
            if (!ensureModule()) {
                emitAckCmd("nfc.read", false, "module not found");
                return;
            }
            // Don't full-stop (would release module); just leave other live modes.
            stopLive();
            g_live = NfcLive::Read;
            g_lastUid = "";
            pcConnectSetStatus(PcRadio::Rfid, "nfc.read", 0);
            emitAckCmd("nfc.read", true);
            return;
        }
        if (rest.startsWith("once") || rest.startsWith("timeout=") || rest.length() == 0) {
            if (!ensureModule()) {
                emitAckCmd("nfc.read.once", false, "module not found");
                return;
            }
            uint32_t to = 5000;
            String t = argValue(rest, "timeout");
            if (!t.length() && rest.toInt() > 0) t = rest;
            if (t.length()) to = (uint32_t)t.toInt();
            if (to < 500) to = 500;
            if (to > 30000) to = 30000;
            emitAckCmd("nfc.read.once", true);
            int rc = readWithTimeout(to);
            if (rc != RFIDInterface::SUCCESS) {
                emitAckCmd("nfc.read.once", false, g_rfid->statusMessage(rc).c_str());
                return;
            }
            emitTagInfo();
            emitDumpChunks(g_rfid->printableUID.uid);
            return;
        }
        if (rest.startsWith("as") || head == "nfc.read.as") {
            // handled below via nfc.read.as
        }
        emitAckCmd("nfc.read", false, "use start|once");
        return;
    }

    if (head == "nfc.read.as") {
        if (!ensureModule()) {
            emitAckCmd("nfc.read.as", false, "module not found");
            return;
        }
        String t = argValue(rest, "type");
        if (!t.length()) t = rest;
        t.toLowerCase();
        RFIDInterface::ReadAsType typ = RFIDInterface::READ_AS_AUTO;
        if (t.indexOf("classic") >= 0) typ = RFIDInterface::READ_AS_CLASSIC;
        else if (t.indexOf("ul") >= 0 || t.indexOf("ntag") >= 0) typ = RFIDInterface::READ_AS_UL_NTAG;
        else if (t.indexOf("15693") >= 0) typ = RFIDInterface::READ_AS_ISO15693;
        else if (t.indexOf("felica") >= 0) typ = RFIDInterface::READ_AS_FELICA;
        else if (t.indexOf("isodep") >= 0 || t.indexOf("desfire") >= 0) typ = RFIDInterface::READ_AS_ISODEP;
        emitAckCmd("nfc.read.as", true);
        int rc = g_rfid->read_as(typ);
        emitAckCmd("nfc.read.as", rc == RFIDInterface::SUCCESS, g_rfid->statusMessage(rc).c_str());
        if (rc == RFIDInterface::SUCCESS) {
            emitTagInfo();
            emitDumpChunks(g_rfid->printableUID.uid);
        }
        return;
    }

    if (head == "nfc.field") {
        if (rest.startsWith("start") || rest.length() == 0) {
            if (!ensureModule()) {
                emitAckCmd("nfc.field", false, "module not found");
                return;
            }
            stopLive();
            g_live = NfcLive::Field;
            g_lastHitMs = 0;
            pcConnectSetStatus(PcRadio::Rfid, "nfc.field", 0);
            emitAckCmd("nfc.field", true);
            return;
        }
        emitAckCmd("nfc.field", false, "use start");
        return;
    }

    if (head == "nfc.emulate") {
        if (rest.startsWith("stop")) {
            if (g_emuRunning || g_live == NfcLive::Emulate) {
                requestStopEmulate();
                emitAckCmd("nfc.emulate", true, "stopping");
                emitStatus("emulate", "stopping", "esc");
            } else {
                emitAckCmd("nfc.emulate", true, "idle");
            }
            return;
        }
        if (rest.startsWith("start") || rest.length() == 0) {
            if (!ensureModule()) {
                emitAckCmd("nfc.emulate", false, "module not found");
                return;
            }
            if (g_rfid->printableUID.uid.length() == 0 && g_rfid->ndefMessage.payloadSize == 0 &&
                g_rfid->rawNdefRecord.empty()) {
                g_rfid->buildNdefMessage("url", "https://github.com/kvx17/kvxputer");
                g_rfid->emuMode = "t4t";
            } else if (g_rfid->emuMode.length() == 0) {
                g_rfid->emuMode = "t4t";
            }
            emitAckCmd("nfc.emulate", true);
            if (!startEmulateAsync(g_rfid->emuMode.c_str()))
                emitAckCmd("nfc.emulate", false, "emulate start failed");
            return;
        }
        emitAckCmd("nfc.emulate", false, "use start|stop");
        return;
    }

    if (head == "nfc.info") {
        if (!g_rfid || g_rfid->printableUID.uid.length() == 0) {
            emitAckCmd("nfc.info", false, "no tag data");
            return;
        }
        emitAckCmd("nfc.info", true);
        emitTagInfo();
        emitDumpChunks(g_rfid->printableUID.uid);
        return;
    }

    if (head == "nfc.save") {
        if (!ensureModule()) {
            emitAckCmd("nfc.save", false, "module not found");
            return;
        }
        if (g_rfid->printableUID.uid.length() == 0) {
            emitAckCmd("nfc.save", false, "no tag data");
            return;
        }
        String name = argValue(rest, "name");
        if (!name.length()) name = "rfid_dump";
        String format = argValue(rest, "format");
        format.toLowerCase();
        int rc;
        if (format == "flipper") {
            rc = g_rfid->saveFlipper(name);
            if (rc == RFIDInterface::NOT_IMPLEMENTED) rc = g_rfid->save(name);
        } else {
            rc = g_rfid->save(name);
        }
        emitAckCmd("nfc.save", rc == RFIDInterface::SUCCESS, g_rfid->statusMessage(rc).c_str());
        return;
    }

    if (head == "nfc.load") {
        if (!ensureModule()) {
            emitAckCmd("nfc.load", false, "module not found");
            return;
        }
        String path = argValue(rest, "path");
        if (!path.length()) {
            path = rest;
            if (path.startsWith("path=")) path = path.substring(5);
        }
        path.trim();
        if (!pathAllowed(path) && !path.startsWith(kvx::paths::RFID)) {
            // allow exact RFID root files
            if (!pathAllowed(path)) {
                emitAckCmd("nfc.load", false, "path not allowed");
                return;
            }
        }
        if (!pathAllowed(path)) {
            emitAckCmd("nfc.load", false, "path not allowed");
            return;
        }
        int rc = g_rfid->loadFromFile(path);
        emitAckCmd("nfc.load", rc == RFIDInterface::SUCCESS, g_rfid->statusMessage(rc).c_str());
        if (rc == RFIDInterface::SUCCESS) {
            emitTagInfo();
            emitDumpChunks(g_rfid->printableUID.uid);
        }
        return;
    }

    if (head == "nfc.write" || head == "nfc.clone" || head == "nfc.erase") {
        if (!ensureModule()) {
            emitAckCmd(head.c_str(), false, "module not found");
            return;
        }
        emitAckCmd(head.c_str(), true);
        emitStatus(head.c_str(), "waiting", "place tag");
        int rc = RFIDInterface::FAILURE;
        if (head == "nfc.write") rc = g_rfid->write();
        else if (head == "nfc.clone") rc = g_rfid->clone();
        else rc = g_rfid->erase();
        emitAckCmd(head.c_str(), rc == RFIDInterface::SUCCESS, g_rfid->statusMessage(rc).c_str());
        return;
    }

    if (head == "nfc.ndef.write" || (head == "nfc.ndef" && rest.startsWith("write"))) {
        handleNdef(head == "nfc.ndef" ? rest : String("write ") + rest);
        return;
    }
    if (head == "nfc.ndef.emulate" || (head == "nfc.ndef" && rest.startsWith("emulate"))) {
        handleNdef(head == "nfc.ndef" ? rest : String("emulate ") + rest);
        return;
    }
    if (head == "nfc.ndef") {
        handleNdef(rest);
        return;
    }

    if (head == "nfc.dict") {
        if (!ensureModule()) {
            emitAckCmd("nfc.dict", false, "module not found");
            return;
        }
        String mode = argValue(rest, "mode");
        if (!mode.length()) mode = rest;
        mode.toLowerCase();
        bool knownOnly = mode.indexOf("known") >= 0;
        emitAckCmd("nfc.dict", true);
        emitStatus("dict", "running", knownOnly ? "known" : "full");
        int rc = g_rfid->dict_read_classic(knownOnly);
        emitAckCmd("nfc.dict", rc == RFIDInterface::SUCCESS || rc == RFIDInterface::TAG_AUTH_ERROR,
                   g_rfid->statusMessage(rc).c_str());
        emitTagInfo();
        emitDumpChunks(g_rfid->printableUID.uid);
        return;
    }

    if (head == "nfc.magic") {
        handleMagic(rest);
        return;
    }

    if (head == "nfc.iso15693") {
        handleIso15693(rest);
        return;
    }

    if (head == "nfc.desfire") {
        if (!ensureModule()) {
            emitAckCmd("nfc.desfire", false, "module not found");
            return;
        }
        int rc = g_rfid->desfire_list();
        if (rc == RFIDInterface::NOT_IMPLEMENTED) {
            rc = g_rfid->read_as(RFIDInterface::READ_AS_ISODEP);
        }
        emitAckCmd("nfc.desfire", rc == RFIDInterface::SUCCESS, g_rfid->statusMessage(rc).c_str());
        if (g_rfid->printableUID.uid.length()) {
            emitTagInfo();
            emitDumpChunks(g_rfid->printableUID.uid);
        }
        return;
    }

    if (head == "nfc.ulc") {
        if (!ensureModule()) {
            emitAckCmd("nfc.ulc", false, "module not found");
            return;
        }
        String hex = argValue(rest, "key");
        hex.replace(" ", "");
        if (hex.length() != 32) {
            emitAckCmd("nfc.ulc", false, "need key=32 hex");
            return;
        }
        uint8_t key[16];
        for (int i = 0; i < 16; i++)
            key[i] = (uint8_t)strtoul(hex.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);
        int rc = g_rfid->ulc_auth(key);
        emitAckCmd("nfc.ulc", rc == RFIDInterface::SUCCESS, g_rfid->statusMessage(rc).c_str());
        if (rc == RFIDInterface::SUCCESS) {
            g_rfid->read();
            emitTagInfo();
            emitDumpChunks(g_rfid->printableUID.uid);
        }
        return;
    }

    if (head == "nfc.felica") {
        if (!ensureModule()) {
            emitAckCmd("nfc.felica", false, "module not found");
            return;
        }
        int rc = g_rfid->felica_services();
        if (rc == RFIDInterface::NOT_IMPLEMENTED) rc = g_rfid->read(1);
        emitAckCmd("nfc.felica", rc == RFIDInterface::SUCCESS, g_rfid->statusMessage(rc).c_str());
        if (rc == RFIDInterface::SUCCESS) {
            emitTagInfo();
            emitDumpChunks(g_rfid->printableUID.uid);
        }
        return;
    }

    if (head == "nfc.apdu") {
        if (!ensureModule()) {
            emitAckCmd("nfc.apdu", false, "module not found");
            return;
        }
        String hex = argValue(rest, "hex");
        if (!hex.length()) hex = rest;
        if (hex.startsWith("hex=")) hex = hex.substring(4);
        hex.replace(" ", "");
        hex.replace(":", "");
        if (hex.length() < 2 || (hex.length() % 2)) {
            emitAckCmd("nfc.apdu", false, "need hex=");
            return;
        }
        std::vector<uint8_t> tx;
        for (size_t i = 0; i + 1 < hex.length(); i += 2)
            tx.push_back((uint8_t)strtoul(hex.substring(i, i + 2).c_str(), nullptr, 16));
        uint8_t rx[256];
        uint16_t rxLen = 0;
        int rc = g_rfid->transceive_apdu(tx.data(), (uint16_t)tx.size(), rx, sizeof(rx), &rxLen);
        char swb[8] = "----";
        if (rc == RFIDInterface::SUCCESS && rxLen >= 2)
            sprintf(swb, "%02X%02X", rx[rxLen - 2], rx[rxLen - 1]);
        emitAckCmd("nfc.apdu", rc == RFIDInterface::SUCCESS, g_rfid->statusMessage(rc).c_str());
        JsonDocument doc;
        doc["evt"] = "nfc.status";
        doc["op"] = "apdu";
        doc["state"] = "done";
        doc["sw"] = swb;
        String rh;
        for (uint16_t i = 0; i < rxLen && i < 48; i++) {
            char b[3];
            sprintf(b, "%02X", rx[i]);
            rh += b;
        }
        doc["rx"] = rh;
        emitDoc(doc);
        return;
    }

    if (head == "nfc.compare") {
        if (!ensureModule()) {
            emitAckCmd("nfc.compare", false, "module not found");
            return;
        }
        String path = argValue(rest, "path");
        if (!path.length()) path = rest;
        if (path.startsWith("path=")) path = path.substring(5);
        path.trim();
        if (!pathAllowed(path)) {
            emitAckCmd("nfc.compare", false, "path not allowed");
            return;
        }
        int lr = g_rfid->loadFromFile(path);
        if (lr != RFIDInterface::SUCCESS) {
            emitAckCmd("nfc.compare", false, "load failed");
            return;
        }
        String fUid = g_rfid->printableUID.uid;
        String fSak = g_rfid->printableUID.sak;
        String fAtqa = g_rfid->printableUID.atqa;
        String fPages = g_rfid->strAllPages;
        emitAckCmd("nfc.compare", true);
        emitStatus("compare", "waiting", "hold live tag");
        int rr = readWithTimeout(8000);
        if (rr != RFIDInterface::SUCCESS) {
            emitAckCmd("nfc.compare", false, "no live tag");
            return;
        }
        JsonDocument doc;
        doc["evt"] = "nfc.status";
        doc["op"] = "compare";
        doc["state"] = "diff";
        doc["uid_ok"] = (fUid == g_rfid->printableUID.uid);
        doc["sak_ok"] = (fSak == g_rfid->printableUID.sak);
        doc["atqa_ok"] = (fAtqa == g_rfid->printableUID.atqa);
        doc["pages_ok"] = (fPages == g_rfid->strAllPages);
        doc["file_uid"] = fUid;
        doc["live_uid"] = g_rfid->printableUID.uid;
        emitDoc(doc);
        return;
    }

    if (head == "nfc.files") {
        handleFiles(rest);
        return;
    }

    // Honest stubs
    if (head == "nfc.picopass" || head == "nfc.eink" || head == "nfc.mfkey" || head == "nfc.epassport") {
        emitAckCmd(head.c_str(), false, "not supported over PC Connect / this module");
        return;
    }

    emitAckCmd(head.c_str(), false, "unknown nfc command");
}

#endif
