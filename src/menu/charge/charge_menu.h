#ifndef __CHARGE_MENU_H__
#define __CHARGE_MENU_H__

#include <MenuItemInterface.h>

class ChargeMenu : public MenuItemInterface {
public:
    ChargeMenu() : MenuItemInterface("Charge") {}

    void optionsMenu(void);
    void drawIcon(float scale);
    bool hasTheme() { return false; }
    const String &themePath() override {
        static const String none;
        return none;
    }
};

#endif
