#ifndef __LORA_RF_H__
#define __LORA_RF_H__
#if !defined(LITE_VERSION)
#include "HWCDC.h"
#include <Arduino.h>
#include <SPI.h>

void lorachat();
void loraconf();
void loraAntennaCheck();
void applyLoraCapPinDefaults();
void ensureLoraSettings();
void configureLoraCap();

// Shared SX1262 / SX1276 radio ownership (plaintext chat and Meshtastic).
void clearLoraRadio();
SPIClass *selectLoraSPIBus();
int getLoraIrqPin();
int getLoraBusyPin();
int getLoraResetPin();
int getLoraCsPin();
bool ensureLoraCapAntenna();
void onLoraPacket();

class Module;
class SX1262;
class SX1276;

extern SPIClass *loraSpi;
extern Module *loraModule;
extern SX1262 *lora1262;
extern SX1276 *lora1276;
extern volatile bool loraPacketReceived;
extern volatile bool loraInterruptEnabled;

#endif
#endif
