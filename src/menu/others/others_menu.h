#ifndef __OTHERS_MENU_H__
#define __OTHERS_MENU_H__

#include "MenuItemInterface.h"

class OthersMenu : public MenuItemInterface {

public:
    OthersMenu() : MenuItemInterface("Others") {}

    void micMenu();
    void optionsMenu(void);
    void drawIcon(float scale);

    bool hasTheme() { return kvxConfig.theme.others; }
    const String& themePath() override { return kvxConfig.theme.paths.others; }
};

#endif
