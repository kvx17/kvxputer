/**
 * @file nfc_dump_gen.h
 * Optional Save-as-file dump synthesis (NFC Maker style).
 * Inspired by Next-Flip/Momentum-Apps nfc_maker; reimplemented for kvxputer.
 */
#ifndef __NFC_DUMP_GEN_H__
#define __NFC_DUMP_GEN_H__

#include "menu/rfid/RFIDInterface.h"

// Compose NDEF (interactive), pick card type / UID, save .rfid (+ .nfc when possible).
void nfcSaveAsFileWizard();

// Apply a Type-2 NTAG dump into rfid fields from an already-composed NDEF.
bool nfcDumpApplyNtag(RFIDInterface *rfid, int totalPages, const uint8_t *uid, uint8_t uidLen);

#endif
