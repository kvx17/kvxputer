#include "clock_menu.h"
#include "root/ui/display.h"
#include "root/ui/settings.h"
#include "menu/others/timer.h"

void ClockMenu::optionsMenu() {
    while (!returnToMenu) {
        runClockLoop(true);

        // If ESC is pressed on the watch, it exits
        if (returnToMenu) break;

        // OK pressed, show submenu
        showSubMenu();

        // If "Exit" is pressed in the submenu, it exits
        if (returnToMenu) break;
    }
}

void ClockMenu::showSubMenu() {
    options = {
        {"Timer",         [=]() { Timer(); }            },
        {"Back to Clock", [=]() {}                      },
        {"Exit",          [=]() { returnToMenu = true; }}
        // Add more options here
    };

    delay(200);
    loopOptions(options);
}

void ClockMenu::drawIcon(float scale) {
    clearIconArea();
    int radius = scale * 30;
    int pointerSize = scale * 15;

    // Case
    tft.drawArc(
        iconCenterX, iconCenterY, 1.1 * radius, radius, 0, 360, kvxConfig.priColor, kvxConfig.bgColor
    );

    // Pivot center
    tft.fillCircle(iconCenterX, iconCenterY, radius / 10, kvxConfig.priColor);

    // Hours & minutes
    tft.drawLine(
        iconCenterX,
        iconCenterY,
        iconCenterX - 2 * pointerSize / 3,
        iconCenterY - 2 * pointerSize / 3,
        kvxConfig.priColor
    );
    tft.drawLine(
        iconCenterX, iconCenterY, iconCenterX + pointerSize, iconCenterY - pointerSize, kvxConfig.priColor
    );
}
