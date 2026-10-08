#ifndef __SERIAL_GPIO_CMD_H__
#define __SERIAL_GPIO_CMD_H__

#include <SimpleCLI.h>

void createGpioCommands(SimpleCLI *cli);

// True when pin_no is on the board's general-purpose allow-list (Grove etc.).
bool is_free_gpio_pin(int pin_no);

#endif

// gpio cmds https://docs.flipper.net/development/cli/#aqA4b
