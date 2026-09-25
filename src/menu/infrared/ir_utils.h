#ifndef __IR_UTILS_H
#define __IR_UTILS_H
#include <globals.h>

void setup_ir_pin(int pin, uint8_t mode);

// Optional second TX GPIO. When >= 0, sendIRCommand also fires this pin
// after the primary (sequential dual LED). -1 disables.
void setIrTxExtraPin(int pin);
int getIrTxExtraPin();

#endif
