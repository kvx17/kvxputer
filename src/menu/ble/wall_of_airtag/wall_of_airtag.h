#pragma once

#if defined(EVIL_EXTENSIONS)
#include "root/ui/scanner_list.h"

void wallOfAirtagMenu();

struct FindMyParse {
    uint8_t battery = 0xFF;
    bool separated = false;
    String keyPrefix;
};

// True if advertisement is Apple Find My / AirTag (company 0x004C, type 0x12).
bool parseFindMy(const ScannerAdvSnap &dev, FindMyParse &out);
const char *findMyBatteryLabel(uint8_t batt);
#endif
