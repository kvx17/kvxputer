#ifndef __Ethernet_MENU_H__
#define __Ethernet_MENU_H__

#include "menu/ethernet/EthernetHelper.h"
#include <MenuItemInterface.h>
#if !defined(LITE_VERSION)
class EthernetMenu : public MenuItemInterface {
private:
    EthernetHelper *eth;
    void start_ethernet();

public:
    EthernetMenu() : MenuItemInterface("Ethernet") {}

    void optionsMenu(void);
    void drawIcon(float scale);
    bool hasTheme() { return kvxConfig.theme.ethernet; }
    const String& themePath() override { return kvxConfig.theme.paths.ethernet; }
};

#endif
#endif
