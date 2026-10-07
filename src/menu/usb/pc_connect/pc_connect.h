#pragma once

#include <Arduino.h>

// Set while USB → PC Connect owns CDC; serial CLI must not read the port.
extern volatile bool pcConnectOwnsSerial;

void pcConnectMenu();

// Shared helpers used by wifi/ble backends.
void pcConnectEmitJson(const String &jsonLine);
void pcConnectNoteEvent();
// Drain USB command lines (used during blocking dwells).
void pcConnectPollSerial();

enum class PcRadio : uint8_t { Idle = 0, Wifi = 1, Ble = 2 };

PcRadio pcConnectRadio();
const char *pcConnectModeName();
uint8_t pcConnectChannel();
bool pcConnectLinked();

void pcConnectSetStatus(PcRadio radio, const char *mode, uint8_t channel = 0);
void pcConnectStopRadio();

#if !defined(LITE_VERSION)
bool pcConnectWifiAnalyzerStart(uint16_t dwellMs);
void pcConnectWifiAnalyzerStop();
void pcConnectWifiAnalyzerTick();
bool pcConnectWifiAnalyzerActive();
#endif

#if !defined(LITE_VERSION)
bool pcConnectBleScanStart();
void pcConnectBleScanStop();
void pcConnectBleScanTick();
bool pcConnectBleScanActive();
#endif

#if defined(EVIL_EXTENSIONS)
bool pcConnectBleFlipperStart();
void pcConnectBleFlipperTick();
bool pcConnectBleAirtagStart();
void pcConnectBleAirtagTick();
bool pcConnectBleSkimmerStart();
void pcConnectBleSkimmerTick();
#endif
