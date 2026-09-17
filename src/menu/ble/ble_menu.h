#ifndef __BLE_MENU_H__
#define __BLE_MENU_H__

#include <MenuItemInterface.h>

class BleMenu : public MenuItemInterface {
public:
    BleMenu() : MenuItemInterface("BLE") {}

    void optionsMenu(void);
    void drawIcon(float scale);
    bool hasTheme() { return kvxConfig.theme.ble; }
    const String& themePath() override { return kvxConfig.theme.paths.ble; }

private:
    void configMenu(void);
    void setBleNameMenu(void);
};

#endif
