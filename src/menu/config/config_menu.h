#ifndef __CONFIG_MENU_H__
#define __CONFIG_MENU_H__

#include <MenuItemInterface.h>

class ConfigMenu : public MenuItemInterface {
public:
    ConfigMenu() : MenuItemInterface("Config") {}

    void optionsMenu(void);
    void drawIcon(float scale);
    bool hasTheme() { return kvxConfig.theme.config; }
    const String &themePath() override { return kvxConfig.theme.paths.config; }

    // Public so mainscreen shortcuts can bind settings screens directly.
    void displayUIMenu(void);
    void ledMenu(void);
    void audioMenu(void);
    void systemMenu(void);
    void advancedMenu(void);
    void powerMenu(void);
    void pinsMenu(void);
    void devMenu(void);

private:
    void switchToUSBSerial(void);
    void switchToUARTSerial(void);
};

#endif
