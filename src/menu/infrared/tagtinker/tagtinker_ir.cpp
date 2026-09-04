/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * ESL IR codec + PP4 bitbang. Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "tagtinker.h"
#if defined(EVIL_EXTENSIONS)
#include <Arduino.h>
#include <string.h>
#include <globals.h>

#define TT_CPU_MHZ 240
#define TT_FLIPPER_MHZ 64
#define TT_SCALE(x) ((uint32_t)((uint64_t)(x) * TT_CPU_MHZ / TT_FLIPPER_MHZ))
#define TT_PP4_BURST TT_SCALE(2581)
#define TT_HALF_CARRIER 85

static const uint32_t kPp4Gaps[4] = {
    TT_SCALE(3871), TT_SCALE(15483), TT_SCALE(7741), TT_SCALE(11612)
};

static int gPin = -1;

static inline uint32_t cc() { return ESP.getCycleCount(); }

static void delayCycles(uint32_t n) {
    uint32_t s = cc();
    while ((cc() - s) < n) {}
}

static void burst(uint32_t duration) {
    uint32_t start = cc();
    while ((cc() - start) < duration) {
        digitalWrite(gPin, HIGH);
        uint32_t t = cc();
        while ((cc() - t) < TT_HALF_CARRIER) {}
        digitalWrite(gPin, LOW);
        t = cc();
        while ((cc() - t) < TT_HALF_CARRIER) {}
    }
}

uint16_t ttCrc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0x8408;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : crc >> 1;
    }
    return crc;
}

size_t ttTerminate(uint8_t *buf, size_t len) {
    uint16_t crc = ttCrc16(buf, len);
    buf[len] = crc & 0xFF;
    buf[len + 1] = (crc >> 8) & 0xFF;
    return len + 2;
}

size_t ttRawFrame(uint8_t *buf, uint8_t proto, const uint8_t plid[4], uint8_t cmd) {
    buf[0] = proto;
    memcpy(&buf[1], plid, 4);
    buf[5] = cmd;
    return 6;
}

size_t ttMakePing(uint8_t *buf, const uint8_t plid[4]) {
    size_t p = ttRawFrame(buf, TT_PROTO_DM, plid, 0x97);
    buf[p++] = 0x01;
    buf[p++] = 0x00;
    buf[p++] = 0x00;
    buf[p++] = 0x00;
    for (int i = 0; i < 20; i++) buf[p++] = 0x01;
    return ttTerminate(buf, p);
}

size_t ttMakeRefresh(uint8_t *buf, const uint8_t plid[4]) {
    size_t p = ttRawFrame(buf, TT_PROTO_DM, plid, 0x34);
    buf[p++] = 0x00;
    buf[p++] = 0x00;
    buf[p++] = 0x00;
    buf[p++] = 0x01;
    for (int i = 0; i < 18; i++) buf[p++] = 0x00;
    return ttTerminate(buf, p);
}

size_t ttMakeBroadcastPage(uint8_t *buf, uint8_t page, bool forever, uint16_t duration) {
    const uint8_t plid[4] = {0};
    size_t p = ttRawFrame(buf, TT_PROTO_DM, plid, 0x06);
    buf[p++] = (uint8_t)((((page + 1) & 7) << 3) | 0x01 | (forever ? 0x80 : 0x00));
    buf[p++] = 0x00;
    buf[p++] = 0x00;
    buf[p++] = (duration >> 8) & 0xFF;
    buf[p++] = duration & 0xFF;
    return ttTerminate(buf, p);
}

size_t ttMakeBroadcastDebug(uint8_t *buf) {
    const uint8_t plid[4] = {0};
    size_t p = ttRawFrame(buf, TT_PROTO_DM, plid, 0x06);
    buf[p++] = 0xF1;
    buf[p++] = 0x00;
    buf[p++] = 0x00;
    buf[p++] = 0x00;
    buf[p++] = 0x0A;
    return ttTerminate(buf, p);
}

size_t ttMakeAddressed(uint8_t *buf, const uint8_t plid[4], const uint8_t *payload, size_t payload_len) {
    size_t p = ttRawFrame(buf, TT_PROTO_DM, plid, payload[0]);
    memcpy(&buf[p], payload + 1, payload_len - 1);
    p += payload_len - 1;
    return ttTerminate(buf, p);
}

bool ttBarcodeToPlid(const char *barcode, uint8_t plid[4]) {
    if (!barcode || strlen(barcode) != TT_BC_LEN) return false;
    uint64_t a = 0, b = 0;
    for (int i = 2; i < 7; i++) a = a * 10 + (barcode[i] - '0');
    for (int i = 7; i < 12; i++) b = b * 10 + (barcode[i] - '0');
    uint64_t id = (a << 16) | b;
    plid[0] = id & 0xFF;
    plid[1] = (id >> 8) & 0xFF;
    plid[2] = (id >> 16) & 0xFF;
    plid[3] = (id >> 24) & 0xFF;
    return true;
}

void ttIrInit(int pin) {
    gPin = pin;
    pinMode(gPin, OUTPUT);
    digitalWrite(gPin, LOW);
}

void ttIrDeinit() {
    if (gPin >= 0) digitalWrite(gPin, LOW);
}

static void sendFramePp4(const uint8_t *data, size_t len) {
    for (size_t byte_idx = 0; byte_idx < len; byte_idx++) {
        uint8_t current = data[byte_idx];
        for (int sym = 0; sym < 4; sym++) {
            uint8_t symbol = current & 0x03;
            current >>= 2;
            burst(TT_PP4_BURST);
            delayCycles(kPp4Gaps[symbol]);
        }
    }
    burst(TT_PP4_BURST);
    digitalWrite(gPin, LOW);
}

bool ttTransmitPp4(const uint8_t *data, size_t len, uint16_t repeats, uint8_t gap_delay) {
    if (gPin < 0 || !data || len == 0 || len > 255) return false;
    for (uint32_t rep = 0; rep <= repeats; rep++) {
        portDISABLE_INTERRUPTS();
        sendFramePp4(data, len);
        portENABLE_INTERRUPTS();
        if (rep < repeats && gap_delay) delayCycles((uint32_t)gap_delay * 120000U);
        if ((rep % 5U) == 4U) delay(1);
        if (check(EscPress)) return false;
    }
    return true;
}
#endif
