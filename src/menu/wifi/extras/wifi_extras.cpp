#include "wifi_extras.h"
#include "root/ui/display.h"

static void wifiExtrasStub(const char *name) {
    displayInfo(String(name) + "\n(port pending — see docs/EVIL_FEATURE_MAP.md)", true);
}

void wifiExtrasAppend(std::vector<Option> &options) {
    // P1 Evil-unique WiFi tools not covered by Bruce wifi_atks / karma / sniffer
    options.push_back({"Handshake Master", []() { wifiExtrasStub("Handshake Master"); }});
    options.push_back({"Check Handshakes", []() { wifiExtrasStub("Check Handshakes"); }});
    options.push_back({"Wall Of Flipper", []() { wifiExtrasStub("Wall Of Flipper"); }});
    options.push_back({"Wardriving Master", []() { wifiExtrasStub("Wardriving Master"); }});
    options.push_back({"Probe Attack", []() { wifiExtrasStub("Probe Attack"); }});
    options.push_back({"Probe Sniffing", []() { wifiExtrasStub("Probe Sniffing"); }});
    options.push_back({"Karma Spear", []() { wifiExtrasStub("Karma Spear"); }});
    options.push_back({"Select Probe", []() { wifiExtrasStub("Select Probe"); }});
    options.push_back({"Delete Probe", []() { wifiExtrasStub("Delete Probe"); }});
    options.push_back({"Delete All Probes", []() { wifiExtrasStub("Delete All Probes"); }});
    // P2
    options.push_back({"WiFi Dead Drop", []() { wifiExtrasStub("WiFi Dead Drop"); }});
    options.push_back({"Open Wifi Checker", []() { wifiExtrasStub("Open Wifi Checker"); }});
    options.push_back({"Aircrack", []() { wifiExtrasStub("Aircrack"); }});
    options.push_back({"ESP32C5 Serial", []() { wifiExtrasStub("ESP32C5 Serial"); }});
    options.push_back({"CSI Radar", []() { wifiExtrasStub("CSI Radar"); }});
}
