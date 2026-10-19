/**
 * @file rfid_shared.h
 * @brief Shared helpers for RFID (RFID2/RC522/125kHz) and NFC (PN532/ST25) channels.
 */
#ifndef __RFID_SHARED_H__
#define __RFID_SHARED_H__

#include <Arduino.h>
#include <functional>

class RFIDInterface;
class PahubChannelGuard;

enum class HfDomain { Rfid, Nfc };

bool nfcNdefEmulationSupported();
String rfidModuleTitleSuffix();
String nfcModuleTitleSuffix();
void showRfidWiringHelp();
void showNfcWiringHelp();
void rfidConfigMenu(const char *title, std::function<void()> onBack);
void nfcConfigMenu(const char *title, std::function<void()> onBack);

/** Probe Grove I2C and correct nfcModule when PN532 vs Unit NFC (ST25 @ 0x50) mismatches. */
void resolveNfcModuleSelection();

RFIDInterface *createHfModule(HfDomain domain);
PahubChannelGuard *openHfPahubGuard(HfDomain domain);

// Back-compat aliases used by older call sites during transition.
inline bool rfidNdefEmulationSupported() { return nfcNdefEmulationSupported(); }
inline RFIDInterface *rfidCreateModule() { return createHfModule(HfDomain::Nfc); }
inline PahubChannelGuard *rfidOpenPahubGuard() { return openHfPahubGuard(HfDomain::Nfc); }

#endif
