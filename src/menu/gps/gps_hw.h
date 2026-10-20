#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>

/// Cardputer Cap LoRa-1262 / Unit GPS v1.1 (AT6668 @ 115200) helpers.
/// Docs: https://docs.m5stack.com/en/cap/Cap_LoRa-1262
///       https://docs.m5stack.com/en/unit/Unit_GPS_v1.1
enum class GpsHwSource : uint8_t {
    Auto = 0,
    CapLora,   ///< Cap LoRa-1262 GNSS: host RX=G15 TX=G13 @ 115200
    GroveUnit, ///< Unit GPS on Grove PORT.A: host RX=G1 TX=G2 @ 115200
};

struct GpsHwInfo {
    GpsHwSource source = GpsHwSource::Auto;
    int rx = -1;
    int tx = -1;
    int baud = 115200;
    const char *label = "none";
};

/// Apply Cap / Grove / Auto pin+baud into kvxConfigPins (does not open UART).
void gpsHwApplySource(GpsHwSource src);

/// Probe Cap then Grove for NMEA '$'; updates kvxConfigPins on success.
/// Leaves `ser` open on the winning port (or best fallback).
bool gpsHwAutodetect(HardwareSerial &ser, uint32_t probeMs = 1200);

/// Ensure AT6668-friendly baud/pins before opening a GPS session.
/// Runs autodetect when source is Auto (or pins/baud look wrong).
GpsHwInfo gpsHwPrepare(HardwareSerial &ser);

/// CASIC PCAS: GGA+RMC only + multi-GNSS (cuts UART flood so TinyGPS can keep up).
void gpsHwConfigureAt6668(HardwareSerial &ser);

GpsHwSource gpsHwCurrentSource();
const char *gpsHwSourceName(GpsHwSource src);
void gpsHwSetPreferredSource(GpsHwSource src); // persists in pins file via saveFile
GpsHwSource gpsHwPreferredSource();
