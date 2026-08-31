#ifndef __NETOPS_MENU_H__
#define __NETOPS_MENU_H__

#include <MenuItemInterface.h>

class NetOpsMenu : public MenuItemInterface {
public:
    NetOpsMenu() : MenuItemInterface("NetOps") {}

    void optionsMenu(void);
    void drawIcon(float scale);
    bool hasTheme() { return false; }
    const String &themePath() override {
        static const String empty;
        return empty;
    }
};

#endif
