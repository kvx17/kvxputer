/**
 * @file nfc_session.h
 * Shared open/close helpers for NFC toolkit tools (nfcModule domain).
 */
#ifndef __NFC_SESSION_H__
#define __NFC_SESSION_H__

#include "menu/rfid/RFIDInterface.h"
#include "menu/rfid/rfid_shared.h"
#include "root/hal/bus_HAL.h"
#include "root/hal/pahub.h"
#include "root/ui/display.h"
#include "root/ui/kvx_ui.h"
#include <globals.h>

/** Instructional / error toasts that stay until any key (never flash-and-vanish). */
inline void nfcInfo(const String &msg) { displayInfo(msg, true); }
inline void nfcInfo(const char *msg) { displayInfo(String(msg), true); }
inline void nfcRefuse(const String &msg) { displayError(msg, true); }
inline void nfcRefuse(const char *msg) { displayError(String(msg), true); }

struct NfcSession {
    RFIDInterface *rfid = nullptr;
    PahubChannelGuard *guard = nullptr;

    bool open(bool needRead) {
#if defined(REMOVE_RFID_HW_INTERFACE)
        displayError("No NFC HW.", true);
        return false;
#else
        holdI2CBus();
        resolveNfcModuleSelection();
        guard = openHfPahubGuard(HfDomain::Nfc);
        rfid = createHfModule(HfDomain::Nfc);
        if (!rfid || !rfid->begin()) {
            displayError("NFC module init failed.", true);
            close();
            return false;
        }
        if (needRead) {
            nfcInfo("Hold tag near reader,\nthen press any key.");
            int result = RFIDInterface::TAG_NOT_PRESENT;
            uint32_t start = millis();
            while (millis() - start < 15000 && !returnToMenu) {
                result = rfid->read();
                if (result == RFIDInterface::SUCCESS || result == RFIDInterface::NOT_IMPLEMENTED) break;
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            if (result != RFIDInterface::SUCCESS && result != RFIDInterface::NOT_IMPLEMENTED) {
                displayError("No tag found.", true);
                close();
                return false;
            }
        }
        return true;
#endif
    }

    void close() {
        delete rfid;
        rfid = nullptr;
        delete guard;
        guard = nullptr;
        releaseI2CBusHold();
    }

    ~NfcSession() { close(); }
};

inline void nfcTopBar(const char *mode = nullptr) {
    TftFrame frame;
    tft.fillScreen(kvxConfig.bgColor);
    drawKvxTopBar("NFC", mode);
}

#endif
