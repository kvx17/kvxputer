#ifndef __DISCOVERY_MENU_H__
#define __DISCOVERY_MENU_H__

#include <MenuItemInterface.h>

class DiscoveryMenu : public MenuItemInterface {
public:
    DiscoveryMenu() : MenuItemInterface("Discovery") {}

    void optionsMenu(void);
    void drawIcon(float scale);
    bool hasTheme() { return false; }
    const String &themePath() override {
        static const String none;
        return none;
    }
};

#endif
