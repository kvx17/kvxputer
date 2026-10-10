#include "calc_menu.h"

#include "root/ui/display.h"
#include <globals.h>

void calcMenu() {
    for (;;) {
        bool goBack = false;
        options = {
            {"Subnet",          calcSubnet  },
            {"Base converter",  calcBase    },
            {"Byte units",      calcBytes   },
            {"Resistor color",  calcResistor},
            {"LED resistor",    calcLed     },
            {"Voltage divider", calcDivider },
            {"Baud bit-time",   calcBaud    },
            {"dBm and power",   calcDbm     },
            {"Epoch",           calcEpoch   },
            {"Temp/Length/AWG", calcMeasure },
            {"Bitmask",         calcBitmask },
            {"CRC",             calcCrc     },
            {"Units",           calcUnits   },
            {"Ohm's law",       calcOhm     },
            {"Voltage drop",    calcVdrop   },
            {"Motor",           calcMotor   },
            {"Gas law",         calcGas     },
            {"Flow and pipe",   calcFlow    },
            {"Back",            [&goBack]() { goBack = true; }},
        };
        int idx = loopOptions(options, MENU_TYPE_SUBMENU, "Calculators");
        if (idx < 0 || goBack || forceHome) break;
    }
}
