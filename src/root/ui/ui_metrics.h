#ifndef __UI_METRICS_H__
#define __UI_METRICS_H__

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

// Default off. Enable with the serial "uimetrics" command.
extern bool uiMetricsEnabled;

void uiMetricsSetEnabled(bool on);
bool uiMetricsIsEnabled();

// Count a blit (presentDirty) or a direct panel write when not buffering.
void uiMetricsNotePresent(uint32_t pixels, uint32_t us);
void uiMetricsNoteDirectPixels(uint32_t pixels);
void uiMetricsNoteFrameFallback();

// Log heap / DMA at app entry, ~2s steady, and exit. screen is a short tag.
enum UiMetricsPhase : uint8_t { UI_METRICS_ENTRY = 0, UI_METRICS_STEADY = 1, UI_METRICS_EXIT = 2 };
void uiMetricsMark(const char *screen, UiMetricsPhase phase);

// Call from a loop so the 1 Hz Serial summary and auto-steady mark can fire.
void uiMetricsTick();

#endif
