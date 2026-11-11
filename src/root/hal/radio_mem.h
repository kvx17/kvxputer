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
#include <esp_bt.h>
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
// External linkage so every file shares one heavy-refcount. A static inline
// would give each translation unit its own counter and its own pause flag.
inline int &uiRamHeavyDepth() {
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
    wifi_mode_t mode = WIFI_MODE_NULL;
};

inline RadioWifiPauseState &radioWifiPauseState() {
    static RadioWifiPauseState s;
    return s;
}

inline bool &radioDriverTouched() {
    static bool touched = false;
    return touched;
}

// One contiguous DMA block taken while the heap is still intact (before the first
// Wi-Fi init). After Wi-Fi stops, the freed driver buffers come back as several
// holes under 15 KiB, so BLE would fail the gate. This block stays allocated
// through the Wi-Fi session and is released only when a radio start is short.
constexpr size_t RADIO_DMA_RESERVE = 16 * 1024;

struct RadioDmaReserve {
    void *ptr = nullptr;
};

inline RadioDmaReserve &radioDmaReserve() {
    static RadioDmaReserve reserve;
    return reserve;
}

static inline void radioReleaseReserve() {
    RadioDmaReserve &reserve = radioDmaReserve();
    if (!reserve.ptr) return;
    heap_caps_free(reserve.ptr);
    reserve.ptr = nullptr;
}

static inline void radioEnsureReserve() {
#if defined(ARDUINO_M5STACK_TAB5)
    return;
#else
    if (psramFound()) return;
    RadioDmaReserve &reserve = radioDmaReserve();
    if (reserve.ptr) return;
    if (radioLargestDmaBlock() < RADIO_DMA_RESERVE + RADIO_WIFI_MIN_DMA_BLOCK) return;
    void *block = heap_caps_malloc(RADIO_DMA_RESERVE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!block) return;
    reserve.ptr = block;
#endif
}

// Drop reclaimable non-radio DRAM (frame canvas; LED effects if still tight on
// no-PSRAM), then remeasure. On success the heavy refcount stays elevated for
// the tool — callers must not call uiRamEnterHeavy() again after a true return.
// On failure the enter is undone.
//
// skipDmaGate: the target stack is already initialized (reuse path).
static inline bool radioPrepare(size_t minBlock, bool skipDmaGate = false) {
    uiRamEnterHeavy();

    if (skipDmaGate) return true;

    auto enough = [&]() { return radioLargestDmaBlock() >= minBlock; };

    if (!enough() && !psramFound()) {
#ifdef HAS_RGB_LED
        ledEffects(false);
        delay(30);
#endif
    }

    if (!enough() && radioDmaReserve().ptr) radioReleaseReserve();

    if (enough()) return true;

    uiRamLeaveHeavy();
    return false;
}

static inline bool radioHasMemForWifi() {
    radioEnsureReserve();
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
    st.mode = WiFi.getMode();
    if (st.mode == WIFI_MODE_NULL && idfDriverUp) st.mode = idfMode;
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
    radioDriverTouched() = true;
#endif
}

// BLE-before-any-Wi-Fi crashed in a core-0 task whose entry pointer was null.
// A Wi-Fi init/deinit leaves the PHY and coexistence callbacks installed, which
// is the state in which later BLE scans stayed up. Do this only once, and only
// when the driver has never been loaded.
static inline void radioTouchDriver() {
#if defined(ARDUINO_M5STACK_TAB5)
    return;
#else
    if (radioDriverTouched()) return;
    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) == ESP_OK) {
        radioDriverTouched() = true;
        return;
    }
    WiFi.persistent(false);
    WiFi.mode(WIFI_MODE_STA);
    delay(20);
    WiFi.mode(WIFI_OFF);
    delay(20);
    radioDriverTouched() = true;
#endif
}

// Restore Wi-Fi that was paused for BLE. Deinits NimBLE first on no-PSRAM so
// esp_wifi_init does not race a live controller. Safe / idempotent.
static inline void radioResumeWifiAfterBle() {
#if defined(ARDUINO_M5STACK_TAB5)
    return;
#else
    RadioWifiPauseState &st = radioWifiPauseState();
    const bool reconnect = st.paused && st.wasConnected;
    const wifi_mode_t restoreMode = st.paused ? st.mode : WIFI_MODE_NULL;
    st.paused = false;
    st.wasConnected = false;
    st.mode = WIFI_MODE_NULL;

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

    // NimBLEDevice::deinit() reports the host down while the controller task is
    // still allocated. That left 8 KB free and the Wi-Fi restore's scan failed.
#if defined(CONFIG_BT_CONTROLLER_ENABLED)
    {
        if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
            esp_bt_controller_disable();
            for (int i = 0; i < 50 && esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED; ++i) {
                delay(10);
            }
        }
        if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED) {
            esp_bt_controller_deinit();
            delay(20);
        }
        if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE) {
            Serial.println("[RAM] BLE controller still up; not restoring WiFi");
            return;
        }
    }
#endif

    if (reconnect) {
        Serial.println("[RAM] BLE done; restoring WiFi");
        // Rebuild the BLE reserve only when the leftover hole can still start Wi-Fi.
        radioEnsureReserve();
        // Static-stack task. A heap stack of 4096 here shrank the largest DMA
        // block from 19444 to 15348 and the 15 KiB gate refused the restore.
        wifiScheduleConnect();
        return;
    }

    // The STA/AP driver was up but not associated (startup scan leaves it that
    // way). Put that mode back so the next Wi-Fi scan is not talking to a
    // driver that was torn down.
    if (restoreMode != WIFI_MODE_NULL) {
        Serial.printf("[RAM] BLE done; restoring WiFi mode %d\n", (int)restoreMode);
        radioEnsureReserve();
        if (!radioHasMemForWifi()) {
            Serial.println("[RAM] WiFi mode restore refused");
            return;
        }
        WiFi.mode(restoreMode);
        uiRamLeaveHeavy();
        return;
    }

    Serial.println("[RAM] BLE done; WiFi was idle — not reconnecting");
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
    radioEnsureReserve();
    radioTouchDriver();
    radioPauseWifiForBle();

    // NimBLE host already up — only reclaim canvas.
    if (radioPrepare(RADIO_BLE_MIN_DMA_BLOCK, radioNimbleInitialized() != 0)) return true;

    // Could not start BLE — put Wi-Fi back if we paused it.
    radioResumeWifiAfterBle();
    return false;
}

#endif // __RADIO_MEM_H__
