#ifndef __NRF24_MENU_H__
#define __NRF24_MENU_H__

#include <MenuItemInterface.h>

class NRF24Menu : public MenuItemInterface {
public:
    NRF24Menu() : MenuItemInterface("NRF24") {}

    void optionsMenu(void);
    void configMenu(void);
    void drawIcon(float scale);
    bool hasTheme() { return kvxConfig.theme.nrf; }
    const String& themePath() override { return kvxConfig.theme.paths.nrf; }
};

#endif
