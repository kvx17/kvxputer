#include "ir_utils.h"

static int s_irTxExtraPin = -1;

void setup_ir_pin(int pin, uint8_t mode) {
    if (pin < 0) return;
    if (kvxConfigPins.SDCARD_bus.checkConflict(pin)) sdcardSPI.end();
    gpio_reset_pin((gpio_num_t)pin);
    pinMode(pin, mode);
}

void setIrTxExtraPin(int pin) { s_irTxExtraPin = pin; }
int getIrTxExtraPin() { return s_irTxExtraPin; }
