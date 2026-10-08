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

enum class PcRadio : uint8_t { Idle = 0, Wifi = 1, Ble = 2, Ir = 3, Rf = 4, Rfid = 5 };

PcRadio pcConnectRadio();
const char *pcConnectModeName();
uint8_t pcConnectChannel();
bool pcConnectLinked();

void pcConnectSetStatus(PcRadio radio, const char *mode, uint8_t channel = 0);
void pcConnectStopRadio();

#if !defined(LITE_VERSION)
bool pcConnectWifiAnalyzerStart(uint16_t dwellMs, uint8_t lockCh = 0);
void pcConnectWifiAnalyzerStop();
void pcConnectWifiAnalyzerTick();
bool pcConnectWifiAnalyzerActive();

bool pcConnectBleScanStart();
void pcConnectBleScanStop();
void pcConnectBleScanTick();
bool pcConnectBleScanActive();

bool pcConnectIrRxStart(bool raw);
void pcConnectIrRxStop();
void pcConnectIrRxTick();
bool pcConnectIrRxActive();
bool pcConnectIrTx(const String &protocol, const String &address, const String &command);
bool pcConnectIrTxRaw(uint32_t freq, const String &samples);

bool pcConnectRfRxStart(float mhz, bool raw);
void pcConnectRfRxStop();
void pcConnectRfRxTick();
bool pcConnectRfRxActive();
bool pcConnectRfRssiStart();
void pcConnectRfRssiStop();
void pcConnectRfRssiTick();
bool pcConnectRfRssiActive();
bool pcConnectRfTxLast();

bool pcConnectRfidStart();
void pcConnectRfidStop();
void pcConnectRfidTick();
bool pcConnectRfidActive();

bool pcConnectJamStart(uint32_t thresholdPerSec);
void pcConnectJamStop();
void pcConnectJamTick();
bool pcConnectJamActive();
#endif

#if defined(EVIL_EXTENSIONS)
bool pcConnectBleFlipperStart();
void pcConnectBleFlipperTick();
bool pcConnectBleAirtagStart();
void pcConnectBleAirtagTick();
bool pcConnectBleSkimmerStart();
void pcConnectBleSkimmerTick();
#endif
