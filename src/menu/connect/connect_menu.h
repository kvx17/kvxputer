#ifndef __CONNECT_MENU_H__
#define __CONNECT_MENU_H__

#include <MenuItemInterface.h>

class ConnectMenu : public MenuItemInterface {
public:
    ConnectMenu() : MenuItemInterface("Connect") {}

    void optionsMenu(void);
    void drawIcon(float scale);
    bool hasTheme() { return kvxConfig.theme.connect; }
    const String& themePath() override { return kvxConfig.theme.paths.connect; }
};

#endif
