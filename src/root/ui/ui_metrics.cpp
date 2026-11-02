#include "ui_metrics.h"
#include <esp_heap_caps.h>
#include <globals.h>

bool uiMetricsEnabled = false;

static uint32_t s_paints = 0;
static uint64_t s_pixelsSum = 0;
static uint32_t s_pixelsMax = 0;
static uint64_t s_presentUsSum = 0;
static uint32_t s_fallbacks = 0;
static uint32_t s_windowMs = 0;
static char s_screen[24] = "";
static uint32_t s_entryMs = 0;
static bool s_steadyDone = false;
static size_t s_entryHeap = 0;
static size_t s_entryDma = 0;

static size_t freeHeapNow() { return ESP.getFreeHeap(); }

static size_t largestDmaNow() {
#if defined(ESP32)
    return heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
#else
    return 0;
#endif
}

static void resetWindow() {
    s_paints = 0;
    s_pixelsSum = 0;
    s_pixelsMax = 0;
    s_presentUsSum = 0;
    s_fallbacks = 0;
    s_windowMs = millis();
}

static void printHeapLine(const char *phase) {
    Serial.printf(
        "[uimetrics] %s screen=%s heap=%u dma=%u\n",
        phase,
        s_screen[0] ? s_screen : "-",
        (unsigned)freeHeapNow(),
        (unsigned)largestDmaNow()
    );
}

void uiMetricsSetEnabled(bool on) {
    uiMetricsEnabled = on;
    if (on) {
        resetWindow();
        Serial.println("[uimetrics] on");
    } else {
        Serial.println("[uimetrics] off");
    }
}

bool uiMetricsIsEnabled() { return uiMetricsEnabled; }

void uiMetricsNotePresent(uint32_t pixels, uint32_t us) {
    if (!uiMetricsEnabled) return;
    s_paints++;
    s_pixelsSum += pixels;
    if (pixels > s_pixelsMax) s_pixelsMax = pixels;
    s_presentUsSum += us;
}

void uiMetricsNoteDirectPixels(uint32_t pixels) {
    if (!uiMetricsEnabled) return;
    s_paints++;
    s_pixelsSum += pixels;
    if (pixels > s_pixelsMax) s_pixelsMax = pixels;
}

void uiMetricsNoteFrameFallback() {
    if (!uiMetricsEnabled) return;
    s_fallbacks++;
}

void uiMetricsMark(const char *screen, UiMetricsPhase phase) {
    if (!uiMetricsEnabled) return;
    if (screen && screen[0]) {
        strncpy(s_screen, screen, sizeof(s_screen) - 1);
        s_screen[sizeof(s_screen) - 1] = '\0';
    }
    if (phase == UI_METRICS_ENTRY) {
        s_entryMs = millis();
        s_steadyDone = false;
        s_entryHeap = freeHeapNow();
        s_entryDma = largestDmaNow();
        resetWindow();
        Serial.printf(
            "[uimetrics] entry screen=%s heap=%u dma=%u\n",
            s_screen,
            (unsigned)s_entryHeap,
            (unsigned)s_entryDma
        );
        return;
    }
    if (phase == UI_METRICS_STEADY) {
        s_steadyDone = true;
        const int32_t dHeap = (int32_t)freeHeapNow() - (int32_t)s_entryHeap;
        Serial.printf(
            "[uimetrics] steady screen=%s heap=%u dma=%u dHeap=%d\n",
            s_screen,
            (unsigned)freeHeapNow(),
            (unsigned)largestDmaNow(),
            (int)dHeap
        );
        return;
    }
    // EXIT
    const int32_t dHeap = (int32_t)freeHeapNow() - (int32_t)s_entryHeap;
    Serial.printf(
        "[uimetrics] exit screen=%s heap=%u dma=%u dHeap=%d\n",
        s_screen,
        (unsigned)freeHeapNow(),
        (unsigned)largestDmaNow(),
        (int)dHeap
    );
    s_screen[0] = '\0';
}

void uiMetricsTick() {
    if (!uiMetricsEnabled) return;
    const uint32_t now = millis();
    if (s_entryMs && !s_steadyDone && (now - s_entryMs) >= 2000) {
        uiMetricsMark(s_screen, UI_METRICS_STEADY);
    }
    if (now - s_windowMs < 1000) return;
    const uint32_t elapsed = now - s_windowMs;
    const float paintsPerSec = elapsed ? (1000.f * (float)s_paints / (float)elapsed) : 0.f;
    const uint32_t avgPx = s_paints ? (uint32_t)(s_pixelsSum / s_paints) : 0;
    const uint32_t avgUs = s_paints ? (uint32_t)(s_presentUsSum / s_paints) : 0;
    Serial.printf(
        "[uimetrics] paints/s=%.1f avgPx=%u maxPx=%u avgPresentUs=%u fallbacks=%u heap=%u dma=%u screen=%s\n",
        paintsPerSec,
        (unsigned)avgPx,
        (unsigned)s_pixelsMax,
        (unsigned)avgUs,
        (unsigned)s_fallbacks,
        (unsigned)freeHeapNow(),
        (unsigned)largestDmaNow(),
        s_screen[0] ? s_screen : "-"
    );
    resetWindow();
}
