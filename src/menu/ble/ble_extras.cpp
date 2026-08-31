#include "ble_extras.h"
#include "root/ui/display.h"

static void bleExtrasStub(const char *name) {
    displayInfo(String(name) + "\n(port pending — see docs/EVIL_FEATURE_MAP.md)", true);
}

void bleExtrasAppend(std::vector<Option> &options) {
    options.push_back({"BLE Name Flood", []() { bleExtrasStub("BLENameFlood"); }});
    options.push_back({"Wall Of Airtag", []() { bleExtrasStub("Wall Of Airtag"); }});
    options.push_back({"FindMyEvil", []() { bleExtrasStub("FindMyEvil"); }});
}
