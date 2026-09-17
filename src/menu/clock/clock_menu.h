#ifndef __CLOCK_MENU_H__
#define __CLOCK_MENU_H__

#include <MenuItemInterface.h>

class ClockMenu : public MenuItemInterface {
public:
    ClockMenu() : MenuItemInterface("Clock") {}

    void optionsMenu(void);
    void showSubMenu(void);
    void drawIcon(float scale);
    bool hasTheme() { return kvxConfig.theme.clock; }
    const String& themePath() override { return kvxConfig.theme.paths.clock; }
};

#endif
