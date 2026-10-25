#include "bus_scan.h"

#include "root/app/utils.h"
#include "root/config/configPins.h"
#include "root/hal/i2c_finder.h"
#include "root/hal/pahub.h"
#include "root/ui/display.h"
#include "root/ui/scrollableTextArea.h"
#include <globals.h>

namespace {

void i2cGroveScan() { find_i2c_addresses(); }

void spiPinMap() {
    ScrollableTextArea area("SPI pin map");
    area.addLine("Shared SPI (do not bit-bang SD)");
    area.addLine("SCK  G" + String(SPI_SCK_PIN));
    area.addLine("MOSI G" + String(SPI_MOSI_PIN));
    area.addLine("MISO G" + String(SPI_MISO_PIN));
    area.addLine("SS   G" + String(SPI_SS_PIN));
#ifdef GROVE_SDA
    area.addLine("Grove SDA G" + String(GROVE_SDA) + " (GDO/CE often)");
#endif
#ifdef GROVE_SCL
    area.addLine("Grove SCL G" + String(GROVE_SCL));
#endif
    area.addLine("");
    area.addLine("RF module cfg: " + String((int)kvxConfigPins.rfModule));
    area.addLine("No active SPI probe.");
    area.show(true);
}

} // namespace

void busScanHelp() {
    showAppHelp(
        {
            "Debug Grove PORT.A wiring.",
            "",
            "I2C Scan (Grove):",
            "  Probes 0x01-0x7F on SDA/SCL.",
            "  If PaHub is enabled, scans",
            "  each mux channel instead.",
            "  Hints: RFID2 0x28, Unit NFC",
            "  0x50, PN532 0x24, Scroll",
            "  0x40, Joystick 0x63, etc.",
            "  Esc/Sel to exit results.",
            "",
            "SPI pin map:",
            "  Shows shared SPI pins only.",
            "  Does NOT bit-bang the bus",
            "  (avoids corrupting SD).",
            "",
            "Conflict: do not share PORT.A",
            "between CC1101 SPI and I2C",
            "RFID at the same time.",
        }
    );
}

void busScanMenu() {
    options = {
        {"Help",             busScanHelp },
        {"I2C Scan (Grove)", i2cGroveScan},
        {"SPI pin map",      spiPinMap   },
    };
    if (pahubEnabled()) {
        options.push_back({"PaHub scan", i2cGroveScan});
    }
    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_SUBMENU, "Bus Scan");
    options.clear();
}
