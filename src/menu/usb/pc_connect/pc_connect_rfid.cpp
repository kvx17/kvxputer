#include "pc_connect.h"

#if !defined(LITE_VERSION)
// Include order matters: ST25R3916 before Adafruit PN532 (FELICA_CMD_POLLING clash).
#include "menu/rfid/ST25R3916.h"
#include "menu/rfid/PN532.h"
#include "menu/rfid/RFID2.h"
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

RFIDInterface *createModule() {
    switch (kvxConfigPins.rfidModule) {
        case PN532_I2C_MODULE: return new PN532(PN532::CONNECTION_TYPE::I2C);
#ifdef M5STICK
        case PN532_I2C_SPI_MODULE: return new PN532(PN532::CONNECTION_TYPE::I2C_SPI);
#endif
        case PN532_SPI_MODULE: return new PN532(PN532::CONNECTION_TYPE::SPI);
        case RC522_SPI_MODULE: return new RFID2(false);
        case ST25R3916_SPI_MODULE: return new ST25R3916(ST25R3916::SPI_MODE);
        case ST25R3916_I2C_MODULE: return new ST25R3916(ST25R3916::I2C_MODE);
        case M5_RFID2_MODULE:
        default: return new RFID2();
    }
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
    holdI2CBus();
    g_pahub = new PahubChannelGuard(PahubChannelGuard::forRfid());
    g_rfid = createModule();
    if (!g_rfid || !g_rfid->begin()) {
        releaseModule();
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
