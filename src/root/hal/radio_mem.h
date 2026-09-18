#ifndef __RADIO_MEM_H__
#define __RADIO_MEM_H__
/*
    Internal-DRAM contiguous-block guard for radio bring-up on no-PSRAM boards.

    On boards without PSRAM (e.g. m5stack-cardputer) the Wi-Fi and BLE stacks —
    plus the SD/SPI DMA buffers — all compete for the SAME internal DRAM. What
    actually gates them is not total free heap but the LARGEST CONTIGUOUS
    DMA-capable block. When that block is exhausted, esp_wifi_init /
    esp_bt_controller_init fail deep inside the SDK and leave the driver in a
    half-initialized state, which then crashes (LoadProhibited / abort()) on the
    next operation.

    These helpers let callers refuse to start a radio *before* touching it when
    there isn't enough contiguous DMA memory, turning an unavoidable crash into a
    clean, user-visible error.
*/
#include <esp_heap_caps.h>
#include <stddef.h>
#include "root/net/wifi_common.h"
#include <WiFi.h>
#include <esp_wifi.h>
#ifdef HAS_RGB_LED
#include "root/hal/led_control.h"
#endif
// Declared in sd_functions.h; avoid pulling FS headers into every radio caller.
void closeSdCard();
extern bool sdcardMounted;

// Implemented in display.cpp. Frees the offscreen UI canvas so BLE/WiFi can
// reclaim the contiguous internal-DRAM block it occupies on no-PSRAM boards.
#if defined(HAS_SCREEN)
void tftReleaseFrameCanvas();
void tftSuppressCanvas(bool suppress);
#else
static inline void tftReleaseFrameCanvas() {}
static inline void tftSuppressCanvas(bool suppress) { (void)suppress; }
#endif

// Largest contiguous DMA-capable internal block, in bytes. This is the number
// that gates Wi-Fi/BLE controller init.
static inline size_t radioLargestDmaBlock() { return heap_caps_get_largest_free_block(MALLOC_CAP_DMA); }

// Minimum contiguous DMA block required BEFORE bringing Wi-Fi up (checked at the
// scan/menu entry, i.e. before esp_wifi is touched at all).
constexpr size_t RADIO_WIFI_MIN_DMA_BLOCK = 15 * 1024;

// Minimum contiguous DMA block required before bringing the BLE stack up.
constexpr size_t RADIO_BLE_MIN_DMA_BLOCK = 15 * 1024;

// Session-level RAM gate: drop the UI canvas and FastLED effect task while a
// heavy app (Wi-Fi/BLE/media) holds DRAM. Nested enter/leave is refcounted.
static inline int &uiRamHeavyDepth() {
    static int depth = 0;
    return depth;
}

static inline void uiRamEnterHeavy() {
    if (uiRamHeavyDepth()++ > 0) return;
    tftReleaseFrameCanvas();
    tftSuppressCanvas(true);
#ifdef HAS_RGB_LED
    ledEffects(false);
#endif
}

static inline void uiRamLeaveHeavy() {
    if (uiRamHeavyDepth() <= 0) return;
    if (--uiRamHeavyDepth() > 0) return;
    tftSuppressCanvas(false);
#ifdef HAS_RGB_LED
    if (!ledIsStatusSuppressed()) ledSetup();
#endif
}

static inline bool radioHasMemForWifi() {
    // return true; // uncomment to disable it
    auto enough = []() { return radioLargestDmaBlock() >= RADIO_WIFI_MIN_DMA_BLOCK; };
    if (enough()) return true;
    tftReleaseFrameCanvas();
    if (enough()) return true;
#ifdef HAS_RGB_LED
    // Match BLE: Battery Status / other effects keep a 2KB task in internal RAM.
    ledEffects(false);
    delay(30);
#endif
    return enough();
}

static inline bool radioHasMemForBle() {
    // return true; // uncomment to disable it

    auto enough = []() { return radioLargestDmaBlock() >= RADIO_BLE_MIN_DMA_BLOCK; };

    if (enough()) return true;

    // Full-screen UI canvas (~64KB) sits in internal DRAM on no-PSRAM boards.
    Serial.println("[RAM] Low DMA for BLE, releasing display canvas...");
    tftReleaseFrameCanvas();
    if (enough()) {
        Serial.printf("[RAM] Display canvas freed, DMA block: %u bytes\n", (unsigned)radioLargestDmaBlock());
        return true;
    }

#ifdef HAS_RGB_LED
    // Battery Status / other effects keep a 2KB task in internal RAM.
    Serial.println("[RAM] Low DMA for BLE, stopping LED effects...");
    ledEffects(false);
    delay(30);
    if (enough()) {
        Serial.printf("[RAM] LED task freed, DMA block: %u bytes\n", (unsigned)radioLargestDmaBlock());
        return true;
    }
#endif

    // Free WiFi stack (mode OFF alone can leave DMA buffers allocated).
    Serial.println("[RAM] Low contiguous DMA memory for BLE, attempting to free WiFi...");
    if (WiFi.getMode() != WIFI_MODE_NULL || wifiConnected) {
        wifiDisconnect();
        delay(100);
    }
    WiFi.mode(WIFI_OFF);
    delay(50);
    esp_wifi_stop();
    esp_wifi_deinit();
    delay(200);
    if (enough()) {
        Serial.printf("[RAM] WiFi freed, DMA block: %u bytes\n", (unsigned)radioLargestDmaBlock());
        return true;
    }

    // SD SPI DMA buffers also sit in internal DRAM on Cardputer.
    if (sdcardMounted) {
        Serial.println("[RAM] Still low DMA, unmounting SD...");
        closeSdCard();
        delay(100);
        if (enough()) {
            Serial.printf("[RAM] SD freed, DMA block: %u bytes\n", (unsigned)radioLargestDmaBlock());
            return true;
        }
    }

    Serial.printf(
        "[RAM] Still only %u bytes DMA block, minimum %u needed\n",
        (unsigned)radioLargestDmaBlock(),
        (unsigned)RADIO_BLE_MIN_DMA_BLOCK
    );
    return false;
}

#endif // __RADIO_MEM_H__
