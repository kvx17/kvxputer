#include "root/hal/i2c_finder.h"
#include "root/hal/bus_HAL.h"
#include "root/hal/pahub.h"
#include "root/ui/display.h"
#include "root/input/mykeyboard.h"

#define FIRST_I2C_ADDRESS 0x01
#define LAST_I2C_ADDRESS 0x7F

static String scanAddrsOnWire(TwoWire *wire, uint8_t skipAddr = 0) {
    String found;
    if (wire == nullptr) return found;
    for (uint8_t i = FIRST_I2C_ADDRESS; i <= LAST_I2C_ADDRESS; i++) {
        if (skipAddr && i == skipAddr) continue;
        wire->beginTransmission(i);
        if (wire->endTransmission() == 0) {
            if (found.length()) found += " ";
            char buf[8];
            snprintf(buf, sizeof(buf), "0x%X", i);
            found += buf;
        }
    }
    return found;
}

void find_i2c_addresses() {
    drawMainBorderWithTitle("I2C Finder");
    padprintln("");
    padprintln("");

    if (pahubEnabled()) {
        padprintln(pahubStatusLabel());
        char hub[24];
        snprintf(hub, sizeof(hub), "Hub addr 0x%02X", kvxConfig.pahubAddr);
        padprintln(hub);
        padprintln("");

        PahubScanResult results[PAHUB_CH_COUNT];
        pahubScanAll(results);
        for (uint8_t ch = 0; ch < PAHUB_CH_COUNT; ch++) {
            String line = "Ch" + String(ch) + ": ";
            String hint = pahubScanHint(ch);
            line += hint.length() ? hint : "-";
            padprintln(line);
        }
    } else {
        TwoWire *Wire = acquireI2CBus();
        padprintln("Checking I2C addresses ...\n");
        delay(300);
        String found = scanAddrsOnWire(Wire);
        padprint("Found: ");
        padprintln(found.length() ? found : "(none)");
        releaseI2CBus();
    }

    while (1) {
        if (check(EscPress) || check(SelPress)) {
            returnToMenu = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

uint8_t find_first_i2c_address() {
    TwoWire *Wire = acquireI2CBus();
    uint8_t found = 0;
    for (uint8_t i = FIRST_I2C_ADDRESS; i <= LAST_I2C_ADDRESS; i++) {
        Wire->beginTransmission(i);
        if (Wire->endTransmission() == 0) {
            found = i;
            break;
        }
    }
    releaseI2CBus();
    return found;
}

bool check_i2c_address(uint8_t i2c_address) {
    TwoWire *Wire = acquireI2CBus();
    Wire->beginTransmission(i2c_address);
    int error = Wire->endTransmission();
    releaseI2CBus();
    return (error == 0);
}
