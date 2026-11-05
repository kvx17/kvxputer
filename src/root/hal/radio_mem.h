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

    BLE policy: when Bluetooth starts, Wi-Fi is interrupted automatically (not a
    user-facing "Low RAM: free WiFi" dialog). On no-PSRAM that is also required
    to avoid an ipc0 stack overflow inside esp_bt_controller_init. Wi-Fi that
    was associated is restored when the BLE session ends.
*/
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <stddef.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp32-hal-psram.h>
#include <globals.h>
#include "root/net/wifi_common.h"
#ifdef HAS_RGB_LED
#include "root/hal/led_control.h"
#endif
#if !defined(KVX_NO_NIMBLE)
#include <NimBLEDevice.h>
#endif

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
#if defined(ARDUINO_M5STACK_TAB5)
    // Tab5 keeps the UI canvas suppressed permanently — a 1280×720 RGB565
    // sprite (~1.8MB) + DSI blit races hosted SDIO and paints a solid cyan
    // crash screen. Direct-to-panel draws are the only safe path.
#else
    tftSuppressCanvas(false);
#endif
#ifdef HAS_RGB_LED
    if (!ledIsStatusSuppressed()) ledSetup();
#endif
}

static inline bool radioWifiIsUp() {
    return WiFi.getMode() != WIFI_MODE_NULL || wifiConnected;
}

static inline int radioNimbleInitialized() {
#if defined(KVX_NO_NIMBLE)
    return 0;
#else
    return NimBLEDevice::isInitialized() ? 1 : 0;
#endif
}

struct RadioWifiPauseState {
    bool paused = false;
    bool wasConnected = false;
};

static inline RadioWifiPauseState &radioWifiPauseState() {
    static RadioWifiPauseState s;
    return s;
}

static inline void radioPrepareLog(const char *tag, size_t minBlock, bool ok) {
    Serial.printf(
        "[RAM] %s ok=%d need=%u dma=%u internal_free=%u internal_largest=%u "
        "wifiMode=%d wifi=%d ble=%d nimble=%d sd=%d heavy=%d pausedWifi=%d\n",
        tag,
        ok ? 1 : 0,
        (unsigned)minBlock,
        (unsigned)radioLargestDmaBlock(),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
        (int)WiFi.getMode(),
        wifiConnected ? 1 : 0,
        BLEConnected ? 1 : 0,
        radioNimbleInitialized(),
        sdcardMounted ? 1 : 0,
        uiRamHeavyDepth(),
        radioWifiPauseState().paused ? 1 : 0
    );
}

// Drop reclaimable non-radio DRAM (frame canvas; LED effects if still tight on
// no-PSRAM), then remeasure. On success the heavy refcount stays elevated for
// the tool — callers must not call uiRamEnterHeavy() again after a true return.
// On failure the enter is undone.
//
// skipDmaGate: the target stack is already initialized (reuse path).
static inline bool radioPrepare(size_t minBlock, bool skipDmaGate = false) {
    // return true; // uncomment to disable it
    uiRamEnterHeavy();

    if (skipDmaGate) {
        radioPrepareLog("prepare-reuse", minBlock, true);
        return true;
    }

    auto enough = [&]() { return radioLargestDmaBlock() >= minBlock; };

    if (!enough() && !psramFound()) {
#ifdef HAS_RGB_LED
        ledEffects(false);
        delay(30);
#endif
    }

    if (enough()) {
        radioPrepareLog("prepare", minBlock, true);
        return true;
    }

    radioPrepareLog("prepare", minBlock, false);
    uiRamLeaveHeavy();
    return false;
}

static inline bool radioHasMemForWifi() {
    return radioPrepare(RADIO_WIFI_MIN_DMA_BLOCK, radioWifiIsUp());
}

// Interrupt Wi-Fi for a BLE session. Must run BEFORE radioPrepare (wifiDisconnect
// leaves the heavy refcount). Tab5 hosted Wi-Fi stays alone on the C6.
static inline void radioPauseWifiForBle() {
#if defined(ARDUINO_M5STACK_TAB5)
    return;
#else
    RadioWifiPauseState &st = radioWifiPauseState();
    if (st.paused) return;

    wifi_mode_t idfMode = WIFI_MODE_NULL;
    const bool idfDriverUp = (esp_wifi_get_mode(&idfMode) == ESP_OK);
    if (!idfDriverUp && !radioWifiIsUp()) return;

    st.wasConnected = WiFi.isConnected() || wifiConnected;
    st.paused = true;

    Serial.printf(
        "[RAM] Pausing WiFi for BLE (connected=%d arduinoUp=%d idfMode=%d)\n",
        st.wasConnected ? 1 : 0,
        radioWifiIsUp() ? 1 : 0,
        (int)idfMode
    );

    if (radioWifiIsUp()) {
        wifiDisconnect();
        delay(50);
    } else {
        // Driver loaded outside Arduino mode tracking — STA→OFF keeps state consistent.
        WiFi.mode(WIFI_MODE_STA);
        delay(30);
        WiFi.mode(WIFI_OFF);
    }
    delay(150);

    if (esp_wifi_get_mode(&idfMode) == ESP_OK) {
        Serial.println("[RAM] WiFi driver still up after pause — force stop/deinit");
        esp_wifi_stop();
        esp_wifi_deinit();
        delay(100);
    }
    wifiConnected = false;
#endif
}

// Restore Wi-Fi that was paused for BLE. Deinits NimBLE first on no-PSRAM so
// esp_wifi_init does not race a live controller. Safe / idempotent.
static inline void radioResumeWifiAfterBle() {
#if defined(ARDUINO_M5STACK_TAB5)
    return;
#else
    RadioWifiPauseState &st = radioWifiPauseState();
    if (!st.paused) return;

    const bool reconnect = st.wasConnected;
    st.paused = false;
    st.wasConnected = false;

#if !defined(KVX_NO_NIMBLE)
    // Wi-Fi bring-up while the BLE controller is still live is unsafe on
    // no-PSRAM (and unnecessary once the BLE tool has finished).
    if (NimBLEDevice::isInitialized()) {
        NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
        if (adv) adv->stop();
        NimBLEScan *scan = NimBLEDevice::getScan();
        if (scan) {
            scan->stop();
            scan->clearResults();
        }
        NimBLEDevice::deinit(true);
        delay(80);
    }
#endif

    if (!reconnect) {
        Serial.println("[RAM] BLE done; WiFi was idle — not reconnecting");
        return;
    }

    Serial.println("[RAM] BLE done; restoring WiFi");
    // Background known-net connect — do not block the UI thread on a scan.
    xTaskCreate(wifiConnectTask, "wifiAfterBle", 4096, NULL, 2, NULL);
#endif
}

// End a BLE tool session: drop heavy UI hold and restore paused Wi-Fi.
static inline void radioBleSessionEnd() {
    uiRamLeaveHeavy();
    radioResumeWifiAfterBle();
}

static inline bool radioHasMemForBle() {
    // Always interrupt Wi-Fi before measuring / bringing the controller up.
    // Do not ask the user to "free WiFi" — pause it for the BLE session.
    radioPauseWifiForBle();

    // NimBLE host already up — only reclaim canvas.
    if (radioPrepare(RADIO_BLE_MIN_DMA_BLOCK, radioNimbleInitialized() != 0)) return true;

    // Could not start BLE — put Wi-Fi back if we paused it.
    radioResumeWifiAfterBle();
    return false;
}

#endif // __RADIO_MEM_H__
