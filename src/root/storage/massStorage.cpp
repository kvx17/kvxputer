#include "root/storage/massStorage.h"
#if defined(SOC_USB_OTG_SUPPORTED)
#include "root/config/configPins.h"
#include "root/hal/bus_HAL.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include <USB.h>
#include <cstring>

bool MassStorage::shouldStop = false;
int32_t MassStorage::status = -1;

namespace {

// Cardputer mounts SD at 4 MHz for shared-bus safety; MSC can run faster on a dedicated SPI.
constexpr uint32_t kMscSpiHz = 20000000UL;

bool remountSdForMsc() {
    if (kvxConfigPins.SDCARD_bus.sck < 0) return false;
    uint8_t cs = (uint8_t)kvxConfigPins.SDCARD_bus.cs;

    SD.end();
    sdcardMounted = false;

    SPIClass *bus = acquireSPIBus(
        kvxConfigPins.SDCARD_bus.sck, kvxConfigPins.SDCARD_bus.miso, kvxConfigPins.SDCARD_bus.mosi
    );

    // Shared TFT/SD SPI: moderate clock so display redraws during MSC stay stable.
    // Dedicated SD bus: push toward SPI SD practical max (~20 MHz).
    if (bus != nullptr && bus != &sdcardSPI) {
        if (SD.begin(cs, *bus, 10000000UL, "/sd", 2) || SD.begin(cs, *bus, 4000000UL, "/sd", 2)) {
            sdcardMounted = true;
            return true;
        }
        return false;
    }

    (void)sdcardSPI.begin(
        (int8_t)kvxConfigPins.SDCARD_bus.sck,
        (int8_t)kvxConfigPins.SDCARD_bus.miso,
        (int8_t)kvxConfigPins.SDCARD_bus.mosi,
        (int8_t)kvxConfigPins.SDCARD_bus.cs
    );
    if (SD.begin(cs, sdcardSPI, kMscSpiHz, "/sd", 2) ||
        SD.begin(cs, sdcardSPI, 10000000UL, "/sd", 2)) {
        sdcardMounted = true;
        return true;
    }
    return setupSdCard(2);
}

} // namespace

MassStorage::MassStorage() { setup(); }

MassStorage::~MassStorage() {
    msc.end();
    USB.~ESPUSB();

    // Hack to make USB back to flash mode
    USB.enableDFU();

    // Restore normal SD mount after raw MSC access
    SD.end();
    sdcardMounted = false;
    setupSdCard();
}

void MassStorage::setup() {
    displayMessage("Mounting...");

    setShouldStop(false);

    if (!setupSdCard()) {
        displayError("SD card not found.");
        delay(1000);
        return;
    }

    // Higher SPI clock for MSC; fall back to the normal mount if it fails.
    if (!remountSdForMsc()) {
        displayError("SD remount failed.");
        delay(1000);
        return;
    }

    beginUsb();

    delay(500);
    return loop();
}

void MassStorage::loop() {
    int32_t prev_status = -1;
    while (!check(EscPress) && !shouldStop) {
        if (prev_status != status) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            switch (status) {
                case ARDUINO_USB_STARTED_EVENT: drawUSBStickIcon(true); break;
                case ARDUINO_USB_STOPPED_EVENT: drawUSBStickIcon(false); break;
                case ARDUINO_USB_SUSPEND_EVENT: MassStorage::displayMessage("USB suspend"); break;
                case ARDUINO_USB_RESUME_EVENT: MassStorage::displayMessage("USB resume"); break;
                default: break;
            }
            prev_status = status;
        } else vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

void MassStorage::beginUsb() {
    setupUsbCallback();
    setupUsbEvent();
    drawUSBStickIcon(false);
    USB.begin();
}

void MassStorage::setupUsbCallback() {
    uint32_t secSize = SD.sectorSize();
    uint32_t numSectors = SD.numSectors();
    if (secSize == 0 || numSectors == 0) {
        displayError("SD geometry invalid");
        return;
    }

    msc.vendorID("KVX");
    msc.productID("kvxputer");
    msc.productRevision("1.1");

    msc.onRead(usbReadCallback);
    msc.onWrite(usbWriteCallback);
    msc.onStartStop(usbStartStopCallback);

    msc.mediaPresent(true);
    msc.begin(numSectors, secSize);
}

void MassStorage::setupUsbEvent() {
    USB.onEvent([](void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
        if (event_base == ARDUINO_USB_EVENTS) { status = event_id; }
    });
}

void MassStorage::displayMessage(String message) {
    drawMainBorderWithTitle("Mass Storage");
    padprintln("");
    padprintln(message);
}

int32_t usbWriteCallback(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
    // Block device: host FS owns free space. Do NOT call SD.usedBytes()/totalBytes()
    // here — those walk FAT and make Windows/macOS mounts crawl.
    const uint32_t secSize = SD.sectorSize();
    if (secSize == 0 || buffer == nullptr || bufsize == 0) return -1;

    if (offset == 0 && (bufsize % secSize) == 0) {
        if (!SD.writeRAW(buffer, lba, bufsize / secSize)) return -1;
        return (int32_t)bufsize;
    }

    // Rare partial transfer: RMW within a sector.
    uint8_t sector[512];
    if (secSize > sizeof(sector) || offset + bufsize > secSize) return -1;
    if (!SD.readRAW(sector, lba)) return -1;
    memcpy(sector + offset, buffer, bufsize);
    if (!SD.writeRAW(sector, lba)) return -1;
    return (int32_t)bufsize;
}

int32_t usbReadCallback(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
    const uint32_t secSize = SD.sectorSize();
    if (secSize == 0 || buffer == nullptr || bufsize == 0) return -1;
    auto *out = reinterpret_cast<uint8_t *>(buffer);

    if (offset == 0 && (bufsize % secSize) == 0) {
        if (!SD.readRAW(out, lba, bufsize / secSize)) return -1;
        return (int32_t)bufsize;
    }

    uint8_t sector[512];
    if (secSize > sizeof(sector) || offset + bufsize > secSize) return -1;
    if (!SD.readRAW(sector, lba)) return -1;
    memcpy(out, sector + offset, bufsize);
    return (int32_t)bufsize;
}

bool usbStartStopCallback(uint8_t power_condition, bool start, bool load_eject) {
    if (!start && load_eject) {
        MassStorage::setShouldStop(true);
        return false;
    }

    return true;
}

void drawUSBStickIcon(bool plugged) {
    static bool first = true;

    float scale;
    if (kvxConfigPins.rotation & 0b01) scale = float((float)tftHeight / (float)135);
    else scale = float((float)tftWidth / (float)240);

    int iconW = scale * 120;
    int iconH = scale * 40;

    if (iconW % 2 != 0) iconW++;
    if (iconH % 2 != 0) iconH++;

    int radius = 5;

    int bodyW = 2 * iconW / 3;
    int bodyH = iconH;
    int bodyX = tftWidth / 2 - iconW / 2;
    int bodyY = tftHeight / 2;

    int portW = iconW - bodyW;
    int portH = 0.8 * bodyH;
    int portX = bodyX + bodyW;
    int portY = bodyY + (bodyH - portH) / 2;

    int portDetailW = portW / 2;
    int portDetailH = portH / 4;
    int portDetailX = portX + (portW - portDetailW) / 2;
    int portDetailY1 = portY + 0.8 * portDetailH;
    int portDetailY2 = portY + portH - 1.8 * portDetailH;

    int ledW = 0.1 * bodyH;
    int ledH = 0.6 * bodyH;
    int ledX = bodyX + 2 * ledW;
    int ledY = bodyY + (iconH - ledH) / 2;

    if (first) {
        MassStorage::displayMessage("");
        // Body
        tft.fillRoundRect(bodyX, bodyY, bodyW, bodyH, radius, TFT_DARKCYAN);
        // Port USB
        tft.fillRoundRect(portX, portY, portW, portH, radius, TFT_LIGHTGREY);
        // Small square on port
        tft.fillRoundRect(portDetailX, portDetailY1, portDetailW, portDetailH, radius, TFT_DARKGREY);
        tft.fillRoundRect(portDetailX, portDetailY2, portDetailW, portDetailH, radius, TFT_DARKGREY);
        first = false;
    }
    // Led
    tft.fillRoundRect(ledX, ledY, ledW, ledH, radius, plugged ? TFT_GREEN : TFT_RED);
}

#endif // SOC_USB_OTG_SUPPORTED
