#include "pc_connect.h"

#if !defined(LITE_VERSION)
// Include order matters: ST25R3916 before Adafruit PN532 (FELICA_CMD_POLLING clash).
#include "menu/rfid/rfid_shared.h"
#include "menu/rfid/RFIDInterface.h"
#include "root/hal/bus_HAL.h"
#include "root/hal/pahub.h"
#include <ArduinoJson.h>
#include <globals.h>

namespace {

bool g_active = false;
RFIDInterface *g_rfid = nullptr;
PahubChannelGuard *g_pahub = nullptr;
String g_lastUid;
uint32_t g_lastPoll = 0;

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

} // namespace

bool pcConnectRfidActive() { return g_active; }

bool pcConnectRfidStart() {
    pcConnectStopRadio();
    // Prefer NFC (PN532/ST25), then RFID2/RC522.
    if (!tryOpenHf(HfDomain::Nfc) && !tryOpenHf(HfDomain::Rfid)) {
        return false;
    }
    g_lastUid = "";
    g_lastPoll = 0;
    g_active = true;
    pcConnectSetStatus(PcRadio::Rfid, "rfid.read", 0);
    return true;
}

void pcConnectRfidStop() {
    if (!g_active) return;
    releaseModule();
    g_active = false;
    g_lastUid = "";
}

void pcConnectRfidTick() {
    if (!g_active || !g_rfid) return;
    uint32_t now = millis();
    if (now - g_lastPoll < 200) return;
    g_lastPoll = now;

    int result = g_rfid->read();
    if (result != RFIDInterface::SUCCESS) return;

    String uid = g_rfid->printableUID.uid;
    if (!uid.length() || uid == g_lastUid) return;
    g_lastUid = uid;

    JsonDocument doc;
    doc["evt"] = "rfid";
    doc["uid"] = uid;
    doc["type"] = g_rfid->printableUID.picc_type;
    doc["sak"] = g_rfid->printableUID.sak;
    doc["atqa"] = g_rfid->printableUID.atqa;
    doc["pages"] = g_rfid->totalPages;
    String out;
    serializeJson(doc, out);
    pcConnectEmitJson(out);
}

#endif
