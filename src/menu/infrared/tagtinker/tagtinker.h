/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * Pricer-style ESL IR (PP4). Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#pragma once

#if defined(EVIL_EXTENSIONS)
#include <stddef.h>
#include <stdint.h>

constexpr uint8_t TT_PROTO_DM = 0x85;
constexpr size_t TT_MAX_FRAME = 96;
constexpr int TT_BC_LEN = 17;

uint16_t ttCrc16(const uint8_t *data, size_t len);
size_t ttTerminate(uint8_t *buf, size_t len);
size_t ttRawFrame(uint8_t *buf, uint8_t proto, const uint8_t plid[4], uint8_t cmd);
size_t ttMakePing(uint8_t *buf, const uint8_t plid[4]);
size_t ttMakeRefresh(uint8_t *buf, const uint8_t plid[4]);
size_t ttMakeBroadcastPage(uint8_t *buf, uint8_t page, bool forever, uint16_t duration);
size_t ttMakeBroadcastDebug(uint8_t *buf);
size_t ttMakeAddressed(uint8_t *buf, const uint8_t plid[4], const uint8_t *payload, size_t payload_len);
bool ttBarcodeToPlid(const char *barcode, uint8_t plid[4]);
void ttIrInit(int pin);
void ttIrDeinit();
bool ttTransmitPp4(const uint8_t *data, size_t len, uint16_t repeats, uint8_t gap_delay);

void tagTinkerMenu();
#endif
